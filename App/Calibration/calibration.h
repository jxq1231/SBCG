/**
 * @file        calibration.h
 * @brief       500mm单点距离标定的保存系数、应用开关和状态版本接口。
 * @details     offset_mm保存500-raw_mm；仅enabled为真时应用偏移；清除与开关保留版本历史。
 * @note        仅前台串行调用；不保存到Flash、不访问HAL或网络；初始化将版本归零。
 */
#ifndef SBCG_CALIBRATION_H
#define SBCG_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CALIBRATION_REFERENCE_MM 500
#define CALIBRATION_MAX_ABS_OFFSET_MM 1000

typedef struct
{
    bool calibrated;
    int32_t offset_mm;
    int32_t reference_mm;
    uint16_t version;
    bool enabled;
} CalibrationSnapshot;

/**
 * @brief 初始化为未标定且停用，清零系数和版本，参考距离设为500mm。
 * @return 无返回值。
 * @note 应在应用初始化阶段调用；运行中的清除使用Calibration_Reset保留版本历史。
 */
void Calibration_Init(void);

/**
 * @brief 检查原始测量及其拟应用偏移，供采集阶段只读筛选。
 * @param raw_mm 未应用标定偏移的温补距离，单位mm。
 * @return 原始距离与拟应用偏移均合规时为true。
 */
bool Calibration_CanSetFromMeasurement(int32_t raw_mm);

/**
 * @brief 用500mm参考的原始测量替换已保存系数，并启用偏移应用。
 * @param raw_mm 原始距离，单位mm，范围0..10000；偏移绝对值不得超过CALIBRATION_MAX_ABS_OFFSET_MM。
 * @return 成功返回true并递增版本；失败返回false且保持全部状态。
 * @note 每次成功标定都推进版本，16位回绕时跳过零；输入必须是未应用偏移的距离。
 */
bool Calibration_SetFromMeasurement(int32_t raw_mm);

/**
 * @brief 修改已保存标定的应用开关，保留系数。
 * @param enabled true启用已保存偏移；false停用偏移应用。
 * @return 已保存有效标定时返回true，包括开关状态未变化；未标定时返回false且不改状态。
 * @note 仅开关状态实际变化时推进非零版本；停用后仍可重新启用或替换系数。
 */
bool Calibration_SetEnabled(bool enabled);

/**
 * @brief 清除已保存系数并停用应用；已有标定或启用状态时推进非零版本。
 * @return 无返回值。
 * @note 清除后参考距离仍为500mm；重复清除不改版本，不允许恢复已清除的系数。
 */
void Calibration_Reset(void);

/**
 * @brief 根据当前开关决定是否向原始距离加上已保存偏移。
 * @param raw_mm 原始距离，单位mm；调用方应保证与偏移相加不会超出int32_t范围。
 * @return 启用时返回raw_mm加保存偏移，停用或未标定时返回raw_mm。
 * @note 不修改保存系数、开关或版本，不对输出距离限幅。
 */
int32_t Calibration_Apply(int32_t raw_mm);

/**
 * @brief 复制当前标定状态，包含保存系数与应用开关。
 * @param out 输出快照，内存由调用方持有；仅调用期间写入，NULL时不操作。
 * @return 无返回值。
 * @note offset_mm始终代表保存系数；实际应用值为enabled为真时的offset_mm，否则为零。
 */
void Calibration_Get(CalibrationSnapshot *out);

#ifdef __cplusplus
}
#endif

#endif
