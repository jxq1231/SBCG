/**
 * @file        sound_pair.c
 * @brief       两板同一声事件的纯C配对、声程差与基线距离计算。
 * @details     在同一共同时间域内，按类型/质量/时间窗口找出唯一A-B对；声源在基线延长线时
 *              d=c(T)|tB-tA|，符号给出A侧/B侧。重复、歧义、越窗、超量程均显式拒绝。
 *              B侧带候选齿时，以已接受（无历史时用标称）距离为唯一参考选择候选，一格以上
 *              跳变需连续确认；确认期间显示已接受距离，异常候选不能把参考拖离接受齿。
 * @note        仅前台纯计算；无网络/PCM/HAL/动态内存；输入时间必须先由同步层证明同一时间域。
 */
#include "sound_pair.h"
#include <stddef.h>
#include <string.h>

/** @brief 饱和增加非NULL计数。 */
static void Increment(uint32_t *value)
{
    if (*value != UINT32_MAX) { ++*value; }
}
/** @brief 配置基本合法性；不检查具体量程语义。 */
static bool ConfigValid(const SoundPairConfig *config)
{
    if (config == NULL || config->max_tdoa_ns == 0U ||
        config->max_distance_mm == 0U || config->sound_speed_mm_s == 0U ||
        config->min_quality > 100U || config->pair_timeout_ms == 0U)
    { return false; }
    if (config->nominal_distance_mm != 0U &&
        (config->tooth_jump_mm == 0U || config->tooth_confirm < 1U ||
         config->tooth_confirm > 8U))
    { return false; }
    return true;
}
/** @brief 1/65536帧的候选偏移转纳秒，四舍五入；无硬件访问。 */
static int64_t Q16ToNs(int32_t offset_q16)
{
    const int64_t numerator = (int64_t)offset_q16 * (int64_t)SOUND_PAIR_NS_PER_SEC;
    const int64_t denominator = 65536LL * 48000LL;
    return numerator >= 0 ? (numerator + denominator / 2) / denominator :
                            (numerator - denominator / 2) / denominator;
}
/** @brief 两个非NULL距离的绝对差，单位mm；不做饱和假设外的其它判断。 */
static uint32_t DistanceGap(uint32_t first, uint32_t second)
{
    return first > second ? first - second : second - first;
}
/**
 * @brief 用A时间+已接受距离从B候选齿中选一个；无候选/未配置时保持原事件。
 * @param manager 非NULL配对状态，允许更新接受/待确认距离。
 * @param config 非NULL已校验配置，只读。
 * @param a 非NULL A事件，仅读取time_ns。
 * @param b 非NULL B事件，携带候选Q16偏移。
 * @param out 非NULL输出B事件副本；无选择时复制b。
 * @return 无返回值；只改B时间，不改A侧或配置。
 * @note 参考距离只能是已接受齿或标称齿，绝不采用待确认齿；否则一次异常候选会连续偏向
 *       邻近齿并在N次后被错误确认。一格内直接跟随，一格以上需连续N次同向确认；
 *       确认期间由调用方保持已接受距离输出。
 */
