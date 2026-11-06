/**
 * @file        calibration.c
 * @brief       500mm单点标定的保存、应用开关与清除实现。
 * @details     校验原始距离后替换保存系数；状态变化推进非零版本，停用不删除系数。
 * @note        仅前台串行调用；无持久化或硬件访问，无效输入保持原状态。
 */
#include "calibration.h"
#include <stddef.h>
#include <string.h>

static CalibrationSnapshot calibration;

/**
 * @brief 推进标定状态版本，保持既有16位回绕跳过零的规则。
 * @return 无返回值。
 * @note 由成功标定、实际开关变化和有效清除共用；不访问硬件。
 */
static void Calibration_AdvanceVersion(void)
{
    ++calibration.version;
    if (calibration.version == 0U)
    {
        calibration.version = 1U;
    }
}

/** @brief 清零模块状态并设置固定参考距离，由应用初始化调用。 @return 无返回值。 */
void Calibration_Init(void)
{
    memset(&calibration, 0, sizeof calibration);
    calibration.reference_mm = CALIBRATION_REFERENCE_MM;
}

/**
 * @brief 判断原始测量是否落在现有标定偏移范围内。
 * @param raw_mm 未应用标定偏移的温补距离，单位mm。
 * @return 原始距离和拟应用偏移均合规时为true；不修改状态。
 */
bool Calibration_CanSetFromMeasurement(int32_t raw_mm)
{
    if (raw_mm < 0 || raw_mm > 10000) { return false; }
    const int32_t offset_mm=CALIBRATION_REFERENCE_MM-raw_mm;
    return offset_mm <= CALIBRATION_MAX_ABS_OFFSET_MM &&
        offset_mm >= -CALIBRATION_MAX_ABS_OFFSET_MM;
}

/**
 * @brief 校验原始距离并替换保存系数，成功后自动启用应用并推进版本。
 * @param raw_mm 未应用偏移的原始距离，单位mm，合法范围及偏移约束见公开接口。
 * @return 校验通过返回true；失败返回false且不修改模块状态。
 */
bool Calibration_SetFromMeasurement(int32_t raw_mm)
{
    if (!Calibration_CanSetFromMeasurement(raw_mm)) { return false; }
    const int32_t offset_mm = CALIBRATION_REFERENCE_MM - raw_mm;
    calibration.calibrated = true;
    calibration.offset_mm = offset_mm;
    calibration.reference_mm = CALIBRATION_REFERENCE_MM;
    calibration.enabled = true;
    Calibration_AdvanceVersion();
    return true;
}

/**
 * @brief 更改已保存标定的应用状态，实际变化时推进版本。
 * @param enabled 目标应用状态，true启用、false停用，不改变保存系数。
 * @return 已有保存系数返回true，包括重复设置；未标定返回false且不修改状态。
 */
bool Calibration_SetEnabled(bool enabled)
{
    if (!calibration.calibrated)
    {
        return false;
    }
    if (calibration.enabled != enabled)
    {
        calibration.enabled = enabled;
        Calibration_AdvanceVersion();
    }
    return true;
}

/**
 * @brief 清除已有标定和应用开关，保留参考距离并推进状态版本。
 * @return 无返回值。
 * @note 未保存标定且已停用时不操作，避免重复清除产生虚假的版本变化。
 */
void Calibration_Reset(void)
{
    if (calibration.calibrated || calibration.enabled)
    {
        calibration.calibrated = false;
        calibration.offset_mm = 0;
        calibration.enabled = false;
        Calibration_AdvanceVersion();
    }
}

/**
 * @brief 在启用状态下向原始距离加保存偏移，停用时直接返回原始距离。
 * @param raw_mm 原始距离，单位mm；调用方保证加法结果可由int32_t表示。
 * @return 按当前应用开关计算的距离，单位mm。
 */
int32_t Calibration_Apply(int32_t raw_mm)
{
    return raw_mm + (calibration.enabled ? calibration.offset_mm : 0);
}

/**
 * @brief 将当前内存状态复制给前台调用方。
 * @param out 调用方持有的输出快照，不保留指针；NULL时不写入。
 * @return 无返回值。
 */
void Calibration_Get(CalibrationSnapshot *out)
{
    if (out != NULL)
    {
        *out = calibration;
    }
}
