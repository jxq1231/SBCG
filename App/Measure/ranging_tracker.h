/**
 * @file        ranging_tracker.h
 * @brief       纯C、有符号TDOA的稳健跟踪器。
 * @details     以独立声事件为输入，维护已确认的5点中值、3点候选新簇、去重身份和
 *              保持/重获/过期状态；不访问HAL、网络、UI、PCM或动态内存。
 * @note        本模块只处理时间和状态，不把代理质量当成物理精度；A/B配对和
 *              温度/标定仍由调用方负责。
 */
#ifndef SBCG_RANGING_TRACKER_H
#define SBCG_RANGING_TRACKER_H

#include <stdbool.h>
#include <stdint.h>

#define RANGING_TRACKER_ACCEPTED_WINDOW 5U
#define RANGING_TRACKER_CANDIDATE_WINDOW 3U
#define RANGING_TRACKER_IDENTITY_WINDOW 16U
#define RANGING_TRACKER_NS_PER_MS 1000000U

/** @brief 观测证据来源；只用于保守质量，不代表物理真值。 */
typedef enum
{
    RANGING_EVIDENCE_PROXY = 0,
    RANGING_EVIDENCE_MATCHED = 1,
    RANGING_EVIDENCE_CODED = 2
} RangingEvidenceKind;

/** @brief 跟踪器状态；ACQUIRING没有确认数字，TRACKING有确认估计。 */
typedef enum
{
    RANGING_STATE_ACQUIRING = 0,
    RANGING_STATE_TRACKING,
    RANGING_STATE_HOLD,
    RANGING_STATE_REACQUIRE,
    RANGING_STATE_STALE,
    RANGING_STATE_FAULT
} RangingTrackerState;

/** @brief 一次Push/Invalidate的可观察决策。 */
typedef enum
{
    RANGING_DECISION_NONE = 0,
    RANGING_DECISION_CANDIDATE,
    RANGING_DECISION_ACCEPT,
    RANGING_DECISION_HOLD,
    RANGING_DECISION_REACQUIRE,
    RANGING_DECISION_REJECT,
    RANGING_DECISION_FAULT
} RangingDecision;

/** @brief 硬/软失效原因；硬原因清历史，软原因只更新状态。 */
typedef enum
{
    RANGING_INVALIDATE_SOFT = 0,
    RANGING_INVALIDATE_DOMAIN,
    RANGING_INVALIDATE_STREAM,
    RANGING_INVALIDATE_FAULT
} RangingInvalidReason;

/** @brief 一次完整的独立声事件；所有字段按值复制。 */
typedef struct
{
    uint32_t time_domain;
    uint32_t a_session;
    uint32_t a_sequence;
    uint32_t b_session;
    uint32_t b_sequence;
    uint64_t a_time_ns;
    uint64_t b_time_ns;
    int64_t delta_ns;             /**< 必须等于b_time_ns-a_time_ns。 */
    uint8_t proxy_quality;        /**< 0..100旧代理质量。 */
    uint8_t evidence_kind;        /**< RangingEvidenceKind。 */
    bool geometry_assumed_valid;  /**< 调用方已确认基线延长线几何。 */
} RangingObservation;

/** @brief 跟踪器冻结参数；由调用方一次初始化，不在运行时自适应。 */
typedef struct
{
    uint32_t accepted_window;
    uint32_t confirm_count;
    uint32_t inlier_gate_ns;
    uint32_t candidate_span_ns;
    uint32_t candidate_max_gap_ms;
    uint32_t source_interval_min_ms;
    uint32_t source_interval_max_ms;
    uint32_t fresh_age_ms;
    uint32_t display_max_age_ms;
    uint32_t display_delta_hysteresis_ns;
    uint8_t legacy_quality_min;
    uint32_t sound_speed_mm_s;
} RangingTrackerConfig;

/** @brief 由前台健康协调者设置的上下文；相同上下文重复设置不清历史。 */
typedef struct
{
    uint32_t time_domain;
    uint32_t a_session;
    uint32_t b_session;
    uint32_t a_clock_epoch;
    bool domain_valid;
    bool stream_valid;
    bool temperature_valid;
} RangingTrackerContext;