static void SelectToaTooth(SoundPairManager *manager, const SoundPairConfig *config,
                           const SoundEventRecord *a, const SoundEventRecord *b,
                           SoundEventRecord *out, uint8_t *selected_index,
                           bool *held)
{
    *out = *b;
    if (selected_index != NULL) { *selected_index = 0U; }
    if (held != NULL) { *held = false; }
    if (config->nominal_distance_mm == 0U || b->toa_candidate_count == 0U ||
        b->toa_candidate_count > SOUND_EVENT_MAX_TOA_CANDIDATES)
    { return; }
    const uint32_t reference = manager->accepted_valid ? manager->accepted_distance_mm :
                               config->nominal_distance_mm;
    uint32_t best_distance = 0U;
    uint64_t best_time = b->time_ns;
    uint8_t best_index = 0U;
    bool have_best = false;
    for (uint8_t i = 0U; i < b->toa_candidate_count; ++i)
    {
        const int64_t offset = Q16ToNs(b->toa_candidate_q16[i]);
        if (offset < 0 && (uint64_t)(-offset) > b->time_ns) { continue; }
        const uint64_t candidate_time = offset < 0 ? b->time_ns - (uint64_t)(-offset) :
                                        b->time_ns + (uint64_t)offset;
        const int64_t delta = (int64_t)candidate_time - (int64_t)a->time_ns;
        const uint64_t magnitude = delta < 0 ? (uint64_t)(-(delta + 1)) + 1U :
                                               (uint64_t)delta;
        const uint64_t distance = (magnitude * config->sound_speed_mm_s +
                                   SOUND_PAIR_NS_PER_SEC / 2U) / SOUND_PAIR_NS_PER_SEC;
        if (!have_best ||
            DistanceGap((uint32_t)distance, reference) <
            DistanceGap(best_distance, reference))
        {
            best_distance = (uint32_t)distance;
            best_time = candidate_time;
            best_index = i;
            have_best = true;
        }
    }
    if (!have_best) { return; }
    if (!manager->accepted_valid)
    {
        manager->accepted_distance_mm = best_distance;
        manager->accepted_valid = 1U;
        manager->pending_count = 0U;
    }
    else if (DistanceGap(best_distance, manager->accepted_distance_mm) <=
             config->tooth_jump_mm)
    {
        manager->accepted_distance_mm = best_distance;
        manager->pending_count = 0U;
    }
    else
    {
        if (manager->pending_count == 0U ||
            DistanceGap(best_distance, manager->pending_distance_mm) >
            config->tooth_jump_mm)
        {
            manager->pending_distance_mm = best_distance;
            manager->pending_count = 1U;
        }
        else if (++manager->pending_count >= config->tooth_confirm)
        {
            manager->accepted_distance_mm = best_distance;
            manager->pending_count = 0U;
        }
        if (manager->pending_count != 0U && held != NULL) { *held = true; }
    }
    out->time_ns = best_time;
    if (selected_index != NULL) { *selected_index = (uint8_t)(best_index + 1U); }
}
/** @brief 结果清零；NULL安全。 */
static void ClearResult(SoundPairResult *result)
{
    if (result != NULL) { memset(result, 0, sizeof *result); }
}
/**
 * @brief 验证一对事件是否满足配对前置条件，并计算声程差/距离。
 * @param config 非NULL已校验配置。
 * @param a A板事件，不可NULL。
 * @param b B板事件，不可NULL。
 * @param result 非NULL输出；成功valid=true，失败valid=false且status说明原因。
 * @return 同result->status，便于调用方判断。
 */
