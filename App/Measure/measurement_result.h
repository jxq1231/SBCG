/**
 * @file        measurement_result.h
 * @brief       两板测距的唯一权威结果缓存：测量ID、距离、方向、质量、温度版本。
 * @details     只接受已配对的SoundPairResult并重新用当前声速计算距离；无温度/超龄/越界时结果为无效。
 * @note        前台单写单读；不访问PCM/HAL/PTP，不生成网络报文，不把无效结果补零。
 */
#ifndef SBCG_MEASUREMENT_RESULT_H
#define SBCG_MEASUREMENT_RESULT_H

#include "Measure/sound_pair.h"
#include "Measure/ranging_tracker.h"
#include "Temperature/temperature_service.h"
#include <stdbool.h>
#include <stdint.h>

#define MEASUREMENT_MAX_AGE_MS 2000U
#define MEASUREMENT_MAX_DISTANCE_MM 10000U

/** @brief 结果状态；只有VALID在有效期内可显示数值。 */
typedef enum
{
    MEASUREMENT_NONE = 0,
    MEASUREMENT_VALID,
    MEASUREMENT_REJECTED
} MeasurementStatus;

/** @brief 只读测量结果副本；distance仅在status=VALID且未超龄时有效。 */
typedef struct
{
    MeasurementStatus status;
    bool valid;
    uint32_t id;
    uint32_t captured_ms;
    int32_t distance_mm;
    SoundDirection direction;
    uint8_t quality;
    uint32_t sound_speed_mm_s;
    uint32_t temperature_version;
    bool temperature_valid;
    uint32_t time_domain;
    uint16_t calibration_version;
    int16_t calibration_offset_mm;
} MeasurementResultSnapshot;

/** @brief 结果观测决策；只用于诊断，不改变发布规则。 */
typedef enum
{
    MEASUREMENT_DECISION_NONE = 0,
    MEASUREMENT_DECISION_ACCEPT,
    MEASUREMENT_DECISION_REJECT_INVALID,
    MEASUREMENT_DECISION_REJECT_TEMPERATURE,
    MEASUREMENT_DECISION_REJECT_RANGE,
    MEASUREMENT_DECISION_REJECT_OUTLIER,
    MEASUREMENT_DECISION_INVALIDATE,
    MEASUREMENT_DECISION_REMOTE_ACCEPT
} MeasurementDecision;

/**
 * @brief 最近一次测量决策的只读诊断副本。
 * @details schema=1：raw/pair/温补/标定/稳定器成员分别记录；不能把这些字段当成独立硬件精度。
 * @note 由MeasurementResult_PublishPair/Invalidate/PublishRemote更新，前台可随时复制。
 */
typedef struct
{
    uint8_t schema;
    uint32_t sequence;
    MeasurementDecision decision;
    MeasurementStatus status;
    uint32_t now_ms;
    uint32_t measurement_id;
    uint32_t pair_time_domain;
    uint32_t a_session;
    uint32_t a_sequence;
    uint32_t b_session;
    uint32_t b_sequence;
    uint64_t a_time_ns;
    uint64_t b_time_ns;
    uint64_t source_common_ns;
    int64_t raw_delta_ns;
    uint32_t pair_distance_mm;
    uint32_t temperature_distance_mm;
    int32_t corrected_mm;
    int32_t published_mm;
    uint8_t stable_count;
    int32_t stable_mm[5];
    uint32_t quality;
    uint32_t temperature_version;
    uint32_t sound_speed_mm_s;
    uint32_t state_revision;
    uint64_t accepted_source_common_ns;
    int64_t accepted_delta_ns[RANGING_TRACKER_ACCEPTED_WINDOW];
    uint8_t tracker_decision;
    uint8_t accepted_count;
    uint8_t candidate_count;
} MeasurementDiagnosticsSnapshot;

/** @brief 面向两屏的显式展示状态；HOLD仍可带旧值，STALE/FAULT无值。 */
typedef enum
{
    MEASUREMENT_PRESENTATION_NONE = 0,
    MEASUREMENT_PRESENTATION_ACQUIRING,
    MEASUREMENT_PRESENTATION_TRACKING,
    MEASUREMENT_PRESENTATION_HOLD,
    MEASUREMENT_PRESENTATION_REACQUIRE,
    MEASUREMENT_PRESENTATION_STALE,
    MEASUREMENT_PRESENTATION_FAULT
} MeasurementPresentationState;

/** @brief 本地/远程权威状态的只读展示副本；不等同于测量有效或物理精度。 */
typedef struct
{
    MeasurementPresentationState state;
    bool has_value;
    bool fresh;
    bool formal;
    bool provisional;
    int64_t filtered_delta_ns;
    int32_t distance_mm;
    uint8_t direction;             /* 1=A侧，2=B侧，0未知。 */
    uint8_t quality;
    uint8_t evidence_kind;
    uint32_t measurement_id;
    uint32_t state_revision;
    uint32_t time_domain;
    uint32_t authority_clock_epoch;
    uint32_t temperature_version;
    uint32_t sound_speed_mm_s;
    uint64_t source_common_ns;
    uint64_t fresh_deadline_ns;
    uint64_t display_deadline_ns;
    uint32_t captured_ms;
    uint16_t calibration_version;
    int16_t calibration_offset_mm;
    uint16_t reason;
    uint16_t detection_version;
    uint32_t source_key_crc32;     /**< 仅诊断摘要，不代替源身份。 */
    uint32_t source_age_ms;        /**< 用实际共同时间计算，非本地收包年龄。 */
} MeasurementPresentationSnapshot;

