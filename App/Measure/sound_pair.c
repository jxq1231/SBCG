/**
 * @file        sound_pair.c
 * @brief       两板同一声事件的纯C配对、声程差与基线距离计算。
 * @details     在同一共同时间域内，按类型/质量/时间窗口找出唯一A-B对；声源在基线延长线时
 *              d=c(T)|tB-tA|，符号给出A侧/B侧。重复、歧义、越窗、超量程均显式拒绝。
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
    return config != NULL && config->max_tdoa_ns > 0U &&
           config->max_distance_mm > 0U && config->sound_speed_mm_s > 0U &&
           config->min_quality <= 100U && config->pair_timeout_ms > 0U;
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
            invalid.time_domain = manager->config.max_tdoa_ns == 0U ? 0U : 0U;
            for (uint8_t i = 0U; i < manager->count[0]; ++i) { used_a[i] = true; }
            for (uint8_t j = 0U; j < manager->count[1]; ++j) { used_b[j] = true; }
            Increment(&manager->stats.ambiguous);
            PushResult(manager, &invalid);
            progressed = true;
            break;
        }
        SoundPairResult result = {0};
        (void)SoundPair_Compute(&manager->config, &manager->events[0][pair_a],
                                &manager->events[1][pair_b], &result);
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
