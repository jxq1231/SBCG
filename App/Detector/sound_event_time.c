/**
 * @file sound_event_time.c
 * @brief 用已完成传输组坐标或固定窗口TOA查询本地PTP模型时间。
 * @details 固定队列等右锚；旧接口保留FIRST+1整数语义，新接口使用Q16窗口TOA并输出独立记录。
 *          任何时域/流边界撤销旧配对，失败省略所有模型时间。
 * @note 仅串行前台，无PCM/HAL/动态分配；所有输出LOCAL=1/SYNC=0，不代表物理声学精度。
 */
#include "sound_event_time.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define ANCHOR_AGE_MS 250U
#define MODEL_MAX_AGE_MS 1500U
#define EVENT_WAIT_MS 2500U
#define STREAM_AGE_MS 1000U
#define RESULT_PERIOD_MS 250U
#define STATE_PERIOD_MS 1000U
typedef struct
{
    SoundCandidate candidate;
    SoundToaPosition toa;
    uint32_t sequence, submitted_ms;
    bool emit_toa;
    const char *failure;
} Pending;
static AudioTimeAnchorObservation anchors[SOUND_TIME_CAPACITY];
static Pending pending[SOUND_TIME_CAPACITY];
static uint8_t anchor_count, count;
static bool have_anchor_sequence, have_event_sequence, have_stream;
static uint32_t last_anchor_sequence, last_event_sequence, last_result_ms, last_state_ms;
static uint32_t last_dropped, stream_session;
static SoundEventTimeSnapshot stats;
static SoundEventTimeEvent toa_events[SOUND_TIME_EVENT_CAPACITY];
static uint8_t toa_event_count;
static void (*logger)(const char *, int);
_Static_assert(sizeof("$SOUND_TIME,SEQ=4294967295,SESSION=4294967295,FIRST=18446744073709551615,"
    "STATUS=OK,L=4294967295,R=4294967295,EST_NS=18446744073709551615,LO_NS=18446744073709551615,"
    "HI_NS=18446744073709551615,LOCAL=1,SYNC=0\r\n") <= 256, "event model line too long");
_Static_assert(sizeof("$SOUND_TOA_TIME,SEQ=4294967295,SESSION=4294967295,FRAME=18446744073709551615,"
    "Q16=65535,STATUS=TOA_INVALID,CORE=10,L=4294967295,R=4294967295,"
    "EST_NS=18446744073709551615,LO_NS=18446744073709551615,HI_NS=18446744073709551615,"
    "LOCAL=1,SYNC=0\r\n") <= 256, "TOA event line too long");
_Static_assert(sizeof("$SOUND_TOA_STATE,QUEUED=4294967295,OK=4294967295,REJECT=4294967295,"
    "MISSING=4294967295,INVALID=4294967295,DROPPED=4294967295,PENDING=4,LOCAL=1,SYNC=0\r\n")
    <= 256, "TOA state line too long");

