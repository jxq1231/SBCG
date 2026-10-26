/**
 * @file        measurement_result.c
 * @brief       两板测距唯一权威结果的缓存与有效性处理。
 * @details     配对结果必须有效且温度/声速当前有效；距离用当前声速重算，超龄/越界不显示数值。
 * @note        纯前台标量；无PCM/HAL/PTP/网络访问，无动态内存。
 */
#include "measurement_result.h"
#include "sound_pair.h"
#include <stddef.h>
#include <string.h>

static MeasurementResultSnapshot latest;
static uint32_t next_id;
static int32_t calibration_offset_mm;
static uint16_t calibration_version;
static const uint32_t NS_PER_SEC = 1000000000U;

static uint64_t AbsDelta(int64_t value)
{
    return value < 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
}

void MeasurementResult_Init(uint32_t now_ms)
{
    memset(&latest, 0, sizeof latest);
    latest.captured_ms = now_ms;
    latest.status = MEASUREMENT_NONE;
    next_id = 0U;
    calibration_offset_mm = 0;
    calibration_version = 0U;
}

void MeasurementResult_SetCalibration(int32_t offset_mm,uint16_t version)
{
    calibration_offset_mm=offset_mm;
    calibration_version=version;
}

bool MeasurementResult_PublishPair(const SoundPairResult *pair,
                                   const TemperatureSnapshot *temperature,
                                   uint32_t now_ms)
{
    latest = (MeasurementResultSnapshot){0};
    latest.captured_ms = now_ms;
    latest.status = MEASUREMENT_REJECTED;
    if (pair == NULL || !pair->valid || pair->time_domain == 0U ||
        temperature == NULL || !temperature->valid ||
        temperature->sound_speed_mm_s == 0U)
    { return false; }
    const uint64_t magnitude = AbsDelta(pair->delta_ns);
    const uint64_t distance = (magnitude * temperature->sound_speed_mm_s +
                               NS_PER_SEC / 2U) / NS_PER_SEC;
    if (distance > MEASUREMENT_MAX_DISTANCE_MM) { return false; }
    const int32_t corrected=(int32_t)distance+calibration_offset_mm;
    if (corrected < 0 || corrected > (int32_t)MEASUREMENT_MAX_DISTANCE_MM) { return false; }
    latest.status = MEASUREMENT_VALID;
    latest.valid = true;
    latest.id = ++next_id;
    latest.distance_mm = corrected;
    latest.calibration_version = calibration_version;
    latest.calibration_offset_mm = (int16_t)calibration_offset_mm;
    latest.direction = pair->direction;
    latest.quality = pair->quality;
    latest.sound_speed_mm_s = temperature->sound_speed_mm_s;
    latest.temperature_version = temperature->version;
    latest.temperature_valid = true;
    latest.time_domain = pair->time_domain;
    return true;
}

bool MeasurementResult_PublishRemote(const MeasurementResultSnapshot *result,
                                     uint32_t now_ms)
{
    if (result == NULL || result->status != MEASUREMENT_VALID || !result->valid ||
        result->id == 0U || result->distance_mm < 0 ||
        result->distance_mm > (int32_t)MEASUREMENT_MAX_DISTANCE_MM ||
        result->quality > 100U || result->sound_speed_mm_s == 0U ||
        result->time_domain == 0U) { return false; }
    /* 同一ID刷新年龄；旧ID只在当前结果已超龄后允许覆盖，避免乱序旧包覆盖新值。 */
    if (latest.status == MEASUREMENT_VALID && latest.id > result->id &&
        (uint32_t)(now_ms - latest.captured_ms) < MEASUREMENT_MAX_AGE_MS)
    { return false; }
    latest = *result;
    latest.captured_ms = now_ms;
    latest.status = MEASUREMENT_VALID;
    latest.valid = true;
    latest.temperature_valid = true;
    return true;
}

void MeasurementResult_Invalidate(uint32_t now_ms)
{
    latest = (MeasurementResultSnapshot){0};
    latest.captured_ms = now_ms;
    latest.status = MEASUREMENT_NONE;
}

bool MeasurementResult_Get(uint32_t now_ms, MeasurementResultSnapshot *out)
{
    if (out == NULL) { return false; }
    *out = latest;
    if (out->status != MEASUREMENT_VALID) { out->valid = false; return false; }
    if ((uint32_t)(now_ms - out->captured_ms) >= MEASUREMENT_MAX_AGE_MS)
    { out->valid = false; return false; }
    return true;
}
