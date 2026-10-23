/**
 * @file        audio_time_anchor.c
 * @brief       采集音频传输进度与本地PTP时钟的夹读锚点。
 * @details     至少一秒间隔先读游标、读一次PTP、再读游标，检查会话/边界/顺序/宽度后交接单槽观测。
 * @note        依赖音频与本地PTP初始化；SYNC=0，不代表跨板同步或PCM声学时刻。
 */
#include "audio_time_anchor.h"
#include "BSP/bsp_audio.h"
#include "BSP/bsp_ptp.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ANCHOR_PERIOD_MS 1000U
#define ANCHOR_LINE_CAPACITY 256U
_Static_assert(sizeof("$AUDIO_ANCHOR,SEQ=4294967295,STATUS=OK,VALID=1,"
    "SESSION=4294967295,F_LO=18446744073709551615,F_HI=18446744073709551615,"
    "SEC=4294967295,NSEC=999999999,WIDTH=4,SYNC=0,OK=4294967295,REJECTS=4294967295\r\n")
    <= ANCHOR_LINE_CAPACITY, "anchor line exceeds application queue capacity");

/** @brief 夹读及跨次单调性检查的日志拒绝原因。 */
typedef enum { ANCHOR_OK, ANCHOR_CURSOR, ANCHOR_PTP, ANCHOR_SESSION,
               ANCHOR_BOUNDARY, ANCHOR_ORDER, ANCHOR_WIDE, ANCHOR_BACKWARD } AnchorStatus;
/** @brief 一次前台夹读的临时快照；含两个进度标量及一次PTP读取结果。 */
typedef struct
{
    BSP_AudioCursor before, after;
    BSP_PtpTime stamp;
    BSP_PtpStatus ptp_status;
} Anchor;
static AudioTimeAnchorLog log_line;
static uint32_t last_attempt_ms, sequence, successes, rejections, previous_session;
static uint64_t previous_time_ns, previous_frame_hi;
static bool have_previous, clock_faulted;
static AudioTimeAnchorObservation latest_observation;
static bool observation_pending;

/**
 * @brief   饱和递增锚点诊断计数。
 * @param   value 可写uint32_t计数器，不可为NULL。
 * @return  无返回值。
 * @note    仅前台，达到UINT32_MAX后保持，不提供并发同步。
 */
static void Increment(uint32_t *value) { if (*value != UINT32_MAX) { ++*value; } }
/**
 * @brief   将64位无符号数转成十进制文本。
 * @param   value 待格式化整数。
 * @param   output 至少21字节可写缓冲，不可为NULL；输出以NUL结尾。
 * @return  无返回值。
 * @note    仅前台格式化，避免依赖精简printf的64位整数支持。
 */