SoundPairStatus SoundPair_Compute(const SoundPairConfig *config,
                                  const SoundEventRecord *a,
                                  const SoundEventRecord *b,
                                  SoundPairResult *result)
{
    if (result == NULL) { return SOUND_PAIR_BAD_ARGUMENT; }
    ClearResult(result);
    /* 诊断元数据按输入复制；即使后续拒绝也保留，不能改变原决定。 */
    if (a != NULL)
    {
        result->a_session = a->session;
        result->a_sequence = a->sequence;
        result->a_time_ns = a->time_ns;
    }
    if (b != NULL)
    {
        result->b_session = b->session;
        result->b_sequence = b->sequence;
        result->b_time_ns = b->time_ns;
        result->b_toa_candidate_count = b->toa_candidate_count;
        for (uint8_t i = 0U; i < SOUND_EVENT_MAX_TOA_CANDIDATES; ++i)
            result->b_toa_candidate_q16[i] = b->toa_candidate_q16[i];
    }
    if (!ConfigValid(config) || a == NULL || b == NULL)
    { result->status = SOUND_PAIR_BAD_ARGUMENT; return result->status; }
    if (!a->valid || !b->valid || a->type == SOUND_EVENT_INVALID ||
        a->side != SOUND_SIDE_A || b->side != SOUND_SIDE_B)
    { result->status = SOUND_PAIR_INVALID_EVENT; return result->status; }
    if (a->type != b->type)
    { result->status = SOUND_PAIR_TYPE; return result->status; }
    if (a->quality < config->min_quality || b->quality < config->min_quality)
    { result->status = SOUND_PAIR_QUALITY; return result->status; }
    if (a->time_domain == 0U || a->time_domain != b->time_domain)
    { result->status = SOUND_PAIR_DOMAIN; return result->status; }
    const int64_t delta = (int64_t)b->time_ns - (int64_t)a->time_ns;
    const uint64_t magnitude = delta < 0 ? (uint64_t)(-(delta + 1)) + 1U
                                          : (uint64_t)delta;
    if (magnitude > config->max_tdoa_ns)
    { result->status = SOUND_PAIR_TDOA_RANGE; return result->status; }
    const uint64_t numerator = magnitude * (uint64_t)config->sound_speed_mm_s +
                               SOUND_PAIR_NS_PER_SEC / 2U;
    const uint64_t distance_mm = numerator / SOUND_PAIR_NS_PER_SEC;
    if (distance_mm > config->max_distance_mm)
    { result->status = SOUND_PAIR_DISTANCE_RANGE; return result->status; }
    result->valid = true;
    result->status = SOUND_PAIR_OK;
    result->delta_ns = delta;
    result->distance_mm = (uint32_t)distance_mm;
    result->direction = delta > 0 ? SOUND_DIRECTION_A :
                        (delta < 0 ? SOUND_DIRECTION_B : SOUND_DIRECTION_UNKNOWN);
    result->quality = a->quality < b->quality ? a->quality : b->quality;
    result->time_domain = a->time_domain;
    result->a_sequence = a->sequence;
    result->b_sequence = b->sequence;
    result->sound_speed_mm_s = config->sound_speed_mm_s;
    return result->status;
}
/** @brief 检查一对事件是否满足时间窗口和身份门槛；忽略距离量程以便区分歧义。 */
static bool CandidatePair(const SoundPairConfig *config,
                          const SoundEventRecord *a, const SoundEventRecord *b)
{
    if (!a->valid || !b->valid || a->type == SOUND_EVENT_INVALID ||
        a->type != b->type || a->side != SOUND_SIDE_A || b->side != SOUND_SIDE_B ||
        a->quality < config->min_quality || b->quality < config->min_quality ||
        a->time_domain == 0U || a->time_domain != b->time_domain)
    { return false; }
    const int64_t delta = (int64_t)b->time_ns - (int64_t)a->time_ns;
    const uint64_t magnitude = delta < 0 ? (uint64_t)(-(delta + 1)) + 1U
                                          : (uint64_t)delta;
    return magnitude <= config->max_tdoa_ns;
}
/** @brief 队尾追加结果；满则计数丢弃，不覆盖旧结果。 */
static void PushResult(SoundPairManager *manager, const SoundPairResult *result)
{
    if (manager->result_count >= SOUND_PAIR_RESULT_CAPACITY)
    {
        Increment(&manager->stats.result_dropped);
        return;
    }
    manager->results[manager->result_count++] = *result;
}
/** @brief 初始化管理器并复制配置；参数非法返回false并清空。 */
bool SoundPair_Init(SoundPairManager *manager, const SoundPairConfig *config)
{
    if (manager == NULL) { return false; }
    memset(manager, 0, sizeof *manager);
    if (!ConfigValid(config)) { return false; }
    manager->config = *config;
    if (config->nominal_distance_mm != 0U)
    {
        manager->accepted_distance_mm = config->nominal_distance_mm;
        manager->accepted_valid = 1U;
    }
    return true;
}
/** @brief 提交事件副本；重复/非法/队列满返回false并计数，不覆盖已有项。 */
bool SoundPair_Submit(SoundPairManager *manager, const SoundEventRecord *event,
                      uint32_t now_ms)
{
    if (manager == NULL || event == NULL || !event->valid ||
        event->type == SOUND_EVENT_INVALID || event->side > SOUND_SIDE_B ||
        event->quality < manager->config.min_quality || event->time_domain == 0U ||
        event->sequence == 0U)
    { if (manager != NULL) { Increment(&manager->stats.rejected); } return false; }
    const uint8_t side = event->side == SOUND_SIDE_A ? 0U : 1U;
    for (uint8_t i = 0U; i < manager->count[side]; ++i)
    {
        const SoundEventRecord *old = &manager->events[side][i];
        if (old->time_domain == event->time_domain &&
            old->session == event->session && old->sequence == event->sequence)
        { Increment(&manager->stats.duplicate); return false; }
    }
    if (manager->count[side] >= SOUND_PAIR_QUEUE_CAPACITY)
    { Increment(&manager->stats.rejected); return false; }
    manager->events[side][manager->count[side]] = *event;
    manager->submitted_ms[side][manager->count[side]] = now_ms;
    ++manager->count[side];
    Increment(&manager->stats.accepted);
    return true;
}
/**
 * @brief 过期未配对事件并生成所有可确定结果；不阻塞、无硬件访问。
 * @note 多个候选对同时落在窗口内时输出一条AMBIGUOUS并消费涉及事件，避免反复猜测。
 */
