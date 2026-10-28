/**
 * @file        sound_pair.h
 * @brief       两板同一声事件的纯C配对、声程差与基线距离计算。
 * @details     只处理已带统一时间域标记的本地PTP模型时间事件；不读网络、不读PCM、不访问硬件。
 *              配对拒绝重复/歧义/类型或时间域不符/质量不足/超出配对窗口的事件。
 * @note        本模块不产生SYNC=1；输入时间必须由上游同步层证明在同一时间域。距离公式仅适用于
 *              声源位于两板基线延长线的课程场景，不能用于任意二维定位。
 */
#ifndef SBCG_SOUND_PAIR_H
#define SBCG_SOUND_PAIR_H

#include <stdbool.h>
#include <stdint.h>

#define SOUND_PAIR_QUEUE_CAPACITY 4U
#define SOUND_PAIR_RESULT_CAPACITY 4U
#define SOUND_PAIR_NS_PER_SEC UINT64_C(1000000000)
/** @brief 单个事件最多携带的TOA早/晚候选数。 */
#define SOUND_EVENT_MAX_TOA_CANDIDATES 4U

/** @brief 事件类型；未知类型不得配对。 */
typedef enum
{
    SOUND_EVENT_INVALID = 0,
    SOUND_EVENT_CHIRP,
    SOUND_EVENT_CLAP
} SoundEventType;

/** @brief 事件来自A或B板。 */
typedef enum
{
    SOUND_SIDE_A = 0,
    SOUND_SIDE_B = 1
} SoundEventSide;

/** @brief 声源方向：A侧/B侧；零声程差时为UNKNOWN。 */
typedef enum
{
    SOUND_DIRECTION_UNKNOWN = 0,
    SOUND_DIRECTION_A,
    SOUND_DIRECTION_B
} SoundDirection;

/** @brief 配对/距离结果状态；失败结果不带有效距离。 */
typedef enum
{
    SOUND_PAIR_OK = 0,
    SOUND_PAIR_BAD_ARGUMENT,
    SOUND_PAIR_INVALID_EVENT,
    SOUND_PAIR_QUALITY,
    SOUND_PAIR_TYPE,
    SOUND_PAIR_DOMAIN,
    SOUND_PAIR_DUPLICATE,
    SOUND_PAIR_AMBIGUOUS,
    SOUND_PAIR_TDOA_RANGE,
    SOUND_PAIR_DISTANCE_RANGE,
    SOUND_PAIR_CAPACITY
} SoundPairStatus;

/**
 * @brief 单个板端的同一声事件提交副本。
 * @note time_ns为本地PTP模型时间；time_domain由同步层提供，必须跨板可比。
 *       session/sequence为板本地诊断身份，不能替代time_domain。valid=false不得提交。
 */
typedef struct
{
    bool valid;
    SoundEventType type;
    uint8_t quality;              /**< 0..100，来自检测/识别质量。 */
    SoundEventSide side;
    uint32_t time_domain;         /**< 非零共同时间域标识；不是原始PTP epoch。 */
    uint32_t session;             /**< 板本地采集会话，仅诊断。 */
    uint32_t sequence;             /**< 板本地事件序号。 */
    uint64_t time_ns;              /**< 本地PTP模型时间，纳秒。 */
    uint8_t toa_candidate_count;   /**< 0..4，B侧TOA早/晚候选数；0表示旧单值。 */
    int32_t toa_candidate_q16[SOUND_EVENT_MAX_TOA_CANDIDATES];
                                   /**< 相对time_ns的候选偏移，1/65536帧。 */
} SoundEventRecord;

