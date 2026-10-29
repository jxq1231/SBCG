/**
 * @file        calibration.h
 * @brief       0.5m单点距离标定：偏移、版本和有效性（纯C）。
 * @details     offset_mm = 500 - raw_mm；Apply(raw)=raw+offset；版本随标定变化。
 * @note        本模块不保存到Flash、不读HAL/网络；标定触发由板端界面/按键或后续命令层决定。
 */
#ifndef SBCG_CALIBRATION_H
#define SBCG_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#define CALIBRATION_REFERENCE_MM 500
#define CALIBRATION_MAX_ABS_OFFSET_MM 1000

typedef struct
{
    bool calibrated;
    int32_t offset_mm;
    int32_t reference_mm;
    uint16_t version;
} CalibrationSnapshot;

/** @brief 复位为未标定、offset=0、version=0。 */
void Calibration_Init(void);

/**
 * @brief 用0.5m参考的原始测量建立标定。
 * @param raw_mm 原始距离，0..10000；偏移绝对值不得超过CALIBRATION_MAX_ABS_OFFSET_MM。
 * @return 成功true并递增版本；失败false且不改状态。
 */
bool Calibration_SetFromMeasurement(int32_t raw_mm);

/** @brief 清除标定，offset=0、calibrated=false、version=0。 */
void Calibration_Reset(void);

/** @brief 应用偏移；未标定等价offset0。 */
int32_t Calibration_Apply(int32_t raw_mm);

/** @brief 复制标定快照；NULL不操作。 */
void Calibration_Get(CalibrationSnapshot *out);

#endif
