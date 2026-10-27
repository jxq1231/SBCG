/**
 * @file        ranging_tracker.c
 * @brief       纯C有符号TDOA跟踪器实现。
 * @details     以独立声事件为单位维护确认中值、候选新簇、去重身份和新鲜度；
 *              单次拒绝只进入HOLD，三次一致新簇才整体换窗。
 * @note        无HAL/网络/UI/PCM访问，无动态内存；调用方负责上下文健康和温度/标定。
 */
#include "ranging_tracker.h"
#include <stddef.h>
#include <string.h>

/** @brief 有符号64位绝对值；INT64_MIN按位安全处理。 */
static uint64_t Magnitude64(int64_t value)
{
    return value < 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
}

/** @brief 两个无符号64位源时刻的毫秒差；假定后一个不小于前一个。 */
static uint32_t SourceGapMs(uint64_t later, uint64_t earlier)
{
    const uint64_t delta = later - earlier;
    return (uint32_t)(delta / RANGING_TRACKER_NS_PER_MS);
}

/** @brief 返回从源时刻到当前共同时间的毫秒年龄；now早于源时按0处理。 */
static uint32_t SourceAgeMs(uint64_t now_common_ns, uint64_t source_ns)
{
    if (now_common_ns <= source_ns) { return 0U; }
    const uint64_t delta = now_common_ns - source_ns;
    const uint64_t age = delta / RANGING_TRACKER_NS_PER_MS;
    return age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
}

/** @brief 将声音到达两板的时间差按当前声速转换为诊断距离，饱和不补零。 */
static int32_t DeltaToDistanceMm(int64_t delta_ns, uint32_t sound_speed_mm_s)
{
    if (sound_speed_mm_s == 0U) { return 0; }
    const uint64_t numerator = Magnitude64(delta_ns) * (uint64_t)sound_speed_mm_s +
                               UINT64_C(500000000);
    const uint64_t distance = numerator / UINT64_C(1000000000);
    return distance > (uint64_t)INT32_MAX ? INT32_MAX : (int32_t)distance;
}

/** @brief 返回当前接受窗口中值；窗口必须非空。 */
static int64_t AcceptedMedian(const RangingTracker *self)
{
    int64_t sorted[RANGING_TRACKER_ACCEPTED_WINDOW];
    const uint32_t count = self->accepted_count;
    for (uint32_t i = 0U; i < count; ++i) { sorted[i] = self->accepted_ns[i]; }
    for (uint32_t i = 1U; i < count; ++i)
    {
        const int64_t value = sorted[i];
        uint32_t j = i;
        while (j > 0U && sorted[j - 1U] > value) { sorted[j] = sorted[j - 1U]; --j; }
        sorted[j] = value;
    }
    return sorted[count / 2U];
}

/** @brief 确认窗口加入一个已接受样本；满窗丢弃最旧值。 */
static void AddAccepted(RangingTracker *self, int64_t delta_ns)
{
    if (self->accepted_count < self->config.accepted_window)
    {
        self->accepted_ns[self->accepted_count++] = delta_ns;
        return;
    }
    for (uint32_t i = 1U; i < self->accepted_count; ++i)
    { self->accepted_ns[i - 1U] = self->accepted_ns[i]; }
    self->accepted_ns[self->accepted_count - 1U] = delta_ns;
}

/** @brief 用当前接受窗口刷新展示中值；小于配置滞回时保留旧显示值。 */
static void RefreshDisplayValue(RangingTracker *self)
{
    const int64_t median = AcceptedMedian(self);
    if (self->display_delta_ns != 0)
    {
        const int64_t difference = median > self->display_delta_ns ?
            median - self->display_delta_ns : self->display_delta_ns - median;
        if (Magnitude64(difference) < self->config.display_delta_hysteresis_ns)
        { return; }
    }
    self->display_delta_ns = median;
}

/** @brief 新确认结果分配递增ID；ID耗尽后停止增加而不回绕。 */
static void AllocateMeasurementId(RangingTracker *self)
{
    if (self->next_measurement_id != UINT32_MAX) { ++self->next_measurement_id; }
    self->measurement_id = self->next_measurement_id;
}