/** @brief 饱和增加非NULL计数p；无返回值，仅前台。 */
static void Increment(uint32_t *p) { if (*p != UINT32_MAX) { ++*p; } }
/** @brief 将value转为至少21字节的非NULL out十进制文本，无返回值。 */
static void Decimal(uint64_t value, char out[21])
{
    char reversed[20];
    unsigned length = 0U;
    do { reversed[length++] = (char)('0' + value % 10U); value /= 10U; }
    while (value != 0U);
    for (unsigned i = 0U; i < length; ++i) { out[i] = reversed[length - i - 1U]; }
    out[length] = '\0';
}
/** @brief 发送临时line的n字节，非正/截断不发送；回调返回前复制，无返回值。 */
static void Emit(const char *line, int n)
{
    if (logger != NULL && n > 0 && n < 256) { logger(line, n); }
}
/** @brief reason为静态原因字符串；清锚并将尚未判定队项标拒绝，无返回值、不打印。 */
static void Invalidate(const char *reason)
{
    anchor_count = 0;
    have_anchor_sequence = false;
    for (uint8_t i = 0U; i < count; ++i)
    {
        if (pending[i].failure == NULL) { pending[i].failure = reason; }
    }
}
/** @brief 初始化前台软件状态；log可NULL停用，回调返回前消费字节；无返回值。 */
void SoundEventTime_Init(void (*log)(const char *, int))
{
    logger = log;
    anchor_count = count = 0U;
    have_anchor_sequence = have_event_sequence = have_stream = false;
    last_anchor_sequence = last_event_sequence = last_result_ms = last_state_ms = 0U;
    last_dropped = stream_session = 0U;
    stats = (SoundEventTimeSnapshot){0};
    toa_event_count = 0U;
    memset(anchors, 0, sizeof anchors);
    memset(pending, 0, sizeof pending);
    memset(toa_events, 0, sizeof toa_events);
}
/** @brief 按公开reason撤销旧模型；无返回值，不输出日志，队列中的身份保留供失败报告。 */
void SoundEventTime_Invalidate(SoundTimeReset reason)
{
    toa_event_count = 0U;
    memset(toa_events, 0, sizeof toa_events);
    Invalidate(reason == SOUND_TIME_CLOCK ? "CLOCK" : "STREAM");
}
/** @brief 校验公共候选字段和序号；不访问PCM，成功返回true。 */
static bool CandidateValid(const SoundCandidate *candidate, uint32_t sequence,
                           uint32_t now_ms)
{
    (void)now_ms;
    return logger != NULL && candidate != NULL && candidate->session != 0U &&
           candidate->onset_frame != UINT64_MAX &&
           candidate->confirmed_frame >= candidate->onset_frame &&
           sequence != 0U && (!have_event_sequence || sequence > last_event_sequence);
}
/** @brief 保留旧整数提交路径；新TOA字段标记为MISSING但不输出TOA记录。 */
bool SoundEventTime_Submit(uint32_t now_ms, uint32_t sequence,
                           const SoundCandidate *candidate)
{
    SoundToaPosition missing = {SOUND_TOA_MISSING, 0U, 0U};
    if (!CandidateValid(candidate, sequence, now_ms)) { Increment(&stats.dropped); return false; }
    have_event_sequence = true;
    last_event_sequence = sequence;
    if (count == SOUND_TIME_CAPACITY) { Increment(&stats.dropped); return false; }
    pending[count++] = (Pending){*candidate, missing, sequence, now_ms, false, NULL};
    Increment(&stats.queued);
    return true;
}
/** @brief 复制带窗口TOA的候选；只做标量校验，不访问PCM或硬件。 */
bool SoundEventTime_SubmitToa(uint32_t now_ms, uint32_t sequence,
                              const SoundCandidate *candidate,
                              const SoundToaPosition *position)
{
    if (position == NULL || !CandidateValid(candidate, sequence, now_ms))
    { Increment(&stats.dropped); return false; }
    SoundToaPosition toa = *position;
    if (toa.status != SOUND_TOA_MISSING && toa.status != SOUND_TOA_INVALID)
    {
        if (toa.status != SOUND_TOA_VALID || toa.frame == UINT64_MAX ||
            toa.frame > UINT64_MAX - 1U)
        {
            toa.status = SOUND_TOA_INVALID;
        }
    }
    have_event_sequence = true;
    last_event_sequence = sequence;
    if (count == SOUND_TIME_CAPACITY) { Increment(&stats.dropped); return false; }
    pending[count++] = (Pending){*candidate, toa, sequence, now_ms, true, NULL};
    Increment(&stats.queued);
    Increment(&stats.toa_queued);
    return true;
}
/** @brief 检查非NULL锚点局部字段；true为有效相位区间，无副作用。 */
static bool Valid(const AudioTimeMapAnchor *a)
{
    return a->valid && a->session != 0U && a->nsec < 1000000000U &&
           a->frame_hi != UINT64_MAX && a->frame_hi >= a->frame_lo &&
           a->frame_hi - a->frame_lo <= AUDIO_TIME_MAP_MAX_WIDTH_FRAMES;
}
/** @brief 非NULL有效a的本地PTP秒/纳秒合成uint64纳秒，无硬件读取。 */
static uint64_t Time(const AudioTimeMapAnchor *a)
{
    return (uint64_t)a->sec * UINT64_C(1000000000) + a->nsec;
}
/** @brief 保存now_ms下新鲜的observation副本；非法/缺序/会话变更清旧模型，无返回值不打印。 */
void SoundEventTime_Observe(uint32_t now_ms, const AudioTimeAnchorObservation *observation)
{
    if (logger == NULL) { return; }
    if (observation == NULL || !Valid(&observation->anchor) ||
        (uint32_t)(now_ms - observation->captured_ms) > ANCHOR_AGE_MS)
    { Invalidate("ANCHOR"); return; }
    if (have_anchor_sequence && observation->sequence != last_anchor_sequence + 1U)
    { Invalidate("GAP"); }
    if (anchor_count != 0U)
    {
        const AudioTimeMapAnchor *last = &anchors[anchor_count - 1U].anchor;
        if (last->session != observation->anchor.session) { Invalidate("SESSION"); }
        else if (Time(&observation->anchor) <= Time(last) ||
                 observation->anchor.frame_lo <= last->frame_hi)
        { Invalidate("ORDER"); return; }
    }
    have_anchor_sequence = true;
    last_anchor_sequence = observation->sequence;
    if (anchor_count == SOUND_TIME_CAPACITY)
    { memmove(anchors, anchors + 1, (SOUND_TIME_CAPACITY - 1U) * sizeof anchors[0]); --anchor_count; }
    anchors[anchor_count++] = *observation;
}
/**
 * @brief 旧整数模型：查询候选onset+1；返回NULL表示等待右锚，否则为结果/拒绝原因。
 */
