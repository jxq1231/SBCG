/**
 * @file        measurement_result.h
 * @brief       两板测距的唯一权威结果缓存：测量ID、距离、方向、质量、温度版本。
 * @details     只接受已配对的SoundPairResult并重新用当前声速计算距离；无温度/超龄/越界时结果为无效。
 * @note        前台单写单读；不访问PCM/HAL/PTP，不生成网络报文，不把无效结果补零。
 */
#ifndef SBCG_MEASUREMENT_RESULT_H
#define SBCG_MEASUREMENT_RESULT_H

#include "Measure/sound_pair.h"
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
} MeasurementResultSnapshot;

/** @brief 复位结果和ID；now_ms仅作时间基准，不读硬件。 */
void MeasurementResult_Init(uint32_t now_ms);

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

/** @brief 复制结果；超龄时valid=false但status可保留REJECTED/VALID供诊断。 */
bool MeasurementResult_Get(uint32_t now_ms, MeasurementResultSnapshot *out);

#endif
