/**
 * @file        ranging_tracker.c
 * @brief       纯C有符号TDOA跟踪器实现。
 * @details     以独立声事件为单位维护确认中值、候选新簇、去重身份和新鲜度；
 *              候选全跨度和两种时基均受限，三次同向一致新簇才整体换窗。
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

/**
 * @brief 求两个有符号差值的绝对间距，不执行可能溢出的有符号减法。
 * @param a 第一个时间差，单位ns。
 * @param b 第二个时间差，单位ns。
 * @return 非负间距；全int64范围的间距可由uint64表示。
 */
static uint64_t DeltaGapNs(int64_t a, int64_t b)
{
    return a >= b ? (uint64_t)a - (uint64_t)b : (uint64_t)b - (uint64_t)a;
}

/**
 * @brief 判断两个时间差方向是否相同；零仅与零同向。
 * @param a 第一个有符号时间差，单位ns。
 * @param b 第二个有符号时间差，单位ns。
 * @return 同为正、同为负或同为零时返回true。
 */
static bool SameDirection(int64_t a, int64_t b)
{
    return (a > 0 && b > 0) || (a < 0 && b < 0) || (a == 0 && b == 0);
}

/** @brief 返回从源时刻到当前共同时间的毫秒年龄；now早于源时按0处理。 */
static uint32_t SourceAgeMs(uint64_t now_common_ns, uint64_t source_ns)
{
    if (now_common_ns <= source_ns) { return 0U; }
    const uint64_t delta = now_common_ns - source_ns;
    const uint64_t age = delta / RANGING_TRACKER_NS_PER_MS;
    return age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
}

/**
 * @brief 检查乘加范围并将有符号时间差换算成受量程保护的绝对距离。
 * @param delta_ns B减A时间差，单位ns。
 * @param sound_speed_mm_s 非零声速，单位mm/s。
 * @param distance_mm 非NULL输出；失败时不写入，调用方拥有存储。
 * @return 乘加安全且四舍五入距离不超过10000mm时返回true。
 */
