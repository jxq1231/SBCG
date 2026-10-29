/**
 * @file        measurement_gate.h
 * @brief       静默/瞬态/失域测量状态机（纯C）。
 * @details     BOOT_WARMUP→WAIT_STANDARD→SEQUENCE_CAND→MEASURED；无标准事件超时进入
 *              SILENCE_TIMEOUT并请求撤销；失域/断流进入FAULT，恢复需重新预热。
 * @note        只做发布许可和失效请求，不计算距离、不访问HAL/网络、不替代标准声识别。
 */
#ifndef SBCG_MEASUREMENT_GATE_H
#define SBCG_MEASUREMENT_GATE_H

#include <stdbool.h>
#include <stdint.h>

#define MEASUREMENT_GATE_WARMUP_MS 1000U
#define MEASUREMENT_GATE_SILENCE_MS 3000U

typedef enum
{
    MEASUREMENT_GATE_BOOT = 0,
    MEASUREMENT_GATE_WAIT,
    MEASUREMENT_GATE_CAND,
    MEASUREMENT_GATE_MEASURED,
    MEASUREMENT_GATE_SILENCE,
    MEASUREMENT_GATE_FAULT
} MeasurementGateState;

typedef struct
{
    MeasurementGateState state;
    bool can_publish;
    uint32_t state_since_ms;
    uint32_t last_chirp_ms;
    uint32_t session;
    bool session_valid;
} MeasurementGateSnapshot;

void MeasurementGate_Init(uint32_t now_ms);

/** @brief 提交一次已分类为CHIRP的本端事件；session变化会重新预热。 */
void MeasurementGate_OnChirp(uint32_t now_ms,uint32_t session);

/** @brief 标准序列形成；只有非FAULT/预热完成后才进入CAND。 */
void MeasurementGate_OnSequence(uint32_t now_ms);

/** @brief 成功发布一次权威测量；仅CAND/MEASURED可进入/保持MEASURED。 */
bool MeasurementGate_OnMeasurement(uint32_t now_ms);

/** @brief 共同域失效：立即FAULT并请求撤销。 */
void MeasurementGate_OnDomainInvalid(uint32_t now_ms);

/** @brief 共同域恢复：从FAULT回到BOOT重新预热。 */
void MeasurementGate_OnDomainValid(uint32_t now_ms);

/** @brief 流/会话断流：立即FAULT并请求撤销。 */
void MeasurementGate_OnStreamInvalid(uint32_t now_ms);

/** @brief 周期推进；超时进入SILENCE并请求撤销。 */
void MeasurementGate_Tick(uint32_t now_ms);

/** @brief 当前是否允许发布权威测量。 */
bool MeasurementGate_CanPublish(const MeasurementGateSnapshot *snapshot);

/** @brief 取走一次“必须撤销MeasurementResult”的请求。 */
bool MeasurementGate_TakeInvalidation(void);

/** @brief 复制状态；NULL不操作。 */
void MeasurementGate_Get(MeasurementGateSnapshot *out);

#endif