/** @brief 清空确认/候选/展示，但保留去重高水位和ID代际；用于硬边界恢复。 */
static void ClearSamples(RangingTracker *self)
{
    memset(self->accepted_ns, 0, sizeof self->accepted_ns);
    memset(self->candidate_ns, 0, sizeof self->candidate_ns);
    memset(self->candidate_source_ns, 0, sizeof self->candidate_source_ns);
    self->accepted_count = 0U;
    self->candidate_count = 0U;
    self->last_source_ns = 0U;
    self->has_source = false;
    self->last_source_ns_for_display = 0U;
    self->display_delta_ns = 0;
    self->display_quality = 0U;
    self->display_evidence = RANGING_EVIDENCE_PROXY;
}

/** @brief 判断上下文是否为健康且身份完整。 */
static bool ContextUsable(const RangingTrackerContext *context)
{
    return context != NULL && context->time_domain != 0U &&
           context->a_session != 0U && context->b_session != 0U &&
           context->domain_valid && context->stream_valid &&
           context->temperature_valid;
}

/** @brief 比较上下文身份字段；健康标志变化也视为边界。 */
static bool ContextEqual(const RangingTrackerContext *a, const RangingTrackerContext *b)
{
    return a->time_domain == b->time_domain && a->a_session == b->a_session &&
           a->b_session == b->b_session && a->a_clock_epoch == b->a_clock_epoch &&
           a->domain_valid == b->domain_valid && a->stream_valid == b->stream_valid &&
           a->temperature_valid == b->temperature_valid;
}

/** @brief 检查最近16个A/B身份，避免同一声音重复计数。 */
static bool IdentitySeen(const RangingTracker *self, const RangingObservation *observation)
{
    for (uint32_t i = 0U; i < self->recent_identity_count; ++i)
    {
        if ((self->recent_identity[i].a_session == observation->a_session &&
             self->recent_identity[i].a_sequence == observation->a_sequence) ||
            (self->recent_identity[i].b_session == observation->b_session &&
             self->recent_identity[i].b_sequence == observation->b_sequence))
        { return true; }
    }
    return false;
}

/** @brief 记录一次已消费身份；环形覆盖最旧项。 */
static void RememberIdentity(RangingTracker *self, const RangingObservation *observation)
{
    const uint32_t index = self->recent_identity_count < RANGING_TRACKER_IDENTITY_WINDOW ?
        self->recent_identity_count : self->recent_identity_head;
    self->recent_identity[index].a_session = observation->a_session;
    self->recent_identity[index].a_sequence = observation->a_sequence;
    self->recent_identity[index].b_session = observation->b_session;
    self->recent_identity[index].b_sequence = observation->b_sequence;
    if (self->recent_identity_count < RANGING_TRACKER_IDENTITY_WINDOW)
    { ++self->recent_identity_count; }
    else { self->recent_identity_head = (uint8_t)((self->recent_identity_head + 1U) %
        RANGING_TRACKER_IDENTITY_WINDOW); }
}

/** @brief 判断候选窗口能否加入当前观测；源间隔和声程差必须同时一致。 */
static bool CandidateCompatible(const RangingTracker *self,
                                const RangingObservation *observation,
                                uint64_t source_ns)
{
    if (self->candidate_count == 0U) { return false; }
    const uint64_t previous = self->candidate_source_ns[self->candidate_count - 1U];
    if (source_ns <= previous) { return false; }
    const uint32_t gap = SourceGapMs(source_ns, previous);
    if (gap < self->config.source_interval_min_ms ||
        gap > self->config.source_interval_max_ms) { return false; }
    const int64_t first = self->candidate_ns[0];
    const int64_t difference = observation->delta_ns > first ?
        observation->delta_ns - first : first - observation->delta_ns;
    return Magnitude64(difference) <= self->config.candidate_span_ns;
}

/** @brief 向候选窗口加入事件；不一致时保留最新观测作为新候选。 */
static void AddCandidate(RangingTracker *self, int64_t delta_ns, uint64_t source_ns)
{
    if (!CandidateCompatible(self, &(RangingObservation){.delta_ns=delta_ns}, source_ns))
    { self->candidate_count = 0U; }
    if (self->candidate_count < RANGING_TRACKER_CANDIDATE_WINDOW)
    {
        self->candidate_ns[self->candidate_count] = delta_ns;
        self->candidate_source_ns[self->candidate_count] = source_ns;
        ++self->candidate_count;
    }
}