static bool DeltaToDistanceMm(int64_t delta_ns, uint32_t sound_speed_mm_s,
                              int32_t *distance_mm)
{
    const uint64_t magnitude = Magnitude64(delta_ns);
    if (sound_speed_mm_s == 0U ||
        magnitude > (UINT64_MAX - UINT64_C(500000000)) / sound_speed_mm_s)
    { return false; }
    const uint64_t numerator = magnitude * sound_speed_mm_s + UINT64_C(500000000);
    const uint64_t distance = numerator / UINT64_C(1000000000);
    if (distance > RANGING_TRACKER_MAX_DISTANCE_MM) { return false; }
    *distance_mm = (int32_t)distance;
    return true;
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

/**
 * @brief 加入确认样本及其证据元数据，满窗时一起淘汰最旧支持点。
 * @param self 非NULL，调用方拥有且仅前台写入的跟踪器。
 * @param delta_ns 已通过准入的有符号时间差，单位ns。
 * @param quality 该事件代理质量，范围0..100。
 * @param evidence 该事件的RangingEvidenceKind。
 * @return 无返回值；更新固定长度接受窗口。
 */
static void AddAccepted(RangingTracker *self, int64_t delta_ns,
                         uint8_t quality, uint8_t evidence)
{
    if (self->accepted_count == self->config.accepted_window)
    {
        for (uint32_t i = 1U; i < self->accepted_count; ++i)
        {
            self->accepted_ns[i - 1U] = self->accepted_ns[i];
            self->accepted_quality[i - 1U] = self->accepted_quality[i];
            self->accepted_evidence[i - 1U] = self->accepted_evidence[i];
        }
        --self->accepted_count;
    }
    const uint32_t index = self->accepted_count++;
    self->accepted_ns[index] = delta_ns;
    self->accepted_quality[index] = quality;
    self->accepted_evidence[index] = evidence;
}

/**
 * @brief 仅从当前支持窗口更新保守质量和中值，同向小变化应用展示滞回。
 * @param self 非NULL且接受窗口非空，由前台独占写入。
 * @return 无返回值；候选及被拒绝事件不会参与质量或证据计算。
 */
static void RefreshDisplayValue(RangingTracker *self)
{
    const int64_t median = AcceptedMedian(self);
    self->display_quality = self->accepted_quality[0];
    self->display_evidence = self->accepted_evidence[0];
    for (uint32_t i = 1U; i < self->accepted_count; ++i)
    {
        if (self->accepted_quality[i] < self->display_quality)
        { self->display_quality = self->accepted_quality[i]; }
        if (self->accepted_evidence[i] < self->display_evidence)
        { self->display_evidence = self->accepted_evidence[i]; }
    }
    if (SameDirection(median, self->display_delta_ns) &&
        DeltaGapNs(median, self->display_delta_ns) < self->config.display_delta_hysteresis_ns)
    { return; }
    self->display_delta_ns = median;
}

/** @brief 新确认结果分配递增ID；ID耗尽后停止增加而不回绕。 */
static void AllocateMeasurementId(RangingTracker *self)
{
    if (self->next_measurement_id != UINT32_MAX) { ++self->next_measurement_id; }
    self->measurement_id = self->next_measurement_id;
}

/**
 * @brief 清除确认、候选及展示；保留消费身份、源高水位、共同读时和ID代际。
 * @param self 非NULL，前台独占写入的实例。
 * @return 无返回值；同上下文硬恢复不能因此重用旧声音。
 */
static void ClearSamples(RangingTracker *self)
{
    memset(self->accepted_ns, 0, sizeof self->accepted_ns);
    memset(self->candidate_ns, 0, sizeof self->candidate_ns);
    memset(self->candidate_source_ns, 0, sizeof self->candidate_source_ns);
    self->accepted_count = 0U;
    self->candidate_count = 0U;
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

/**
 * @brief 检查各侧单调序号和最近身份，拒绝重配、回退及旧声音。
 * @param self 非NULL，只读实例；消费高水位跨同上下文硬失效保留。
 * @param observation 非NULL，当前上下文的输入事件，只在调用内借用。
 * @return 任一侧序号不高于已消费高水位或最近身份重复时返回true。
 */
static bool IdentitySeen(const RangingTracker *self, const RangingObservation *observation)
{
    if (observation->a_sequence <= self->consumed.a_sequence ||
        observation->b_sequence <= self->consumed.b_sequence) { return true; }
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

/**
 * @brief 记录已通过准入事件的两侧高水位和最近16个身份。
 * @param self 非NULL，前台独占实例。
 * @param observation 非NULL，只在调用内借用的已准入事件。
 * @return 无返回值；候选虽未确认，也已消费，不可重复累计。
 */
static void RememberIdentity(RangingTracker *self, const RangingObservation *observation)
{
    self->consumed.a_sequence = observation->a_sequence;
    self->consumed.b_sequence = observation->b_sequence;
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

/**
 * @brief 判断保留指定后缀后，新候选是否同向且全跨度不超过限制。
 * @param self 非NULL，只读候选窗口。
 * @param delta_ns 新候选的有符号时间差，单位ns。
 * @param first 后缀起始下标，不大于candidate_count。
 * @return 后缀和新点同向且最大减最小不超过配置时返回true。
 */
static bool CandidateSpanFits(const RangingTracker *self, int64_t delta_ns,
                               uint32_t first)
{
    int64_t min_ns = delta_ns;
    int64_t max_ns = delta_ns;
    for (uint32_t i = first; i < self->candidate_count; ++i)
    {
        const int64_t member_ns = self->candidate_ns[i];
        if (!SameDirection(member_ns, delta_ns)) { return false; }
        if (member_ns < min_ns) { min_ns = member_ns; }
        if (member_ns > max_ns) { max_ns = member_ns; }
    }
    return DeltaGapNs(max_ns, min_ns) <= self->config.candidate_span_ns;
}

/**
 * @brief 加入候选；节奏/接收间隔失败重建，跨度失败保留最长一致后缀。
 * @param self 非NULL，由前台独占写入的实例。
 * @param observation 非NULL，只在调用内借用的已准入事件。
 * @param source_ns 事件两侧较晚的共同源时刻，单位ns。
 * @param now_ms 前台接收毫秒计数；无符号减法支持一次32位回绕。
 * @return 无返回值；只更新固定候选数组及接收计数，不改变展示质量。
 */
static void AddCandidate(RangingTracker *self, const RangingObservation *observation,
                          uint64_t source_ns, uint32_t now_ms)
{
    if (self->candidate_count > 0U)
    {
        const uint64_t previous = self->candidate_source_ns[self->candidate_count - 1U];
        const uint64_t gap_ns = source_ns > previous ? source_ns - previous : 0U;
        if (source_ns <= previous ||
            gap_ns < (uint64_t)self->config.source_interval_min_ms * RANGING_TRACKER_NS_PER_MS ||
            gap_ns > (uint64_t)self->config.source_interval_max_ms * RANGING_TRACKER_NS_PER_MS ||
            (uint32_t)(now_ms - self->candidate_last_recv_ms) > self->config.candidate_max_gap_ms)
        { self->candidate_count = 0U; }
    }
    uint32_t first = 0U;
    while (first < self->candidate_count &&
           !CandidateSpanFits(self, observation->delta_ns, first)) { ++first; }
    for (uint32_t i = first; i < self->candidate_count; ++i)
    {
        self->candidate_ns[i - first] = self->candidate_ns[i];
        self->candidate_source_ns[i - first] = self->candidate_source_ns[i];
        self->candidate_quality[i - first] = self->candidate_quality[i];
        self->candidate_evidence[i - first] = self->candidate_evidence[i];
    }
    self->candidate_count -= first;
    if (self->candidate_count < RANGING_TRACKER_CANDIDATE_WINDOW)
    {
        self->candidate_ns[self->candidate_count] = observation->delta_ns;
        self->candidate_source_ns[self->candidate_count] = source_ns;
        self->candidate_quality[self->candidate_count] = observation->proxy_quality;
        self->candidate_evidence[self->candidate_count] = observation->evidence_kind;
        ++self->candidate_count;
    }
    self->candidate_last_recv_ms = now_ms;
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

/**
 * @brief 验证冻结配置的窗口、时间、质量和声速约束。
 * @param config 初始化期间借用的配置，允许NULL并判非法。
 * @return 所有固定数组边界及时间顺序有效时返回true。
 */
static bool ConfigValid(const RangingTrackerConfig *config)
{
    return config != NULL && config->accepted_window > 0U &&
           config->accepted_window <= RANGING_TRACKER_ACCEPTED_WINDOW &&
           config->confirm_count > 0U &&
           config->confirm_count <= RANGING_TRACKER_CANDIDATE_WINDOW &&
           config->confirm_count <= config->accepted_window &&
           config->candidate_max_gap_ms > 0U &&
           config->candidate_max_gap_ms < UINT32_C(0x80000000) &&
           config->source_interval_min_ms < config->source_interval_max_ms &&
           config->fresh_age_ms < config->display_max_age_ms &&
           config->legacy_quality_min <= 100U &&
           config->sound_speed_mm_s > 0U;
}

/**
 * @brief 初始化纯CPU实例及配置；只应在实例生命周期开始时调用。
 * @param self 调用方持有的输出实例；NULL时不操作。
 * @param config 仅在调用内借用并按值复制；NULL或非法配置进入FAULT。
 * @return 无返回值；初始化清除包括身份高水位在内的全部状态。
 */
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

/**
 * @brief 检查实际共同读时；失败或倒退时立即撤值，保留身份与时钟高水位。
 * @param self 非NULL，由前台独占写入的实例。
 * @param now_ms 当前前台毫秒计数，仅传给失效接口。
 * @param now_common_ns 非零实际共同读时，单位ns；0表示读时失败。
 * @return 时间有效且不早于上次共同读时时返回true。
 */
static bool ObserveClock(RangingTracker *self, uint32_t now_ms, uint64_t now_common_ns)
{
    if (now_common_ns == 0U || now_common_ns < self->now_common_ns)
    {
        RangingTracker_Invalidate(self, RANGING_INVALIDATE_FAULT, now_ms);
        return false;
    }
    self->now_common_ns = now_common_ns;
    return true;
}

/**
 * @brief 设置前台确认的上下文；同侧同会话消费高水位跨硬恢复保留。
 * @param self 调用方持有的实例；NULL时不操作。
 * @param context 调用期间借用并复制；NULL或不健康上下文撤值进入FAULT。
 * @param now_ms 当前前台毫秒计数，单位ms。
 * @param now_common_ns 实际共同读时，单位ns；新域/时钟代际允许重新起时。
 * @return 无返回值；新域重置双方序号，新单侧session只重置该侧序号。
 * @note 包不得自行改变上下文；FAULT仅由健康SetContext显式恢复。
 */
void RangingTracker_SetContext(RangingTracker *self,
                               const RangingTrackerContext *context,
                               uint32_t now_ms,
                               uint64_t now_common_ns)
{
    if (self == NULL) { return; }
    if (!ConfigValid(&self->config) || !ContextUsable(context))
    {
        self->context = context != NULL ? *context : (RangingTrackerContext){0};
        ClearSamples(self);
        self->state = RANGING_STATE_FAULT;
        self->invalidation = RANGING_INVALIDATE_FAULT;
        self->last_decision = RANGING_DECISION_FAULT;
        ++self->state_revision;
        return;
    }
    if (self->context.time_domain != context->time_domain ||
        self->context.a_clock_epoch != context->a_clock_epoch)
    {
        self->now_common_ns = 0U;
        self->last_source_ns = 0U;
        self->has_source = false;
    }
    if (!ObserveClock(self, now_ms, now_common_ns)) { return; }
    const bool new_domain = self->consumed.time_domain != context->time_domain;
    const bool new_a = new_domain || self->consumed.a_session != context->a_session;
    const bool new_b = new_domain || self->consumed.b_session != context->b_session;
    if (new_a) { self->consumed.a_sequence = 0U; }
    if (new_b) { self->consumed.b_sequence = 0U; }
    if (new_a || new_b)
    {
        self->recent_identity_count = 0U;
        self->recent_identity_head = 0U;
    }
    self->consumed.time_domain = context->time_domain;
    self->consumed.a_session = context->a_session;
    self->consumed.b_session = context->b_session;
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

/**
 * @brief 验证域/身份、受检有符号时间差、质量、量程与截止加法范围。
 * @param self 非NULL，只读配置及当前上下文。
 * @param observation 调用内借用的输入；NULL直接拒绝。
 * @return 字段、算术及10000mm量程均有效时返回true，不修改状态。
 */
static bool ObservationValid(const RangingTracker *self,
                             const RangingObservation *observation)
{
    if (observation == NULL || !observation->geometry_assumed_valid ||
        observation->time_domain == 0U || observation->a_session == 0U ||
        observation->b_session == 0U || observation->a_time_ns == 0U ||
        observation->a_sequence == 0U || observation->b_sequence == 0U ||
        observation->b_time_ns == 0U ||
        observation->proxy_quality > 100U ||
        observation->proxy_quality < self->config.legacy_quality_min ||
        observation->evidence_kind > RANGING_EVIDENCE_CODED)
    { return false; }
    const bool positive = observation->b_time_ns >= observation->a_time_ns;
    const uint64_t magnitude = positive ? observation->b_time_ns - observation->a_time_ns :
        observation->a_time_ns - observation->b_time_ns;
    if (magnitude > INT64_MAX) { return false; }
    const int64_t expected = positive ? (int64_t)magnitude : -(int64_t)magnitude;
    if (observation->delta_ns != expected) { return false; }
    int32_t distance_mm;
    if (!DeltaToDistanceMm(expected, self->config.sound_speed_mm_s, &distance_mm))
    { return false; }
    const uint64_t source_ns = positive ? observation->b_time_ns : observation->a_time_ns;
    if (source_ns > UINT64_MAX - (uint64_t)self->config.display_max_age_ms *
        RANGING_TRACKER_NS_PER_MS) { return false; }
    if (observation->time_domain != self->context.time_domain ||
        observation->a_session != self->context.a_session ||
        observation->b_session != self->context.b_session)
    { return false; }
    return true;
}

/**
 * @brief 更新后续距离换算声速，不刷新源时间、身份和期限。
 * @param self 调用方持有的实例；NULL时不操作。
 * @param sound_speed_mm_s 非零声速，单位mm/s；0保持原配置。
 * @return 无返回值；确认窗口越量程时FAULT，候选越量程时只清候选。
 */
void RangingTracker_SetSoundSpeed(RangingTracker *self, uint32_t sound_speed_mm_s)
{
    if(self==NULL || sound_speed_mm_s==0U)return;
    for (uint32_t i = 0U; i < self->accepted_count; ++i)
    {
        int32_t distance_mm;
        if (!DeltaToDistanceMm(self->accepted_ns[i], sound_speed_mm_s, &distance_mm))
        {
            RangingTracker_Invalidate(self, RANGING_INVALIDATE_FAULT, 0U);
            break;
        }
    }
    for (uint32_t i = 0U; i < self->candidate_count; ++i)
    {
        int32_t distance_mm;
        if (!DeltaToDistanceMm(self->candidate_ns[i], sound_speed_mm_s, &distance_mm))
        {
            self->candidate_count = 0U;
            break;
        }
    }
    self->config.sound_speed_mm_s=sound_speed_mm_s;
}

/**
 * @brief 消费一个独立声事件；确认候选或更新原簇，拒绝不改变支持窗口。
 * @param self 调用方持有且前台独占的实例；NULL返回REJECT。
 * @param observation 调用内借用、按值复制的输入；NULL返回REJECT。
 * @param now_ms 实际前台接收计数，单位ms；用于候选最大间隔及回绕。
 * @param now_common_ns 实际共同读时，单位ns；0或倒退立即FAULT。
 * @return 返回本次准入/确认/保持/失效决策；只有独立接受事件分配ID。
 */
RangingDecision RangingTracker_Push(RangingTracker *self,
                                    const RangingObservation *observation,
                                    uint32_t now_ms,
                                    uint64_t now_common_ns)
{
    if (self == NULL) { return RANGING_DECISION_REJECT; }
    RangingTracker_Tick(self, now_ms, now_common_ns);
    if (self->state == RANGING_STATE_FAULT)
    { self->last_decision = RANGING_DECISION_FAULT; return self->last_decision; }
    if (!ObservationValid(self, observation))
    { self->last_decision = RANGING_DECISION_REJECT; return self->last_decision; }
    const uint64_t source_ns = observation->a_time_ns > observation->b_time_ns ?
        observation->a_time_ns : observation->b_time_ns;
    if (source_ns > now_common_ns && source_ns - now_common_ns > UINT64_C(2000000))
    { self->last_decision = RANGING_DECISION_REJECT; return self->last_decision; }
    if (self->has_source && source_ns <= self->last_source_ns)
    { self->last_decision = RANGING_DECISION_REJECT; return self->last_decision; }
    if (IdentitySeen(self, observation))
    { self->last_decision = RANGING_DECISION_REJECT; return self->last_decision; }

    RememberIdentity(self, observation);
    self->last_source_ns = source_ns;
    self->has_source = true;
    if (self->state == RANGING_STATE_ACQUIRING || self->state == RANGING_STATE_STALE ||
        self->accepted_count == 0U)
    {
        AddCandidate(self, observation, source_ns, now_ms);
        if (self->candidate_count >= self->config.confirm_count)
        {
            self->accepted_count = 0U;
            for (uint32_t i = 0U; i < self->candidate_count; ++i)
            {
                AddAccepted(self, self->candidate_ns[i], self->candidate_quality[i],
                            self->candidate_evidence[i]);
            }
            self->candidate_count = 0U;
            RefreshDisplayValue(self);
            self->last_source_ns_for_display = source_ns;
            self->state = RANGING_STATE_TRACKING;
            self->last_decision = RANGING_DECISION_ACCEPT;
            self->invalidation = RANGING_INVALIDATE_SOFT;
            AllocateMeasurementId(self);
            ++self->state_revision;
            RangingTracker_Tick(self, now_ms, now_common_ns);
            return self->last_decision;
        }
        self->last_decision = RANGING_DECISION_CANDIDATE;
        ++self->state_revision;
        return self->last_decision;
    }

    const int64_t median = AcceptedMedian(self);
    const uint64_t difference = DeltaGapNs(observation->delta_ns, median);
    if (SameDirection(observation->delta_ns, median) &&
        difference <= self->config.inlier_gate_ns)
    {
        AddAccepted(self, observation->delta_ns, observation->proxy_quality,
                    observation->evidence_kind);
        self->candidate_count = 0U;
        RefreshDisplayValue(self);
        self->last_source_ns_for_display = source_ns;
        self->state = RANGING_STATE_TRACKING;
        self->last_decision = RANGING_DECISION_ACCEPT;
        self->invalidation = RANGING_INVALIDATE_SOFT;
        AllocateMeasurementId(self);
        ++self->state_revision;
        RangingTracker_Tick(self, now_ms, now_common_ns);
        return self->last_decision;
    }

    /* 离群：候选必须来自不同源事件且间隔/幅度一致，否则只保留最新一个。 */
    AddCandidate(self, observation, source_ns, now_ms);
    if (self->candidate_count >= self->config.confirm_count)
    {
        self->accepted_count = 0U;
        for (uint32_t i = 0U; i < self->candidate_count; ++i)
        {
            AddAccepted(self, self->candidate_ns[i], self->candidate_quality[i],
                        self->candidate_evidence[i]);
        }
        self->candidate_count = 0U;
        RefreshDisplayValue(self);
        self->last_source_ns_for_display = source_ns;
        self->state = RANGING_STATE_TRACKING;
        self->last_decision = RANGING_DECISION_ACCEPT;
        self->invalidation = RANGING_INVALIDATE_SOFT;
        AllocateMeasurementId(self);
        ++self->state_revision;
        RangingTracker_Tick(self, now_ms, now_common_ns);
        return self->last_decision;
    }
    self->state = self->candidate_count == 1U ? RANGING_STATE_HOLD :
        RANGING_STATE_REACQUIRE;
    self->last_decision = self->candidate_count == 1U ? RANGING_DECISION_HOLD :
        RANGING_DECISION_REACQUIRE;
    ++self->state_revision;
    RangingTracker_Tick(self, now_ms, now_common_ns);
    return self->last_decision;
}

/**
 * @brief 用实际共同时间推进新鲜度、硬截止及读时失效。
 * @param self 调用方持有、前台独占的实例；NULL不操作。
 * @param now_ms 当前前台毫秒计数，单位ms；不作为源年龄。
 * @param now_common_ns 实际共同读时，单位ns；0或倒退撤值进入FAULT。
 * @return 无返回值；不产生新测量ID，不延长任何源截止时刻。
 */
void RangingTracker_Tick(RangingTracker *self,
                         uint32_t now_ms,
                         uint64_t now_common_ns)
{
    if (self == NULL) { return; }
    if (!ObserveClock(self, now_ms, now_common_ns)) { return; }
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

/**
 * @brief 软失效保持旧数字资格，硬失效清样本并保留身份与时间高水位。
 * @param self 调用方持有的实例；NULL不操作。
 * @param reason 失效原因；DOMAIN/STREAM/FAULT均立即撤值。
 * @param now_ms 当前前台毫秒计数，保留接口兼容，不用作源年龄。
 * @return 无返回值；硬失效后需健康SetContext显式恢复。
 */
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

/**
 * @brief 从支持窗口和源时刻复制快照，换算失败不授予数字资格。
 * @param self 只读实例，调用期间借用；NULL时不操作。
 * @param out 调用方拥有的输出存储；NULL时不操作，否则完整覆盖。
 * @return 无返回值；无源时截止字段为0，所有截止加法均检查范围。
 */
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
    if (out->has_value && !DeltaToDistanceMm(self->display_delta_ns,
        self->config.sound_speed_mm_s, &out->distance_mm))
    {
        out->has_value = false;
        out->fresh = false;
    }
    out->direction = self->display_delta_ns > 0 ? 1 :
        self->display_delta_ns < 0 ? 2 : 0;
    out->source_common_ns = self->last_source_ns_for_display;
    out->source_age_ms = out->source_common_ns != 0U ?
        SourceAgeMs(self->now_common_ns, out->source_common_ns) : 0U;
    const uint64_t fresh_ns = (uint64_t)self->config.fresh_age_ms * RANGING_TRACKER_NS_PER_MS;
    const uint64_t display_ns = (uint64_t)self->config.display_max_age_ms * RANGING_TRACKER_NS_PER_MS;
    if (out->source_common_ns != 0U && out->source_common_ns <= UINT64_MAX - display_ns)
    {
        out->fresh_deadline_ns = out->source_common_ns + fresh_ns;
        out->display_deadline_ns = out->source_common_ns + display_ns;
    }
    out->measurement_id = self->measurement_id;
    out->state_revision = self->state_revision;
    out->quality = self->display_quality;
    out->evidence_kind = self->display_evidence;
    out->accepted_count = (uint8_t)self->accepted_count;
    out->candidate_count = (uint8_t)self->candidate_count;
    out->last_decision = self->last_decision;
    out->invalidation = self->invalidation;
}