static const char *ResolveLegacy(uint32_t now_ms, const Pending *item,
                                 AudioTimeMapResult *result, uint32_t *left,
                                 uint32_t *right, AudioTimeMapStatus *core)
{
    if (item->failure != NULL) { return item->failure; }
    if ((uint32_t)(now_ms - item->submitted_ms) >= EVENT_WAIT_MS) { return "TIMEOUT"; }
    const uint64_t position = item->candidate.onset_frame + 1U;
    if (anchor_count != 0U &&
        (uint32_t)(now_ms - anchors[anchor_count - 1U].captured_ms) > MODEL_MAX_AGE_MS)
    { return "STALE"; }
    if (anchor_count != 0U)
    {
        if (item->candidate.session != anchors[anchor_count - 1U].anchor.session) { return "SESSION"; }
        if (position < anchors[0].anchor.frame_hi + 1U) { return "NO_LEFT"; }
    }
    for (uint8_t i = 1U; i < anchor_count; ++i)
    {
        const AudioTimeMapAnchor *a = &anchors[i - 1U].anchor;
        const AudioTimeMapAnchor *b = &anchors[i].anchor;
        if (position >= a->frame_hi + 1U && position <= b->frame_lo)
        {
            *left = anchors[i - 1U].sequence;
            *right = anchors[i].sequence;
            *core = AudioTimeMap_Estimate(a, b, item->candidate.session, position, result);
            return *core == AUDIO_TIME_MAP_OK ? "OK" : "CORE";
        }
    }
    return NULL;
}
/**
 * @brief Q16模型：查询固定窗口TOA；返回NULL表示等待右锚，否则为结果/拒绝原因。
 * @note MISSING/INVALID不尝试任何模型，也不回退到FIRST+1。
 */