/** @brief 返回默认冻结参数；调用方可在Init前覆盖单个字段。 */
RangingTrackerConfig RangingTracker_DefaultConfig(void)
{
    return (RangingTrackerConfig){
        .accepted_window = RANGING_TRACKER_ACCEPTED_WINDOW,
        .confirm_count = RANGING_TRACKER_CANDIDATE_WINDOW,
        .inlier_gate_ns = 45000U,
        .candidate_span_ns = 20000U,
        .candidate_max_gap_ms = 2500U,
        .source_interval_min_ms = 1500U,
        .source_interval_max_ms = 2500U,
        .fresh_age_ms = 2500U,
        .display_max_age_ms = 6500U,
        .display_delta_hysteresis_ns = 5800U,
        .legacy_quality_min = 50U,
        .sound_speed_mm_s = 343000U};
}

/** @brief 参数冻结合法性；不接受零窗口或零声速。 */
static bool ConfigValid(const RangingTrackerConfig *config)
{
    return config != NULL && config->accepted_window > 0U &&
           config->accepted_window <= RANGING_TRACKER_ACCEPTED_WINDOW &&
           config->confirm_count > 0U &&
           config->confirm_count <= RANGING_TRACKER_CANDIDATE_WINDOW &&
           config->source_interval_min_ms < config->source_interval_max_ms &&
           config->fresh_age_ms < config->display_max_age_ms &&
           config->sound_speed_mm_s > 0U;
}

void RangingTracker_Init(RangingTracker *self, const RangingTrackerConfig *config)
{
    if (self == NULL) { return; }
    memset(self, 0, sizeof *self);
    if (!ConfigValid(config))
    {
        self->state = RANGING_STATE_FAULT;
        self->invalidation = RANGING_INVALIDATE_FAULT;
        self->last_decision = RANGING_DECISION_FAULT;
        return;
    }
    self->config = *config;
    self->state = RANGING_STATE_ACQUIRING;
    self->last_decision = RANGING_DECISION_NONE;
    self->invalidation = RANGING_INVALIDATE_SOFT;
    self->display_evidence = RANGING_EVIDENCE_PROXY;
}

void RangingTracker_SetContext(RangingTracker *self,
                               const RangingTrackerContext *context,
                               uint32_t now_ms,
                               uint64_t now_common_ns)
{
    (void)now_ms;
    (void)now_common_ns;
    if (self == NULL) { return; }
    if (!ContextUsable(context))
    {
        self->context = context != NULL ? *context : (RangingTrackerContext){0};
        ClearSamples(self);
        self->state = RANGING_STATE_FAULT;
        self->invalidation = RANGING_INVALIDATE_FAULT;
        self->last_decision = RANGING_DECISION_FAULT;
        ++self->state_revision;
        return;
    }
    if (ContextEqual(&self->context, context))
    {
        if (self->state == RANGING_STATE_FAULT)
        {
            ClearSamples(self);
            self->state = RANGING_STATE_ACQUIRING;
            self->invalidation = RANGING_INVALIDATE_SOFT;
            self->last_decision = RANGING_DECISION_NONE;
            ++self->state_revision;
        }
        return;
    }
    ClearSamples(self);
    self->context = *context;
    self->state = RANGING_STATE_ACQUIRING;
    self->invalidation = RANGING_INVALIDATE_SOFT;
    self->last_decision = RANGING_DECISION_NONE;
    ++self->state_revision;
}

/** @brief 验证观测的域/身份/时间/质量；不修改状态。 */
static bool ObservationValid(const RangingTracker *self,
                             const RangingObservation *observation)
{
    if (observation == NULL || !observation->geometry_assumed_valid ||
        observation->time_domain == 0U || observation->a_session == 0U ||
        observation->b_session == 0U || observation->a_time_ns == 0U ||
        observation->b_time_ns == 0U ||
        observation->proxy_quality < self->config.legacy_quality_min ||
        observation->evidence_kind > RANGING_EVIDENCE_CODED)
    { return false; }
    const int64_t expected = (int64_t)observation->b_time_ns -
                             (int64_t)observation->a_time_ns;
    if (observation->delta_ns != expected) { return false; }
    if (observation->time_domain != self->context.time_domain ||
        observation->a_session != self->context.a_session ||
        observation->b_session != self->context.b_session)
    { return false; }
    return true;
}

void RangingTracker_SetSoundSpeed(RangingTracker *self, uint32_t sound_speed_mm_s)
{
    if(self==NULL || sound_speed_mm_s==0U)return;
    self->config.sound_speed_mm_s=sound_speed_mm_s;
}