/**
 * @brief 前台协调者确认的展示上下文；不能由单条pair或RST2自行建立。
 * @details authority=true为A；authority_epoch来自状态握手，local_clock_epoch来自本板PTP。
 *          source_floor_ns是本次握手的源时间下界，旧声音不得穿越上下文恢复。
 */
typedef struct
{
    bool authority;
    bool domain_valid;
    bool stream_valid;
    bool clock_valid;
    uint32_t time_domain;
    uint32_t authority_epoch;
    uint32_t local_clock_epoch;
    uint32_t a_session;
    uint32_t b_session;
    uint64_t source_floor_ns;
} MeasurementContext;

/**
 * @brief 持续更新真实健康上下文、温补和源期限；由App前台在收发/显示前调用。
 * @param context 非NULL协调者副本；非法域/时钟/流立即撤值，不能从pair补成健康。
 * @param temperature 可NULL；A温度无效时撤值，B不重算A的结果。
 * @param now_ms 当前HAL毫秒，仅作诊断和接收节奏检查。
 * @param now_common_ns 实际读取的当前共同时间纳秒；0或倒退为失效。
 * @return 无返回值；不读硬件、不发送网络、没有IRQ副作用。
 */
void MeasurementResult_Update(const MeasurementContext *context,
                               const TemperatureSnapshot *temperature,
                               uint32_t now_ms,uint64_t now_common_ns);

/** @brief 复位结果和ID；now_ms仅作时间基准，不读硬件。 */
void MeasurementResult_Init(uint32_t now_ms);

/** @brief 设置本地标定偏移/版本；距离发布前应用，远程结果保留原值。 */
void MeasurementResult_SetCalibration(int32_t offset_mm,uint16_t version);

/**
 * @brief 由配对结果生成权威测量；成功/拒绝都更新状态，返回是否VALID。
 * @param pair 非NULL已配对结果；无效/超窗口结果只生成REJECTED。
 * @param temperature 可NULL温度快照；无有效温度/声速时不能生成VALID。
 * @param now_ms 前台当前毫秒，用于超龄判断。
 */
bool MeasurementResult_PublishPair(const SoundPairResult *pair,
                                   const TemperatureSnapshot *temperature,
                                   uint32_t now_ms);

/**
 * @brief 接收A板权威结果并按原ID/距离/方向/质量发布；B板只显示，不重算。
 * @param result 非NULL已通过线协议校验的结果；captured_ms由本函数覆盖。
 * @param now_ms 接收/发布时的本地毫秒，用于2s有效期。
 * @return 字段合法且已发布返回true；否则false且不改当前结果。
 */
bool MeasurementResult_PublishRemote(const MeasurementResultSnapshot *result,
                                     uint32_t now_ms);

/** @brief 显式撤销当前结果（时钟/流/配对边界由上层调用）；now_ms仅记录。 */
void MeasurementResult_Invalidate(uint32_t now_ms);

/** @brief 硬撤销tracker展示状态；普通pair拒绝不应调用。 */
void MeasurementResult_InvalidatePresentation(uint32_t now_ms);

/** @brief 复制结果；超龄时valid=false但status可保留REJECTED/VALID供诊断。 */
bool MeasurementResult_Get(uint32_t now_ms, MeasurementResultSnapshot *out);

/** @brief 复制最近一次测量决策诊断；NULL不操作，返回值表示有可用副本。 */
bool MeasurementResult_GetDiagnostics(MeasurementDiagnosticsSnapshot *out);

/**
 * @brief 将独立pair送入tracker并更新显式展示状态。
 * @param pair 非NULL已配对结果；域/session/源时刻必须完整。
 * @param temperature 可NULL；无效时硬撤销展示值。
 * @param formal 标准序列/门控是否已确认；false表示PROVISIONAL。
 * @param now_ms 本地HAL毫秒，仅写展示/诊断。
 * @param now_common_ns 实际读取的当前共同时间纳秒；0表示读时失败，不能用源时刻替代。
 * @return tracker本次决策；展示副本通过GetPresentation读取。
 */
RangingDecision MeasurementResult_PublishTrackedPair(const SoundPairResult *pair,
                                                     const TemperatureSnapshot *temperature,
                                                     bool formal,
                                                     uint32_t now_ms,
                                                     uint64_t now_common_ns);

/** @brief 复制当前展示快照；无结果时state=NONE且has_value=false。 */
void MeasurementResult_GetPresentation(MeasurementPresentationSnapshot *out);

/**
 * @brief B接收A的RST2展示副本；只缓存同一共同域的显式状态，不刷新源年龄。
 * @param remote A已解码的状态；captured_ms由接收方记录。
 * @param time_domain 当前已确认共同域，不匹配则拒绝。
 * @param now_ms 本地接收毫秒，仅诊断。
 * @return 接受返回true；旧revision/无域返回false且不覆盖。
 */
bool MeasurementResult_PublishRemotePresentation(const MeasurementPresentationSnapshot *remote,
                                                 uint32_t time_domain,
                                                 uint32_t now_ms);

#endif