void SoundPair_Process(SoundPairManager *manager, uint32_t now_ms)
{
    if (manager == NULL) { return; }
    /* Remove stale events first, preserving order. */
    for (uint8_t side = 0U; side < 2U; ++side)
    {
        uint8_t write = 0U;
        for (uint8_t read = 0U; read < manager->count[side]; ++read)
        {
            if ((uint32_t)(now_ms - manager->submitted_ms[side][read]) >=
                manager->config.pair_timeout_ms)
            { Increment(&manager->stats.expired); continue; }
            if (write != read)
            {
                manager->events[side][write] = manager->events[side][read];
                manager->submitted_ms[side][write] = manager->submitted_ms[side][read];
            }
            ++write;
        }
        manager->count[side] = write;
    }
    bool used_a[SOUND_PAIR_QUEUE_CAPACITY] = {false};
    bool used_b[SOUND_PAIR_QUEUE_CAPACITY] = {false};
    bool progressed = true;
    while (progressed && manager->result_count < SOUND_PAIR_RESULT_CAPACITY)
    {
        progressed = false;
        uint8_t pair_a = 0U, pair_b = 0U;
        unsigned candidates = 0U;
        bool ambiguous = false;
        for (uint8_t i = 0U; i < manager->count[0]; ++i)
        {
            if (used_a[i]) { continue; }
            unsigned matches = 0U;
            uint8_t last = 0U;
            for (uint8_t j = 0U; j < manager->count[1]; ++j)
            {
                if (used_b[j]) { continue; }
                if (CandidatePair(&manager->config, &manager->events[0][i],
                                  &manager->events[1][j]))
                { ++matches; last = j; ++candidates; }
            }
            if (matches == 1U && candidates == 1U) { pair_a = i; pair_b = last; }
            if (matches > 1U || candidates > 1U) { ambiguous = true; }
        }
        if (candidates == 0U) { break; }
        if (ambiguous || candidates != 1U)
        {
            SoundPairResult invalid = {0};
            invalid.valid = false;
            invalid.status = SOUND_PAIR_AMBIGUOUS;
            for (uint8_t i = 0U; i < manager->count[0]; ++i) { used_a[i] = true; }
            for (uint8_t j = 0U; j < manager->count[1]; ++j) { used_b[j] = true; }
            Increment(&manager->stats.ambiguous);
            PushResult(manager, &invalid);
            progressed = true;
            break;
        }
        SoundPairResult result = {0};
        SoundEventRecord selected_b;
        uint8_t selected_index = 0U;
        bool held = false;
        SelectToaTooth(manager, &manager->config, &manager->events[0][pair_a],
                       &manager->events[1][pair_b], &selected_b, &selected_index,
                       &held);
        (void)SoundPair_Compute(&manager->config, &manager->events[0][pair_a],
                                &selected_b, &result);
        result.b_toa_selected = selected_index;
        result.b_toa_held = held;
        if (held && result.valid)
        {
            /* 确认期间保持旧齿：用接受距离重建输出，不显示未确认的新齿。 */
            const uint64_t magnitude_ns =
                ((uint64_t)manager->accepted_distance_mm * SOUND_PAIR_NS_PER_SEC +
                 manager->config.sound_speed_mm_s / 2U) /
                manager->config.sound_speed_mm_s;
            const int64_t delta = result.direction == SOUND_DIRECTION_A ?
                (int64_t)magnitude_ns :
                (result.direction == SOUND_DIRECTION_B ? -(int64_t)magnitude_ns : 0);
            result.delta_ns = delta;
            result.distance_mm = manager->accepted_distance_mm;
            result.b_time_ns = (uint64_t)((int64_t)manager->events[0][pair_a].time_ns + delta);
        }
        used_a[pair_a] = true;
        used_b[pair_b] = true;
        if (result.valid) { Increment(&manager->stats.paired); }
        else { Increment(&manager->stats.rejected); }
        PushResult(manager, &result);
        progressed = true;
        break;
    }
    /* Compact consumed pairs. */
    for (uint8_t side = 0U; side < 2U; ++side)
    {
        const bool *used = side == 0U ? used_a : used_b;
        uint8_t write = 0U;
        for (uint8_t read = 0U; read < manager->count[side]; ++read)
        {
            if (used[read]) { continue; }
            if (write != read)
            {
                manager->events[side][write] = manager->events[side][read];
                manager->submitted_ms[side][write] = manager->submitted_ms[side][read];
            }
            ++write;
        }
        manager->count[side] = write;
    }
}
/** @brief 取出一个结果；NULL不消费；无结果返回false。 */
bool SoundPair_Take(SoundPairManager *manager, SoundPairResult *result)
{
    if (manager == NULL || result == NULL || manager->result_count == 0U) { return false; }
    *result = manager->results[0];
    memmove(manager->results, manager->results + 1U,
            (manager->result_count - 1U) * sizeof manager->results[0]);
    --manager->result_count;
    return true;
}
/** @brief 复制计数器；NULL不操作。 */
void SoundPair_GetSnapshot(const SoundPairManager *manager, SoundPairSnapshot *out)
{
    if (manager == NULL || out == NULL) { return; }
    *out = manager->stats;
    out->pending_a = manager->count[0];
    out->pending_b = manager->count[1];
    out->pending_results = manager->result_count;
}