RangingDecision RangingTracker_Push(RangingTracker *self,
                                    const RangingObservation *observation,
                                    uint32_t now_ms,
                                    uint64_t now_common_ns)
{
    (void)now_ms;
    if (self == NULL) { return RANGING_DECISION_REJECT; }
    if (self->state == RANGING_STATE_FAULT)
    { self->last_decision = RANGING_DECISION_FAULT; return self->last_decision; }
    if (!ObservationValid(self, observation))
    { self->last_decision = RANGING_DECISION_REJECT; return self->last_decision; }
    const uint64_t source_ns = observation->a_time_ns > observation->b_time_ns ?
        observation->a_time_ns : observation->b_time_ns;
    if (now_common_ns != 0U && source_ns > now_common_ns + 2000000ULL)
    { self->last_decision = RANGING_DECISION_REJECT; return self->last_decision; }
    if (self->has_source && source_ns <= self->last_source_ns)
    { self->last_decision = RANGING_DECISION_REJECT; return self->last_decision; }
    if (IdentitySeen(self, observation))
    { self->last_decision = RANGING_DECISION_REJECT; return self->last_decision; }

    RememberIdentity(self, observation);
    self->last_source_ns = source_ns;
    self->has_source = true;
    self->now_common_ns = now_common_ns;
    self->display_quality = self->display_quality == 0U ? observation->proxy_quality :
        (observation->proxy_quality < self->display_quality ?
         observation->proxy_quality : self->display_quality);
    self->display_evidence = observation->evidence_kind < self->display_evidence ?
        observation->evidence_kind : self->display_evidence;

    if (self->state == RANGING_STATE_ACQUIRING || self->accepted_count == 0U)
    {
        AddCandidate(self, observation->delta_ns, source_ns);
        if (self->candidate_count >= self->config.confirm_count)
        {
            self->accepted_count = 0U;
            for (uint32_t i = 0U; i < self->candidate_count; ++i)
            { self->accepted_ns[self->accepted_count++] = self->candidate_ns[i]; }
            self->candidate_count = 0U;
            RefreshDisplayValue(self);
            self->last_source_ns_for_display = source_ns;
            self->state = RANGING_STATE_TRACKING;
            self->last_decision = RANGING_DECISION_ACCEPT;
            self->invalidation = RANGING_INVALIDATE_SOFT;
            AllocateMeasurementId(self);
            ++self->state_revision;
            return self->last_decision;
        }
        self->last_decision = RANGING_DECISION_CANDIDATE;
        ++self->state_revision;
        return self->last_decision;
    }

    const int64_t median = AcceptedMedian(self);
    const uint64_t difference = Magnitude64(observation->delta_ns - median);
    if (difference <= self->config.inlier_gate_ns)
    {
        AddAccepted(self, observation->delta_ns);
        self->candidate_count = 0U;
        RefreshDisplayValue(self);
        self->last_source_ns_for_display = source_ns;
        self->state = RANGING_STATE_TRACKING;
        self->last_decision = RANGING_DECISION_ACCEPT;
        self->invalidation = RANGING_INVALIDATE_SOFT;
        AllocateMeasurementId(self);
        ++self->state_revision;
        return self->last_decision;
    }

    /* 离群：候选必须来自不同源事件且间隔/幅度一致，否则只保留最新一个。 */
    if (self->candidate_count > 0U)
    {
        const uint64_t previous = self->candidate_source_ns[self->candidate_count - 1U];
        const uint32_t gap = source_ns > previous ? SourceGapMs(source_ns, previous) : 0U;
        const int64_t first = self->candidate_ns[0];
        const uint64_t span_magnitude = Magnitude64(observation->delta_ns - first);
        if (source_ns <= previous ||
            gap < self->config.source_interval_min_ms ||
            gap > self->config.source_interval_max_ms ||
            gap > self->config.candidate_max_gap_ms ||
            span_magnitude > self->config.candidate_span_ns)
        { self->candidate_count = 0U; }
    }
    if (self->candidate_count < RANGING_TRACKER_CANDIDATE_WINDOW)
    {
        self->candidate_ns[self->candidate_count] = observation->delta_ns;
        self->candidate_source_ns[self->candidate_count] = source_ns;
        ++self->candidate_count;
    }
    if (self->candidate_count >= self->config.confirm_count)
    {
        self->accepted_count = 0U;
        for (uint32_t i = 0U; i < self->candidate_count; ++i)
        { self->accepted_ns[self->accepted_count++] = self->candidate_ns[i]; }
        self->candidate_count = 0U;
        RefreshDisplayValue(self);
        self->last_source_ns_for_display = source_ns;
        self->state = RANGING_STATE_TRACKING;
        self->last_decision = RANGING_DECISION_ACCEPT;
        self->invalidation = RANGING_INVALIDATE_SOFT;
        AllocateMeasurementId(self);
        ++self->state_revision;
        return self->last_decision;
    }
    self->state = self->candidate_count == 1U ? RANGING_STATE_HOLD :
        RANGING_STATE_REACQUIRE;
    self->last_decision = self->candidate_count == 1U ? RANGING_DECISION_HOLD :
        RANGING_DECISION_REACQUIRE;
    ++self->state_revision;
    return self->last_decision;
}