/** @brief 配对策略；时间窗口和量程由调用方按课程范围配置。 */
typedef struct
{
    uint64_t max_tdoa_ns;          /**< 允许的|tB-tA|上限，须覆盖量程传播时差。 */
    uint32_t max_distance_mm;      /**< 允许输出的最大基线距离。 */
    uint32_t sound_speed_mm_s;     /**< 当前声速，毫米/秒。 */
    uint8_t min_quality;           /**< 0..100最低质量。 */
    uint32_t pair_timeout_ms;      /**< 未配对事件保留时间。 */
    uint32_t nominal_distance_mm;  /**< 首齿初始参考；0表示禁用候选选择。 */
    uint32_t tooth_jump_mm;        /**< 一格跳变阈值，0表示禁用。 */
    uint8_t tooth_confirm;         /**< 连续确认次数，1..8。 */
} SoundPairConfig;

/** @brief 配对/距离输出；invalid结果用于显式报告拒绝原因；A/B会话和源时刻仅被动复制。 */
typedef struct
{
    bool valid;
    SoundPairStatus status;
    int64_t delta_ns;              /**< tB-tA，正表示B较晚、声源在A侧。 */
    uint32_t distance_mm;          /**< 仅valid时有效。 */
    SoundDirection direction;
    uint8_t quality;               /**< A/B质量较小值。 */
    uint32_t time_domain;
    uint32_t a_session;
    uint32_t a_sequence;
    uint32_t b_session;
    uint32_t b_sequence;
    uint64_t a_time_ns;
    uint64_t b_time_ns;
    uint32_t sound_speed_mm_s;
    uint8_t b_toa_candidate_count; /**< 诊断：B事件携带的TOA候选数。 */
    uint8_t b_toa_selected;        /**< 诊断：选中的候选序号，0表示未选。 */
    bool b_toa_held;               /**< true表示本次输出保持旧接受齿。 */
    int32_t b_toa_candidate_q16[SOUND_EVENT_MAX_TOA_CANDIDATES]; /**< 诊断副本。 */
} SoundPairResult;

/** @brief 队列和拒绝原因计数；均为单调饱和。 */
typedef struct
{
    uint32_t accepted, duplicate, expired, ambiguous, paired, rejected, result_dropped;
    uint8_t pending_a, pending_b, pending_results;
} SoundPairSnapshot;

/** @brief 前台单消费者管理器；固定数组，无动态内存。 */
typedef struct
{
    SoundPairConfig config;
    SoundEventRecord events[2][SOUND_PAIR_QUEUE_CAPACITY];
    uint32_t submitted_ms[2][SOUND_PAIR_QUEUE_CAPACITY];
    uint8_t count[2];
    SoundPairResult results[SOUND_PAIR_RESULT_CAPACITY];
    uint8_t result_count;
    uint32_t accepted_distance_mm;
    uint32_t pending_distance_mm;
    uint8_t accepted_valid;
    uint8_t pending_count;
    SoundPairSnapshot stats;
} SoundPairManager;

/** @brief 初始化管理器并复制配置；参数非法返回false并清空。 */
bool SoundPair_Init(SoundPairManager *manager, const SoundPairConfig *config);

/** @brief 提交一个事件副本；重复/非法/队列满返回false并计数，不覆盖已有项。 */
bool SoundPair_Submit(SoundPairManager *manager, const SoundEventRecord *event,
                      uint32_t now_ms);

/** @brief 过期未配对事件并生成所有可确定结果；不阻塞、无硬件访问。 */
void SoundPair_Process(SoundPairManager *manager, uint32_t now_ms);

/** @brief 取出一个结果；NULL不消费；无结果返回false。 */
bool SoundPair_Take(SoundPairManager *manager, SoundPairResult *result);

/** @brief 复制计数器；NULL不操作。 */
void SoundPair_GetSnapshot(const SoundPairManager *manager, SoundPairSnapshot *out);

/** @brief 纯函数：验证并计算一对已确定事件；不修改事件。 */
SoundPairStatus SoundPair_Compute(const SoundPairConfig *config,
                                  const SoundEventRecord *a,
                                  const SoundEventRecord *b,
                                  SoundPairResult *result);

#endif
