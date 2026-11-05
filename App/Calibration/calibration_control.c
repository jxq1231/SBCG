/**
 * @file        calibration_control.c
 * @brief       消费设置页确认动作并维护保存/应用标定状态。
 * @details     App以实际刷新后的受检样本驱动；确认与执行样本身份不一致则拒绝，B永不修改本地标定。
 * @note        仅前台串行，单请求无动态队列；系数变更后的撤值由调用方沿已有测量合同完成。
 */
#include "calibration_control.h"
#include <stddef.h>
#include <string.h>

static CalibrationControlSnapshot state;
static MeasurementCalibrationSample confirmed;

/** @brief 清空控制软件状态，不修改已保存标定。@return 无返回值，App初始化阶段调用。 */
void CalibrationControl_Init(void)
{
    memset(&state,0,sizeof state);
    memset(&confirmed,0,sizeof confirmed);
}

/**
 * @brief 接收UI确认后的单条动作并复制样本身份。
 * @param action CAPTURE/ENABLE/DISABLE/CLEAR之一，外部边界在此校验。
 * @param expected CAPTURE非NULL，确认时的受检副本；其余动作允许NULL，不保留指针。
 * @return 参数合法且没有待处理请求为true；否则false且不覆盖旧请求。
 */
bool CalibrationControl_Request(CalibrationAction action,const MeasurementCalibrationSample *expected)
{
    if(action<=CALIBRATION_ACTION_NONE || action>CALIBRATION_ACTION_CLEAR ||
       state.result==CALIBRATION_CONTROL_PENDING ||
       (action==CALIBRATION_ACTION_CAPTURE && expected==NULL))return false;
    confirmed=expected!=NULL?*expected:(MeasurementCalibrationSample){0};
    state.action=action;
    state.result=CALIBRATION_CONTROL_PENDING;
    return true;
}

/**
 * @brief 更新当前样本/应用元数据，执行待处理动作后返回是否改变标定合同。
 * @param authority A权威为true；false一律拒绝写操作。
 * @param sample 测量模块已按当前健康和期限核对的只读样本，可NULL表示无资格。
 * @param applied_offset_mm 当前权威展示的有效偏移，仅B读取，单位mm。
 * @param applied_version 当前权威展示版本，仅B读取。
 * @return 本地版本变化为true；调用方须同步测量合同并撤销旧展示。
 * @note 不重复核验测量模块内部健康条件；仅比较跨UI确认阶段的样本身份。
 */
bool CalibrationControl_Process(bool authority,const MeasurementCalibrationSample *sample,
                                int32_t applied_offset_mm,uint16_t applied_version)
{
    state.authority=authority;
    state.sample_available=sample!=NULL;
    state.sample=sample!=NULL?*sample:(MeasurementCalibrationSample){0};
    Calibration_Get(&state.calibration);
    state.applied_offset_mm=authority?
        (state.calibration.enabled?state.calibration.offset_mm:0):applied_offset_mm;
    state.applied_version=authority?state.calibration.version:applied_version;
    if(state.result!=CALIBRATION_CONTROL_PENDING)return false;

    const uint16_t before=state.calibration.version;
    ++state.completed;
    if(!authority)state.result=CALIBRATION_CONTROL_A_ONLY;
    else if(state.action==CALIBRATION_ACTION_CAPTURE)
    {
        if(sample==NULL)state.result=CALIBRATION_CONTROL_NO_SAMPLE;
        else if(sample->measurement_id!=confirmed.measurement_id ||
                sample->state_revision!=confirmed.state_revision ||
                sample->time_domain!=confirmed.time_domain ||
                sample->authority_clock_epoch!=confirmed.authority_clock_epoch ||
                sample->source_common_ns!=confirmed.source_common_ns)
            state.result=CALIBRATION_CONTROL_SAMPLE_CHANGED;
        else state.result=Calibration_SetFromMeasurement(sample->raw_mm)?
            CALIBRATION_CONTROL_APPLIED:CALIBRATION_CONTROL_OUT_OF_RANGE;
    }
    else if(state.action==CALIBRATION_ACTION_CLEAR)
    {
        Calibration_Reset();
        state.result=CALIBRATION_CONTROL_APPLIED;
    }
    else
        state.result=Calibration_SetEnabled(state.action==CALIBRATION_ACTION_ENABLE)?
            CALIBRATION_CONTROL_APPLIED:CALIBRATION_CONTROL_NO_SAVED;

    Calibration_Get(&state.calibration);
    if(authority)
    {
        state.applied_offset_mm=state.calibration.enabled?state.calibration.offset_mm:0;
        state.applied_version=state.calibration.version;
    }
    const bool changed=before!=state.calibration.version;
    if(changed)
    {
        state.sample_available=false;
        memset(&state.sample,0,sizeof state.sample);
    }
    return changed;
}

/** @brief 复制控制副本，不读硬件或推进测量时间。@param out 可NULL调用方输出。@return 无返回值。 */
void CalibrationControl_GetSnapshot(CalibrationControlSnapshot *out)
{
    if(out!=NULL)*out=state;
}

/** @brief 模式不允许标定动作时取消待处理请求并标记模式拒绝，不修改标定数据。 */
void CalibrationControl_CancelPendingForMode(void)
{
    if(state.result!=CALIBRATION_CONTROL_PENDING)return;
    ++state.completed;
    state.result=CALIBRATION_CONTROL_MODE_REJECT;
}
