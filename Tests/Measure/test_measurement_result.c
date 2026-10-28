/**
 * @file        test_measurement_result.c
 * @brief       验证旧结果接口及显式可信上下文下的跟踪展示兼容性。
 * @details     旧模式保留既有毫米稳定器；Update启用后只允许权威展示作为结果真值。
 * @note        全部使用合成输入，不操作硬件。
 */
#include "Measure/measurement_result.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static TemperatureSnapshot Temp(bool valid,uint32_t speed,uint32_t version)
{
    TemperatureSnapshot t={0};t.valid=valid;t.sound_speed_mm_s=speed;t.version=version;return t;
}
static SoundPairResult Pair(bool valid,int64_t delta,uint8_t quality,SoundDirection dir)
{
    SoundPairResult p={0};p.valid=valid;p.status=valid?SOUND_PAIR_OK:SOUND_PAIR_QUALITY;
    p.delta_ns=delta;p.quality=quality;p.direction=dir;p.time_domain=7U;return p;
}
static uint32_t tracked_a_sequence=1U,tracked_b_sequence=1U;
static SoundPairResult TrackedPair(int32_t mm,uint32_t source_ms)
{
    const int64_t magnitude=mm<0?-(int64_t)mm:(int64_t)mm;
    const int64_t delta=(magnitude*1000000000LL+343000LL/2LL)/343000LL;
    SoundPairResult p=Pair(true,mm<0?-delta:delta,98U,
        mm<0?SOUND_DIRECTION_B:SOUND_DIRECTION_A);
    p.a_session=11U;p.b_session=12U;p.a_sequence=tracked_a_sequence++;
    p.b_sequence=tracked_b_sequence++;
    p.a_time_ns=1000000000ULL+(uint64_t)source_ms*1000000ULL;
    p.b_time_ns=(uint64_t)((int64_t)p.a_time_ns+p.delta_ns);
    return p;
}
/** @brief 运行旧接口与新权威展示回归，成功返回0，断言失败退出。 */
int main(void)
{
    MeasurementResult_Init(0U);MeasurementResultSnapshot out;
    assert(!MeasurementResult_Get(0U,&out) && out.status==MEASUREMENT_NONE);
    TemperatureSnapshot temp=Temp(true,343000U,7U);
    SoundPairResult pair=Pair(true,1000000,80U,SOUND_DIRECTION_A);
    assert(MeasurementResult_PublishPair(&pair,&temp,100U));
    assert(MeasurementResult_Get(100U,&out) && out.valid && out.id==1U);
    assert(out.distance_mm==343 && out.direction==SOUND_DIRECTION_A &&
           out.quality==80U && out.sound_speed_mm_s==343000U && out.temperature_version==7U);
    MeasurementDiagnosticsSnapshot diag;
    assert(MeasurementResult_GetDiagnostics(&diag));
    assert(diag.schema==1U && diag.decision==MEASUREMENT_DECISION_ACCEPT &&
           diag.measurement_id==1U &&
           diag.raw_delta_ns==1000000 && diag.published_mm==343);
    assert(!MeasurementResult_Get(100U+MEASUREMENT_MAX_AGE_MS,&out) && !out.valid);
    /* No temperature => cannot publish formal distance. */
    assert(!MeasurementResult_PublishPair(&pair,&(TemperatureSnapshot){0},200U));
    assert(MeasurementResult_GetDiagnostics(&diag) &&
           diag.decision==MEASUREMENT_DECISION_REJECT_TEMPERATURE);
    assert(!MeasurementResult_Get(200U,&out) && out.status==MEASUREMENT_REJECTED);
    /* Valid temperature but too far/negative/type mismatch is rejected. */
    pair=Pair(true,100000000,80U,SOUND_DIRECTION_A);
    assert(!MeasurementResult_PublishPair(&pair,&temp,300U));
    pair=Pair(false,0,0U,SOUND_DIRECTION_UNKNOWN);
    assert(!MeasurementResult_PublishPair(&pair,&temp,400U));
    /* Direction B and temperature version propagate on success. */
    pair=Pair(true,-1000000,60U,SOUND_DIRECTION_B);
    temp=Temp(true,350000U,9U);
    assert(MeasurementResult_PublishPair(&pair,&temp,500U));
    assert(MeasurementResult_Get(500U,&out) && out.direction==SOUND_DIRECTION_B &&
           out.distance_mm==350 && out.sound_speed_mm_s==350000U && out.temperature_version==9U);
    MeasurementResult_Invalidate(600U);
    assert(!MeasurementResult_Get(600U,&out) && out.status==MEASUREMENT_NONE);
    /* Remote A result keeps its ID/distance and is not recomputed locally. */
    MeasurementResultSnapshot remote=(MeasurementResultSnapshot){
        .status=MEASUREMENT_VALID,.valid=true,.id=42U,.distance_mm=777,
        .direction=SOUND_DIRECTION_B,.quality=90U,.sound_speed_mm_s=346000U,
        .temperature_version=3U,.temperature_valid=true,.time_domain=1U};
    assert(MeasurementResult_PublishRemote(&remote,700U));
    assert(MeasurementResult_Get(700U,&out) && out.id==42U && out.distance_mm==777 &&
           out.direction==SOUND_DIRECTION_B && out.quality==90U);
    remote.time_domain=0U;assert(!MeasurementResult_PublishRemote(&remote,800U));
    /* Out-of-order old remote IDs must not overwrite a fresh newer result. */
    MeasurementResultSnapshot older=remote;older.id=41U;older.time_domain=1U;
    assert(!MeasurementResult_PublishRemote(&older,800U));
    assert(MeasurementResult_Get(800U,&out) && out.id==42U);
    older.id=43U;assert(MeasurementResult_PublishRemote(&older,900U));
    assert(MeasurementResult_Get(900U,&out) && out.id==43U);
    /* Only after the current result ages out may a lower ID be accepted (A reboot). */
    older.id=1U;assert(!MeasurementResult_PublishRemote(&older,900U));
    assert(MeasurementResult_PublishRemote(&older,900U+MEASUREMENT_MAX_AGE_MS));
    assert(MeasurementResult_Get(900U+MEASUREMENT_MAX_AGE_MS,&out) && out.id==1U);
    /* Calibration offset/version are applied locally and preserved for remote display. */
    MeasurementResult_Init(1000U);
    MeasurementResult_SetCalibration(20,5U);
    pair=Pair(true,1000000,80U,SOUND_DIRECTION_A);temp=Temp(true,343000U,7U);
    assert(MeasurementResult_PublishPair(&pair,&temp,1001U));
    assert(MeasurementResult_Get(1001U,&out) && out.distance_mm==363 &&
           out.calibration_version==5U && out.calibration_offset_mm==20);
    MeasurementResultSnapshot remote2=out;remote2.id=99U;
    assert(MeasurementResult_PublishRemote(&remote2,1002U));
    assert(MeasurementResult_Get(1002U,&out) && out.distance_mm==363 &&
           out.calibration_version==5U && out.calibration_offset_mm==20);

    /* 中值稳定器：固定间距下3点起稳定，孤立大离群不覆盖已显示结果。 */
    MeasurementResult_Init(2000U);
    temp=Temp(true,343000U,7U);
    const int64_t delta320=((int64_t)320*1000000000LL+343000/2)/343000;
    const int64_t delta322=((int64_t)322*1000000000LL+343000/2)/343000;
    const int64_t delta318=((int64_t)318*1000000000LL+343000/2)/343000;
    pair=Pair(true,delta320,95U,SOUND_DIRECTION_A);
    assert(MeasurementResult_PublishPair(&pair,&temp,2100U));
    pair=Pair(true,delta322,95U,SOUND_DIRECTION_A);
    assert(MeasurementResult_PublishPair(&pair,&temp,3100U));
    pair=Pair(true,delta318,95U,SOUND_DIRECTION_A);
    assert(MeasurementResult_PublishPair(&pair,&temp,4100U));
    assert(MeasurementResult_Get(4100U,&out) && out.distance_mm==320U);
    pair=Pair(true,((int64_t)400*1000000000LL+343000/2)/343000,90U,SOUND_DIRECTION_A);
    assert(!MeasurementResult_PublishPair(&pair,&temp,5100U));
    assert(MeasurementResult_Get(5100U,&out) && out.distance_mm==320U);
    pair=Pair(true,delta320,95U,SOUND_DIRECTION_A);
    assert(MeasurementResult_PublishPair(&pair,&temp,6100U));
    assert(MeasurementResult_Get(6100U,&out) && out.distance_mm==320U);
    /* 旧入口也不得让64位乘法回绕生成零距离。 */
    MeasurementResult_Init(7000U);
    pair=Pair(true,INT64_C(1)<<61,95U,SOUND_DIRECTION_A);
    assert(!MeasurementResult_PublishPair(&pair,&temp,7000U));

    /* A2b展示层：首点临时，3点确认，离群HOLD，远程状态缓存。 */
    tracked_a_sequence=1U;tracked_b_sequence=1U;
    MeasurementResult_Init(8000U);
    TemperatureSnapshot tracked_temp=Temp(true,343000U,9U);
    SoundPairResult tracked=TrackedPair(315,0U);
    MeasurementContext tracked_context={true,true,true,true,7U,41U,3U,11U,12U,900000000ULL};
    MeasurementResult_Update(&tracked_context,&tracked_temp,8000U,
        tracked.a_time_ns>tracked.b_time_ns?tracked.a_time_ns:tracked.b_time_ns);
    assert(MeasurementResult_PublishTrackedPair(&tracked,&tracked_temp,false,8000U,
        tracked.a_time_ns>tracked.b_time_ns?tracked.a_time_ns:tracked.b_time_ns)!=
        RANGING_DECISION_REJECT);
    MeasurementPresentationSnapshot presentation;
    MeasurementResult_GetPresentation(&presentation);
    assert(presentation.state==MEASUREMENT_PRESENTATION_ACQUIRING &&
           presentation.has_value && presentation.provisional &&
           presentation.distance_mm==315);
    tracked=TrackedPair(314,2000U);
    assert(MeasurementResult_PublishTrackedPair(&tracked,&tracked_temp,true,10000U,
        tracked.a_time_ns>tracked.b_time_ns?tracked.a_time_ns:tracked.b_time_ns)!=
        RANGING_DECISION_REJECT);
    tracked=TrackedPair(316,4000U);
    assert(MeasurementResult_PublishTrackedPair(&tracked,&tracked_temp,true,12000U,
        tracked.a_time_ns>tracked.b_time_ns?tracked.a_time_ns:tracked.b_time_ns)==
        RANGING_DECISION_ACCEPT);
    MeasurementResult_GetPresentation(&presentation);
    assert(presentation.state==MEASUREMENT_PRESENTATION_TRACKING &&
           presentation.has_value && !presentation.formal &&
           presentation.provisional && presentation.distance_mm==315);
    /* 前两次临时结果和第三次确认均来自独立源，ID不得回退复用。 */
    assert(presentation.measurement_id==3U);
    const uint32_t formal_id=presentation.measurement_id;
    tracked=TrackedPair(245,6000U);
    assert(MeasurementResult_PublishTrackedPair(&tracked,&tracked_temp,true,14000U,
        tracked.a_time_ns>tracked.b_time_ns?tracked.a_time_ns:tracked.b_time_ns)==
        RANGING_DECISION_HOLD);
    MeasurementResult_GetPresentation(&presentation);
    assert(presentation.state==MEASUREMENT_PRESENTATION_HOLD &&
           presentation.has_value && !presentation.fresh &&
           presentation.distance_mm==315);
    assert(presentation.measurement_id==formal_id);
    const MeasurementPresentationSnapshot remote_presentation=presentation;
    MeasurementResult_Init(15000U);
    tracked_context.authority=false;tracked_context.local_clock_epoch=9U;
    MeasurementResult_Update(&tracked_context,&tracked_temp,15000U,
        tracked.a_time_ns>tracked.b_time_ns?tracked.a_time_ns:tracked.b_time_ns);
    assert(MeasurementResult_PublishRemotePresentation(&remote_presentation,7U,15000U));
    MeasurementResult_GetPresentation(&presentation);
    assert(presentation.state==MEASUREMENT_PRESENTATION_HOLD &&
           presentation.distance_mm==315 && presentation.captured_ms==15000U);
    puts("PASS measurement result: id/age/temp version/direction/speed/range/calibration, remote ID, median/outlier and presentation");
    return 0;
}