/** @brief 只读展示快照；HAS_VALUE与state/fresh共同解释。 */
typedef struct
{
    RangingTrackerState state;
    bool has_value;
    bool fresh;
    int64_t filtered_delta_ns;
    int32_t distance_mm;
    int32_t direction;             /**< 1=A侧，2=B侧，0=未知。 */
    uint64_t source_common_ns;
    uint32_t source_age_ms;
    uint64_t fresh_deadline_ns;
    uint64_t display_deadline_ns;
    uint32_t measurement_id;
    uint32_t state_revision;
    uint8_t quality;
    uint8_t evidence_kind;
    uint8_t accepted_count;
    uint8_t candidate_count;
    RangingDecision last_decision;
    RangingInvalidReason invalidation;
} RangingTrackerSnapshot;

/** @brief 内部固定数组状态；调用方持有实例，禁止并发写。 */
typedef struct
{
    RangingTrackerConfig config;
    RangingTrackerContext context;
    RangingTrackerState state;
    RangingDecision last_decision;
    RangingInvalidReason invalidation;
    int64_t accepted_ns[RANGING_TRACKER_ACCEPTED_WINDOW];
    uint32_t accepted_count;
    int64_t candidate_ns[RANGING_TRACKER_CANDIDATE_WINDOW];
    uint64_t candidate_source_ns[RANGING_TRACKER_CANDIDATE_WINDOW];
    uint32_t candidate_count;
    uint64_t last_source_ns;
    bool has_source;
    uint64_t last_source_ns_for_display;
    uint64_t now_common_ns;
    int64_t display_delta_ns;
    uint8_t display_quality;
    uint8_t display_evidence;
    uint32_t measurement_id;
    uint32_t state_revision;
    uint32_t next_measurement_id;
    struct
    {
        uint32_t a_session;
        uint32_t a_sequence;
        uint32_t b_session;
        uint32_t b_sequence;
    } recent_identity[RANGING_TRACKER_IDENTITY_WINDOW];
    uint8_t recent_identity_count;
    uint8_t recent_identity_head;
} RangingTracker;

/** @brief 返回冻结的起始配置；测试可覆盖单个字段但不能依赖运行时调参。 */
RangingTrackerConfig RangingTracker_DefaultConfig(void);

/** @brief 初始化实例；config非法或NULL时进入FAULT且不保留历史。 */
void RangingTracker_Init(RangingTracker *self, const RangingTrackerConfig *config);

/** @brief 设置/确认上下文；硬失效清历史并进入FAULT，同上下文重复设置无副作用。 */
void RangingTracker_SetContext(RangingTracker *self,
                               const RangingTrackerContext *context,
                               uint32_t now_ms,
                               uint64_t now_common_ns);

/** @brief 合法温度更新声速；不刷新源时间、ID或deadline，只影响后续距离换算。 */
void RangingTracker_SetSoundSpeed(RangingTracker *self, uint32_t sound_speed_mm_s);

/** @brief 提交一个独立声事件，返回本次决策；拒绝不刷新源时间/ID/历史。 */
RangingDecision RangingTracker_Push(RangingTracker *self,
                                    const RangingObservation *observation,
                                    uint32_t now_ms,
                                    uint64_t now_common_ns);

/** @brief 只推进新鲜度/保持/过期；不产生新测量ID，不读硬件。 */
void RangingTracker_Tick(RangingTracker *self,
                         uint32_t now_ms,
                         uint64_t now_common_ns);

/** @brief 软/硬失效；硬原因清历史，软原因只更新状态。 */
void RangingTracker_Invalidate(RangingTracker *self,
                               RangingInvalidReason reason,
                               uint32_t now_ms);

/** @brief 复制快照；NULL不操作。 */
void RangingTracker_Get(const RangingTracker *self, RangingTrackerSnapshot *out);

#endif