static const char *ResolveToa(uint32_t now_ms, const Pending *item,
                              AudioTimeMapResult *result, uint32_t *left,
                              uint32_t *right, AudioTimeMapStatus *core)
{
    if (item->failure != NULL) { return item->failure; }
    if ((uint32_t)(now_ms - item->submitted_ms) >= EVENT_WAIT_MS) { return "TIMEOUT"; }
    if (item->toa.status == SOUND_TOA_MISSING) { return "TOA_MISSING"; }
    if (item->toa.status != SOUND_TOA_VALID) { return "TOA_INVALID"; }
    if (item->toa.frame == UINT64_MAX) { return "TOA_INVALID"; }
    const uint64_t position = item->toa.frame + 1U;
    if (anchor_count != 0U &&
        (uint32_t)(now_ms - anchors[anchor_count - 1U].captured_ms) > MODEL_MAX_AGE_MS)
    { return "STALE"; }
    if (anchor_count != 0U)
    {
        if (item->candidate.session != anchors[anchor_count - 1U].anchor.session) { return "SESSION"; }
        if (position < anchors[0].anchor.frame_hi + 1U) { return "NO_LEFT"; }
    }
    for (uint8_t i = 1U; i < anchor_count; ++i)
    {
        const AudioTimeMapAnchor *a = &anchors[i - 1U].anchor;
        const AudioTimeMapAnchor *b = &anchors[i].anchor;
        if (position >= a->frame_hi + 1U && position <= b->frame_lo)
        {
            AudioTimeMapPosition position_q16 = {position, item->toa.fraction_q16};
            if (position == b->frame_lo && position_q16.fraction_q16 != 0U)
            { return NULL; }
            *left = anchors[i - 1U].sequence;
            *right = anchors[i].sequence;
            *core = AudioTimeMap_EstimateQ16(a, b, item->candidate.session,
                                             &position_q16, result);
            return *core == AUDIO_TIME_MAP_OK ? "OK" : "CORE";
        }
    }
    return NULL;
}
/** @brief 发送旧整数结果；格式保持历史$SOUND_TIME不变。 */
static void EmitLegacy(const Pending *item, const char *status,
                       const AudioTimeMapResult *result, uint32_t left,
                       uint32_t right, AudioTimeMapStatus core)
{
    char line[256];
    char frame[21];
    Decimal(item->candidate.onset_frame, frame);
    if (result->valid && core == AUDIO_TIME_MAP_OK && strcmp(status, "OK") == 0)
    {
        char estimate[21], lower[21], upper[21];
        Decimal(result->estimate_ns, estimate);
        Decimal(result->model_lower_ns, lower);
        Decimal(result->model_upper_ns, upper);
        Increment(&stats.ok);
        Emit(line, snprintf(line, sizeof line,
            "$SOUND_TIME,SEQ=%" PRIu32 ",SESSION=%" PRIu32 ",FIRST=%s,STATUS=OK,L=%" PRIu32
            ",R=%" PRIu32 ",EST_NS=%s,LO_NS=%s,HI_NS=%s,LOCAL=1,SYNC=0\r\n",
            item->sequence, item->candidate.session, frame, left, right,
            estimate, lower, upper));
    }
    else
    {
        Increment(&stats.rejected);
        Emit(line, snprintf(line, sizeof line,
            "$SOUND_TIME,SEQ=%" PRIu32 ",SESSION=%" PRIu32 ",FIRST=%s,STATUS=%s,CORE=%u,"
            "LOCAL=1,SYNC=0\r\n", item->sequence, item->candidate.session, frame,
            status, (unsigned)core));
    }
}
/** @brief 保存成功TOA标量副本；满队列丢弃事件副本但不改变已发布日志/模型。 */
static void PushEvent(const Pending *item, const AudioTimeMapResult *result)
{
    if (toa_event_count >= SOUND_TIME_EVENT_CAPACITY) { return; }
    toa_events[toa_event_count++] = (SoundEventTimeEvent){
        item->sequence, item->candidate.session, result->estimate_ns,
        item->toa.frame + 1U, item->toa.fraction_q16,
        item->candidate.mean_square_ac, item->candidate.peak_abs};
}
/** @brief 发送Q16窗口TOA结果或明确失败；缺失/非法不输出模型字段。 */
static void EmitToa(const Pending *item, const char *status,
                    const AudioTimeMapResult *result, uint32_t left,
                    uint32_t right, AudioTimeMapStatus core)
{
    char line[256];
    char frame[21];
    if (result->valid && core == AUDIO_TIME_MAP_OK && strcmp(status, "OK") == 0)
    {
        const uint64_t model_frame = item->toa.frame + 1U;
        Decimal(model_frame, frame);
        char estimate[21], lower[21], upper[21];
        Decimal(result->estimate_ns, estimate);
        Decimal(result->model_lower_ns, lower);
        Decimal(result->model_upper_ns, upper);
        Increment(&stats.toa_ok);
        Emit(line, snprintf(line, sizeof line,
            "$SOUND_TOA_TIME,SEQ=%" PRIu32 ",SESSION=%" PRIu32 ",FRAME=%s,Q16=%u,"
            "STATUS=OK,CORE=0,L=%" PRIu32 ",R=%" PRIu32 ",EST_NS=%s,LO_NS=%s,HI_NS=%s,"
            "LOCAL=1,SYNC=0\r\n", item->sequence, item->candidate.session, frame,
            (unsigned)item->toa.fraction_q16, left, right, estimate, lower, upper));
        PushEvent(item, result);
    }
    else
    {
        Increment(&stats.toa_rejected);
        if (strcmp(status, "TOA_MISSING") == 0) { Increment(&stats.toa_missing); }
        else if (strcmp(status, "TOA_INVALID") == 0) { Increment(&stats.toa_invalid); }
        Emit(line, snprintf(line, sizeof line,
            "$SOUND_TOA_TIME,SEQ=%" PRIu32 ",SESSION=%" PRIu32 ",STATUS=%s,CORE=%u,"
            "LOCAL=1,SYNC=0\r\n", item->sequence, item->candidate.session, status,
            (unsigned)core));
    }
}
/** @brief 在now_ms和非NULL snapshot健康流下处理队首；异常清旧配对，限速输出模型及状态。 */
void SoundEventTime_Process(uint32_t now_ms, const BSP_AudioSnapshot *snapshot)
{
    if (logger == NULL) { return; }
    if (snapshot == NULL || !snapshot->running || snapshot->session == 0U ||
        snapshot->index_faults != 0U || snapshot->callback_count == 0U ||
        (uint32_t)(now_ms - snapshot->last_callback_ms) >= STREAM_AGE_MS)
    { Invalidate("STREAM"); }
    else
    {
        if (have_stream &&
            (stream_session != snapshot->session || last_dropped != snapshot->dropped_blocks))
        { Invalidate("STREAM"); }
        if (anchor_count != 0U &&
            anchors[anchor_count - 1U].anchor.session != snapshot->session)
        { Invalidate("SESSION"); }
        have_stream = true;
        stream_session = snapshot->session;
        last_dropped = snapshot->dropped_blocks;
    }
    char line[256];
    if (count != 0U && (uint32_t)(now_ms - last_result_ms) >= RESULT_PERIOD_MS)
    {
        const Pending item = pending[0];
        AudioTimeMapResult legacy_result = {0};
        AudioTimeMapResult toa_result = {0};
        uint32_t legacy_left = 0U, legacy_right = 0U, toa_left = 0U, toa_right = 0U;
        AudioTimeMapStatus legacy_core = AUDIO_TIME_MAP_OK;
        AudioTimeMapStatus toa_core = AUDIO_TIME_MAP_OK;
        const char *legacy = ResolveLegacy(now_ms, &item, &legacy_result, &legacy_left,
                                           &legacy_right, &legacy_core);
        const char *toa = item.emit_toa ? ResolveToa(now_ms, &item, &toa_result, &toa_left,
                                                     &toa_right, &toa_core) : NULL;
        const bool toa_terminal = !item.emit_toa || toa != NULL;
        if (legacy != NULL && toa_terminal)
        {
            memmove(pending, pending + 1U, (count - 1U) * sizeof pending[0]);
            --count;
            last_result_ms = now_ms;
            EmitLegacy(&item, legacy, &legacy_result, legacy_left, legacy_right, legacy_core);
            if (item.emit_toa) { EmitToa(&item, toa, &toa_result, toa_left, toa_right, toa_core); }
        }
    }
    if ((uint32_t)(now_ms - last_state_ms) >= STATE_PERIOD_MS)
    {
        last_state_ms = now_ms;
        Emit(line, snprintf(line, sizeof line,
            "$SOUND_TIME_STATE,QUEUED=%" PRIu32 ",OK=%" PRIu32 ",REJECT=%" PRIu32
            ",DROPPED=%" PRIu32 ",PENDING=%u,LOCAL=1,SYNC=0\r\n",
            stats.queued, stats.ok, stats.rejected, stats.dropped, (unsigned)count));
        Emit(line, snprintf(line, sizeof line,
            "$SOUND_TOA_STATE,QUEUED=%" PRIu32 ",OK=%" PRIu32 ",REJECT=%" PRIu32
            ",MISSING=%" PRIu32 ",INVALID=%" PRIu32 ",DROPPED=%" PRIu32
            ",PENDING=%u,LOCAL=1,SYNC=0\r\n",
            stats.toa_queued, stats.toa_ok, stats.toa_rejected, stats.toa_missing,
            stats.toa_invalid, stats.dropped, (unsigned)count));
    }
}
/** @brief 将前台软件状态复制到非NULL out；NULL不操作，无返回值，不读硬件。 */
void SoundEventTime_GetSnapshot(SoundEventTimeSnapshot *out)
{
    if (out != NULL) { *out = stats; out->pending = count; }
}
/** @brief 取走一个成功TOA事件副本；无事件时清零输出，返回false。 */
bool SoundEventTime_TakeEvent(SoundEventTimeEvent *event)
{
    if (event == NULL || toa_event_count == 0U) { if (event != NULL) { memset(event, 0, sizeof *event); } return false; }
    *event = toa_events[0];
    memmove(toa_events, toa_events + 1U, (toa_event_count - 1U) * sizeof toa_events[0]);
    --toa_event_count;
    return true;
}