static void Decimal64(uint64_t value, char output[21])
{
    char reverse[20]; unsigned count = 0U;
    do { reverse[count++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U);
    for (unsigned i = 0; i < count; ++i) { output[i] = reverse[count-i-1U]; }
    output[count] = '\0';
}

/**
 * @brief   在两次音频游标之间读取一次PTP并校验夹读。
 * @param   anchor 调用方可写临时对象，不可为NULL；失败时可能只填写部分字段。
 * @return  成功或游标、PTP、会话、边界、顺序、宽度拒绝原因。
 * @note    仅前台；整体夹读不屏蔽IRQ，不领取或读取PCM缓冲。
 */
static AnchorStatus ReadAnchor(Anchor *anchor)
{
    if (!BSP_Audio_ReadCursor(&anchor->before)) { return ANCHOR_CURSOR; }
    /* No surrounding IRQ mask: PTP stays in its existing foreground context. */
    anchor->ptp_status = BSP_PTP_Read(&anchor->stamp);
    if (anchor->ptp_status != BSP_PTP_OK) { return ANCHOR_PTP; }
    if (anchor->stamp.nanoseconds >= 1000000000U)
    { anchor->ptp_status = BSP_PTP_BAD_SNAPSHOT; return ANCHOR_PTP; }
    if (!BSP_Audio_ReadCursor(&anchor->after)) { return ANCHOR_CURSOR; }
    if (anchor->before.session == 0U || anchor->before.session != anchor->after.session)
    { return ANCHOR_SESSION; }
    if (anchor->before.boundary_generation != anchor->after.boundary_generation)
    { return ANCHOR_BOUNDARY; }
    if (anchor->after.complete_frames < anchor->before.complete_frames) { return ANCHOR_ORDER; }
    if (anchor->after.complete_frames - anchor->before.complete_frames > AUDIO_ANCHOR_MAX_WIDTH_FRAMES)
    { return ANCHOR_WIDE; }
    return ANCHOR_OK;
}

/**
 * @brief   复位锚点历史、故障锁存和单槽状态。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   log 日志回调，可为NULL以关闭对应前台处理；非空回调须返回前消费或复制字节，不保留临时缓冲指针。
 * @return  无返回值。
 * @note    应用前台初始化调用，音频与PTP应已准备好；不初始化外设。
 */
void AudioTimeAnchor_Init(uint32_t now_ms, AudioTimeAnchorLog log)
{
    log_line = log; last_attempt_ms = now_ms;
    sequence = successes = rejections = previous_session = 0U;
    previous_time_ns = previous_frame_hi = 0U;
    have_previous = clock_faulted = false;
    memset(&latest_observation, 0, sizeof latest_observation);
    observation_pending = false;
}

/**
 * @brief   消费尚未读取的最新一次锚点尝试。
 * @param   observation 调用方可写输出；NULL不消费，非NULL在无待取数据时清零。
 * @return  取到观测返回true，无数据或NULL返回false。
 * @note    仅前台单消费者；新尝试覆盖单槽，失败尝试的anchor.valid=false，序号暴露遗漏。
 */
bool AudioTimeAnchor_TakeObservation(AudioTimeAnchorObservation *observation)
{
    if (observation == NULL) { return false; }
    memset(observation, 0, sizeof *observation);
    if (!observation_pending) { return false; }
    *observation = latest_observation;
    observation_pending = false;
    return true;
}

/**
 * @brief   按至少一秒间隔采锚、检查单调性并发布观测。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   processing_allowed 本次是否允许前台处理；DHT11启动拉低期间应为false。
 * @return  无返回值。
 * @note    App_Process前台调用；同会话时钟不递增会锁存拒绝，需新会话成功或重新初始化恢复；不标记跨板同步。
 */
void AudioTimeAnchor_Process(uint32_t now_ms, bool processing_allowed)
{
    if (log_line == NULL || !processing_allowed ||
        (uint32_t)(now_ms - last_attempt_ms) < ANCHOR_PERIOD_MS) { return; }
    last_attempt_ms = now_ms;
    ++sequence;
    Anchor anchor = {0};
    AnchorStatus status = ReadAnchor(&anchor);
    const uint64_t time_ns = (uint64_t)anchor.stamp.seconds * 1000000000U + anchor.stamp.nanoseconds;
    if (status == ANCHOR_OK && have_previous && anchor.before.session == previous_session)
    {
        if (clock_faulted || time_ns <= previous_time_ns)
        { clock_faulted = true; status = ANCHOR_BACKWARD; }
        else if (anchor.before.complete_frames <= previous_frame_hi) { status = ANCHOR_ORDER; }
    }
    /* CPU scalar snapshot only; no DMA buffer ownership or second PTP read. */
    latest_observation = (AudioTimeAnchorObservation){0};
    latest_observation.sequence = sequence;
    latest_observation.captured_ms = now_ms;
    if (status == ANCHOR_OK)
    {
        latest_observation.anchor = (AudioTimeMapAnchor){true,
            anchor.before.session, anchor.before.complete_frames,
            anchor.after.complete_frames, anchor.stamp.seconds, anchor.stamp.nanoseconds};
    }
    observation_pending = true;
    char line[ANCHOR_LINE_CAPACITY];
    int length;
    if (status == ANCHOR_OK)
    {
        Increment(&successes);
        previous_session = anchor.before.session; previous_time_ns = time_ns;
        previous_frame_hi = anchor.after.complete_frames;
        have_previous = true; clock_faulted = false;
        char lo[21], hi[21];
        Decimal64(anchor.before.complete_frames, lo); Decimal64(anchor.after.complete_frames, hi);
        length = snprintf(line, sizeof line,
            "$AUDIO_ANCHOR,SEQ=%" PRIu32 ",STATUS=OK,VALID=1,SESSION=%" PRIu32
            ",F_LO=%s,F_HI=%s,SEC=%" PRIu32 ",NSEC=%" PRIu32 ",WIDTH=%" PRIu32
            ",SYNC=0,OK=%" PRIu32 ",REJECTS=%" PRIu32 "\r\n",
            sequence, anchor.before.session, lo, hi, anchor.stamp.seconds, anchor.stamp.nanoseconds,
            (uint32_t)(anchor.after.complete_frames - anchor.before.complete_frames), successes, rejections);
    }
    else
    {
        static const char *const names[] = {"OK", "CURSOR", "PTP", "SESSION", "BOUNDARY", "ORDER", "WIDE", "BACKWARD"};
        Increment(&rejections);
        length = snprintf(line, sizeof line,
            "$AUDIO_ANCHOR,SEQ=%" PRIu32 ",STATUS=%s,VALID=0,PTP_ERR=%" PRIu32
            ",SYNC=0,OK=%" PRIu32 ",REJECTS=%" PRIu32 "\r\n",
            sequence, names[status], (uint32_t)anchor.ptp_status, successes, rejections);
    }
    if (length > 0 && (size_t)length < sizeof line) { log_line(line, length); }
}
