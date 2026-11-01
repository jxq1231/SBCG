/**
 * @file        test_control.c
 * @brief       验证确认样本身份、A权限和标定单请求交接。
 * @details     真实纯C控制器和标定模块运行，不替代业务判断；样本资格由测量接口另行测试。
 * @note        不访问硬件，不能证明500mm实测精度。
 */
#include "Calibration/calibration_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/** @brief 提交一条动作并消费，测试调用前已提供本轮实际受检样本。 */
static bool Apply(CalibrationAction action,const MeasurementCalibrationSample *expected,
                  bool authority,const MeasurementCalibrationSample *current)
{
    assert(CalibrationControl_Request(action,expected));
    return CalibrationControl_Process(authority,current,0,0);
}
/** @brief 复制最近控制状态，不依赖内部静态变量。 */
static CalibrationControlSnapshot State(void)
{ CalibrationControlSnapshot s;CalibrationControl_GetSnapshot(&s);return s; }
/** @brief 运行初始化、正确标定、幂等操作及失败保持用例。 */
int main(void)
{
    const MeasurementCalibrationSample first={.raw_mm=490,.measurement_id=3,
        .state_revision=9,.time_domain=7,.authority_clock_epoch=2,.source_common_ns=5000000000U};
    Calibration_Init();CalibrationControl_Init();
    assert(!CalibrationControl_Process(true,&first,0,0));
    assert(State().authority && State().sample_available && State().sample.raw_mm==490);
    assert(!CalibrationControl_Request(CALIBRATION_ACTION_NONE,NULL));
    assert(!CalibrationControl_Request((CalibrationAction)99,NULL));
    assert(!CalibrationControl_Request(CALIBRATION_ACTION_CAPTURE,NULL));
    assert(CalibrationControl_Request(CALIBRATION_ACTION_CAPTURE,&first));
    assert(!CalibrationControl_Request(CALIBRATION_ACTION_CLEAR,NULL));
    assert(CalibrationControl_Process(true,&first,0,0));
    assert(State().result==CALIBRATION_CONTROL_APPLIED && State().completed==1);
    assert(State().calibration.calibrated && State().calibration.enabled);
    assert(State().calibration.offset_mm==10 && State().applied_offset_mm==10);
    assert(!State().sample_available); /* 旧样本的合同已改变，不能在确认后继续沿用。 */
    assert(!CalibrationControl_Process(true,NULL,0,0) && State().completed==1);
    assert(Apply(CALIBRATION_ACTION_DISABLE,NULL,true,NULL));
    assert(State().calibration.offset_mm==10 && !State().calibration.enabled && State().applied_offset_mm==0);
    const uint16_t disabled=State().calibration.version;
    assert(!Apply(CALIBRATION_ACTION_DISABLE,NULL,true,NULL));
    assert(State().calibration.version==disabled && State().result==CALIBRATION_CONTROL_APPLIED);
    assert(Apply(CALIBRATION_ACTION_ENABLE,NULL,true,NULL));
    assert(State().applied_offset_mm==10);
    MeasurementCalibrationSample updated=first;updated.raw_mm=480;++updated.measurement_id;
    assert(Apply(CALIBRATION_ACTION_CAPTURE,&updated,true,&updated));
    assert(State().calibration.offset_mm==20); /* 替换原10mm，不叠加。 */
    assert(!Apply(CALIBRATION_ACTION_CAPTURE,&updated,true,NULL));
    assert(State().result==CALIBRATION_CONTROL_NO_SAMPLE && State().calibration.offset_mm==20);
    for (unsigned i=0;i<5;++i) {
        MeasurementCalibrationSample changed=updated;
        if(i==0)++changed.measurement_id;
        if(i==1)++changed.state_revision;
        if(i==2)++changed.time_domain;
        if(i==3)++changed.authority_clock_epoch;
        if(i==4)++changed.source_common_ns;
        assert(!Apply(CALIBRATION_ACTION_CAPTURE,&updated,true,&changed));
        assert(State().result==CALIBRATION_CONTROL_SAMPLE_CHANGED && State().calibration.offset_mm==20);
    }
    updated.raw_mm=1600;
    assert(!Apply(CALIBRATION_ACTION_CAPTURE,&updated,true,&updated));
    assert(State().result==CALIBRATION_CONTROL_OUT_OF_RANGE && State().calibration.offset_mm==20);
    for(CalibrationAction a=CALIBRATION_ACTION_CAPTURE;a<=CALIBRATION_ACTION_CLEAR;a=(CalibrationAction)(a+1)) {
        assert(CalibrationControl_Request(a,&updated));
        assert(!CalibrationControl_Process(false,NULL,23,7));
        assert(State().result==CALIBRATION_CONTROL_A_ONLY && State().calibration.offset_mm==20);
        assert(State().applied_offset_mm==23 && State().applied_version==7);
    }
    assert(Apply(CALIBRATION_ACTION_CLEAR,NULL,true,NULL));
    assert(!State().calibration.calibrated && !State().calibration.enabled && State().applied_offset_mm==0);
    const uint16_t cleared=State().calibration.version;
    assert(!Apply(CALIBRATION_ACTION_CLEAR,NULL,true,NULL));
    assert(State().calibration.version==cleared);
    assert(!Apply(CALIBRATION_ACTION_ENABLE,NULL,true,NULL));
    assert(State().result==CALIBRATION_CONTROL_NO_SAVED);
    assert(!Apply(CALIBRATION_ACTION_DISABLE,NULL,true,NULL));
    assert(State().result==CALIBRATION_CONTROL_NO_SAVED);
    CalibrationControl_GetSnapshot(NULL);
    puts("PASS calibration control: authority, bounded request, sample identity, replacement, enable/disable/clear");
}
