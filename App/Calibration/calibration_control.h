/**
 * @file        calibration_control.h
 * @brief       A板500 mm标定的采集、锁存及确认接口。
 * @details     App用实际HAL时间、共同时间、测量上下文和受检样本推进；UI只提交动作并读副本。
 *              采集窗锁定本轮时钟代次、共同域、权威代次和音频会话，标识变化即退出并报告原因；
 *              瞬时失效只暂停捕获，恢复后标识一致则继续本轮。
 * @note        仅前台串行调用；B只读A通过既有测量合同同步的标定偏移。
 */
#ifndef SBCG_CALIBRATION_CONTROL_H
#define SBCG_CALIBRATION_CONTROL_H
#include "calibration.h"
#include "Measure/measurement_result.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef SBCG_CALIBRATION_CAPTURE_MS
#define SBCG_CALIBRATION_CAPTURE_MS 20000U
#endif
#ifndef SBCG_CALIBRATION_CONFIRM_MS
#define SBCG_CALIBRATION_CONFIRM_MS 20000U
#endif

typedef enum {
    CALIBRATION_ACTION_NONE, CALIBRATION_ACTION_START,
    CALIBRATION_ACTION_ENABLE, CALIBRATION_ACTION_DISABLE, CALIBRATION_ACTION_CLEAR,
    CALIBRATION_ACTION_CONFIRM, CALIBRATION_ACTION_CANCEL
} CalibrationAction;

typedef enum {
    CALIBRATION_CONTROL_IDLE, CALIBRATION_CONTROL_PENDING,
    CALIBRATION_CONTROL_APPLIED, CALIBRATION_CONTROL_A_ONLY,
    CALIBRATION_CONTROL_NO_SOUND, CALIBRATION_CONTROL_NO_QUALIFIED_SAMPLE,
    CALIBRATION_CONTROL_NO_SAVED, CALIBRATION_CONTROL_OUT_OF_RANGE,
    CALIBRATION_CONTROL_MODE_REJECT, CALIBRATION_CONTROL_LISTENING,
    CALIBRATION_CONTROL_CANDIDATE, CALIBRATION_CONTROL_CANCELLED,
    CALIBRATION_CONTROL_CONFIRM_TIMEOUT, CALIBRATION_CONTROL_CLOCK_LOST,
    CALIBRATION_CONTROL_DOMAIN_LOST, CALIBRATION_CONTROL_AUDIO_LOST
} CalibrationControlResult;

typedef enum {
    CALIBRATION_PHASE_IDLE, CALIBRATION_PHASE_WAIT_SOUND, CALIBRATION_PHASE_WAIT_CONFIRM
} CalibrationPhase;

/** @brief 前台已刷新后的输入。sample可空，非空仅借用至Process返回。 */
typedef struct {
    MeasurementContext context;
    const MeasurementCalibrationSample *sample;
    bool mode_allowed;
    uint32_t now_ms;             /**< 实际HAL毫秒计数，可回绕。 */
    uint64_t common_ns;          /**< 本机当前共同时间纳秒，读时失败为0。 */
    uint64_t audio_floor_frame;  /**< START时本机已采帧的保守上界；未知为UINT64_MAX。 */
    uint32_t chirp_session;      /**< 最近已分类扫频的本机音频会话；0表示无观测。 */
    uint64_t chirp_onset_frame;  /**< 上述扫频的会话内起始帧。 */
    int32_t applied_offset_mm;   /**< B展示的权威有效偏移，单位mm。 */
    uint16_t applied_version;
} CalibrationControlInput;

/** @brief UI资格、锁存候选和最近事件；candidate只在待确认阶段可用于操作。 */
typedef struct {
    bool authority;
    bool heard_chirp;
    CalibrationPhase phase;
    uint32_t remaining_ms;
    MeasurementCalibrationSample candidate;
    CalibrationSnapshot calibration;
    int32_t applied_offset_mm;
    uint16_t applied_version;
    CalibrationAction action;
    CalibrationControlResult result;
    uint32_t completed;          /**< 非倒计时状态事件计数，用于限频日志。 */
} CalibrationControlSnapshot;

/** @brief 清空前台标定会话状态，不修改保存的系数。 */
void CalibrationControl_Init(void);

/**
 * @brief 按当前阶段入队一个操作，不覆盖已有请求。
 * @param action START/CONFIRM/CANCEL或已有系数管理动作。
 * @return 已入队为true；阶段不符、参数非法或繁忙时为false。
 */
bool CalibrationControl_Request(CalibrationAction action);

/**
 * @brief 前台推进双窗口，按实际资格捕获或执行动作。
 * @param input 非NULL只读前台输入，调用期间借用；NULL不操作。
 * @return 标定版本改变时为true，调用方沿已有测量合同同步。
 */
bool CalibrationControl_Process(const CalibrationControlInput *input);

/** @brief 复制控制快照；out可空，不保存地址。 */
void CalibrationControl_GetSnapshot(CalibrationControlSnapshot *out);

/** @brief 撤销模式不允许的待处理请求和活动标定流程。 */
void CalibrationControl_CancelPendingForMode(void);

/** @brief 返回日志用的静态原因名；result为待描述的状态。 */
const char *CalibrationControl_ResultName(CalibrationControlResult result);
#ifdef __cplusplus
}
#endif
#endif
