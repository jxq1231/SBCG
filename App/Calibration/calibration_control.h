/**
 * @file        calibration_control.h
 * @brief       500mm设置页与A板测量协调之间的单请求接口。
 * @details     UI只提交确认过的动作；App刷新实际健康/共同时间后提供受检样本并执行。
 * @note        仅前台串行调用，无HAL、队列分配或网络访问；B只读显示A应用的元数据。
 */
#ifndef SBCG_CALIBRATION_CONTROL_H
#define SBCG_CALIBRATION_CONTROL_H
#include "calibration.h"
#include "Measure/measurement_result.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CALIBRATION_ACTION_NONE, CALIBRATION_ACTION_CAPTURE,
    CALIBRATION_ACTION_ENABLE, CALIBRATION_ACTION_DISABLE,
    CALIBRATION_ACTION_CLEAR
} CalibrationAction;

typedef enum {
    CALIBRATION_CONTROL_IDLE, CALIBRATION_CONTROL_PENDING,
    CALIBRATION_CONTROL_APPLIED, CALIBRATION_CONTROL_A_ONLY,
    CALIBRATION_CONTROL_NO_SAMPLE, CALIBRATION_CONTROL_SAMPLE_CHANGED,
    CALIBRATION_CONTROL_NO_SAVED, CALIBRATION_CONTROL_OUT_OF_RANGE,
    CALIBRATION_CONTROL_MODE_REJECT
} CalibrationControlResult;

/** @brief 按钮资格及最近动作结果；sample仅在sample_available为真时有效。 */
typedef struct {
    bool authority, sample_available;
    MeasurementCalibrationSample sample;
    CalibrationSnapshot calibration;
    int32_t applied_offset_mm;
    uint16_t applied_version;
    CalibrationAction action;
    CalibrationControlResult result;
    uint32_t completed;
} CalibrationControlSnapshot;

/** @brief 初始化单请求状态；不修改标定或测量。@return 无返回值，App初始化时调用。 */
void CalibrationControl_Init(void);
/**
 * @brief UI二次确认后提交一条动作；已有待处理动作时拒绝覆盖。
 * @param action 合法非NONE动作。
 * @param expected CAPTURE必须提供确认时受检样本身份；其余动作可NULL，不保留指针。
 * @return 已复制入队为true；参数非法或请求占用为false。
 */
bool CalibrationControl_Request(CalibrationAction action, const MeasurementCalibrationSample *expected);
/**
 * @brief App刷新实际测量后更新资格并消费至多一条动作。
 * @param authority 本机是否为A权威，B的动作一律拒绝。
 * @param sample 当前受检样本副本或NULL；只读借用，仅本次有效。
 * @param applied_offset_mm 当前展示的有效偏移，B用于只读显示A元数据，单位mm。
 * @param applied_version 当前展示标定版本，B不据此推断A保存状态。
 * @return 本地标定版本实际变化为true；调用方随后更新测量合同并撤销旧结果。
 * @note 调用前必须刷新实际共同时间/健康；本模块不读取UI缓存或自行延长样本期限。
 */
bool CalibrationControl_Process(bool authority, const MeasurementCalibrationSample *sample,
                                int32_t applied_offset_mm, uint16_t applied_version);
/** @brief 复制前台状态，不刷新业务期限。@param out 可NULL输出，不保留地址。@return 无返回值。 */
void CalibrationControl_GetSnapshot(CalibrationControlSnapshot *out);

/**
 * @brief 当前模式不允许标定动作时取消待处理请求。
 * @return 无返回值；只在存在待处理动作时推进完成计数并标记模式拒绝。
 * @note 由前台模式闸门调用；不修改已保存系数、启用状态或测量结果。
 */
void CalibrationControl_CancelPendingForMode(void);
#ifdef __cplusplus
}
#endif
#endif