void RangingTracker_Tick(RangingTracker *self,
                         uint32_t now_ms,
                         uint64_t now_common_ns)
{
    (void)now_ms;
    if (self == NULL) { return; }
    self->now_common_ns = now_common_ns;
    if (self->accepted_count == 0U ||
        self->state == RANGING_STATE_FAULT ||
        self->state == RANGING_STATE_ACQUIRING) { return; }
    const uint32_t age = SourceAgeMs(now_common_ns, self->last_source_ns_for_display);
    if (age >= self->config.display_max_age_ms)
    {
        if (self->state != RANGING_STATE_STALE)
        { self->state = RANGING_STATE_STALE; ++self->state_revision; }
        self->last_decision = RANGING_DECISION_REJECT;
        return;
    }
    if (age >= self->config.fresh_age_ms && self->state == RANGING_STATE_TRACKING)
    {
        self->state = RANGING_STATE_HOLD;
        ++self->state_revision;
    }
}

void RangingTracker_Invalidate(RangingTracker *self,
                               RangingInvalidReason reason,
                               uint32_t now_ms)
{
    (void)now_ms;
    if (self == NULL) { return; }
    if (reason == RANGING_INVALIDATE_DOMAIN || reason == RANGING_INVALIDATE_STREAM ||
        reason == RANGING_INVALIDATE_FAULT)
    {
        ClearSamples(self);
        self->state = RANGING_STATE_FAULT;
        self->last_decision = RANGING_DECISION_FAULT;
        self->invalidation = reason;
        ++self->state_revision;
    }
    else
    {
        self->invalidation = reason;
        if (self->state == RANGING_STATE_TRACKING || self->state == RANGING_STATE_HOLD)
        { self->state = RANGING_STATE_HOLD; }
        ++self->state_revision;
    }
}

void RangingTracker_Get(const RangingTracker *self, RangingTrackerSnapshot *out)
{
    if (self == NULL || out == NULL) { return; }
    memset(out, 0, sizeof *out);
    out->state = self->state;
    out->has_value = (self->state == RANGING_STATE_TRACKING ||
                      self->state == RANGING_STATE_HOLD ||
                      self->state == RANGING_STATE_REACQUIRE) &&
                     self->accepted_count > 0U;
    out->fresh = self->state == RANGING_STATE_TRACKING && out->has_value;
    out->filtered_delta_ns = self->display_delta_ns;
    out->distance_mm = out->has_value ?
        DeltaToDistanceMm(self->display_delta_ns, self->config.sound_speed_mm_s) : 0;
    out->direction = self->display_delta_ns > 0 ? 1 :
        self->display_delta_ns < 0 ? 2 : 0;
    out->source_common_ns = self->last_source_ns_for_display;
    out->source_age_ms = out->source_common_ns != 0U ?
        SourceAgeMs(self->now_common_ns, out->source_common_ns) : 0U;
    out->fresh_deadline_ns = self->last_source_ns_for_display +
        (uint64_t)self->config.fresh_age_ms * RANGING_TRACKER_NS_PER_MS;
    out->display_deadline_ns = self->last_source_ns_for_display +
        (uint64_t)self->config.display_max_age_ms * RANGING_TRACKER_NS_PER_MS;
    out->measurement_id = self->measurement_id;
    out->state_revision = self->state_revision;
    out->quality = self->display_quality;
    out->evidence_kind = self->display_evidence;
    out->accepted_count = (uint8_t)self->accepted_count;
    out->candidate_count = (uint8_t)self->candidate_count;
    out->last_decision = self->last_decision;
    out->invalidation = self->invalidation;
}
