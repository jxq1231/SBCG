/**
 * @file test_ranging_presentation.c
 * @brief 验证真实权威缓存的源期限、版本、温补标定和B接收保护。
 * @note 纯主机时间/温度输入，不提供硬件验收结论。
 */
#include "Measure/measurement_result.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL presentation %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static MeasurementContext context;
static TemperatureSnapshot temperature;
static uint32_t serial;
static uint64_t current_ns;
/** @brief 建立由测试协调者显式确认的A上下文，不从pair建立。 */
static void Setup(void)
{
    MeasurementResult_Init(0U);
    context=(MeasurementContext){true,true,true,true,7U,41U,3U,11U,12U,900000000ULL};
    temperature=(TemperatureSnapshot){0};temperature.valid=true;
    temperature.sound_speed_mm_s=343000U;temperature.version=9U;
    serial=0U;current_ns=1000000000ULL;
    MeasurementResult_Update(&context,&temperature,1000U,current_ns);
}
/** @brief 构造单个独立源；delta按343000mm/s固定测试声速换算。 */
static SoundPairResult PairMm(int32_t mm,uint32_t offset_ms)
{
    SoundPairResult p={0};p.valid=true;p.status=SOUND_PAIR_OK;
    p.time_domain=7U;p.a_session=11U;p.b_session=12U;
    p.a_sequence=p.b_sequence=++serial;p.quality=98U;
    p.delta_ns=(int64_t)mm*1000000000LL/343000LL;
    p.a_time_ns=1000000000ULL+(uint64_t)offset_ms*1000000ULL;
    p.b_time_ns=(uint64_t)((int64_t)p.a_time_ns+p.delta_ns);
    p.direction=mm>0?SOUND_DIRECTION_A:SOUND_DIRECTION_B;
    return p;
}
/** @brief 用真实当前时钟先更新健康再提交pair，返回实际展示副本。 */
static MeasurementPresentationSnapshot Push(int32_t mm,uint32_t offset_ms)
{
    SoundPairResult p=PairMm(mm,offset_ms);
    current_ns=(p.a_time_ns>p.b_time_ns?p.a_time_ns:p.b_time_ns)+20000000ULL;
    MeasurementResult_Update(&context,&temperature,1000U+offset_ms,current_ns);
    (void)MeasurementResult_PublishTrackedPair(&p,&temperature,false,
        1000U+offset_ms,current_ns);
    MeasurementPresentationSnapshot out;MeasurementResult_GetPresentation(&out);return out;
}
/** @brief 验证临时值也以源时刻到期，硬恢复不能复活旧声或启用旧发布路径。 */
static void TestProvisionalRecovery(void)
{
    Setup();
    MeasurementPresentationSnapshot p=Push(315,0U);
    const uint32_t id=p.measurement_id;
    const uint64_t source=p.source_common_ns,deadline=p.display_deadline_ns;
    MeasurementResult_Update(&context,&temperature,3500U,p.fresh_deadline_ns);
    MeasurementResult_GetPresentation(&p);
    CHECK(p.state==MEASUREMENT_PRESENTATION_HOLD && p.has_value && !p.fresh);
    CHECK(p.measurement_id==id && p.source_common_ns==source);
    MeasurementResult_Update(&context,&temperature,7500U,deadline);
    MeasurementResult_GetPresentation(&p);CHECK(!p.has_value && p.state==MEASUREMENT_PRESENTATION_STALE);
    context.stream_valid=false;
    MeasurementResult_Update(&context,&temperature,7501U,deadline+1000000ULL);
    MeasurementResult_GetPresentation(&p);const uint32_t rev=p.state_revision;
    context.stream_valid=true;
    MeasurementResult_Update(&context,&temperature,7502U,deadline+2000000ULL);
    MeasurementResult_GetPresentation(&p);
    CHECK(!p.has_value && p.state==MEASUREMENT_PRESENTATION_ACQUIRING);
    p=Push(315,10000U);CHECK(p.measurement_id>id && p.state_revision>rev);
    const uint32_t recovered_id=p.measurement_id;
    MeasurementResult_Update(&context,&temperature,11001U,0U);
    MeasurementResult_GetPresentation(&p);CHECK(!p.has_value && p.state==MEASUREMENT_PRESENTATION_FAULT);
    MeasurementResult_Update(&context,&temperature,11002U,current_ns+1000U);
    p=Push(315,12000U);CHECK(p.measurement_id>recovered_id);
}

/** @brief 确认窗口过期后，新候选必须保留自己的布防状态和源期限。 */
static void TestNewSoundAfterExpiry(void)
{
    Setup();(void)Push(315,0U);(void)Push(315,2000U);
    MeasurementPresentationSnapshot p=Push(315,4000U);
    const uint32_t old_id=p.measurement_id;
    current_ns=p.display_deadline_ns;
    MeasurementResult_Update(&context,&temperature,12000U,current_ns);
    MeasurementResult_GetPresentation(&p);CHECK(!p.has_value);
    p=Push(450,12000U);
    CHECK(p.has_value && p.fresh && p.state==MEASUREMENT_PRESENTATION_ACQUIRING);
    CHECK(p.measurement_id>old_id && p.distance_mm==450);
    const uint64_t source=p.source_common_ns,deadline=p.display_deadline_ns;
    MeasurementResult_Update(&context,&temperature,13000U,current_ns+1000000000ULL);
    MeasurementResult_GetPresentation(&p);
    CHECK(p.has_value && p.fresh && p.state==MEASUREMENT_PRESENTATION_ACQUIRING);
    CHECK(p.source_common_ns==source && p.display_deadline_ns==deadline);
    p=Push(450,14000U);
    CHECK(p.has_value && p.fresh && p.state==MEASUREMENT_PRESENTATION_ACQUIRING);
    p=Push(450,16000U);
    CHECK(p.has_value && p.fresh && p.state==MEASUREMENT_PRESENTATION_TRACKING);
}

/** @brief 验证pair不授予上下文、旧源下界及会话错配不会清除当前历史。 */
static void TestTrustedInput(void)
{
    Setup();MeasurementPresentationSnapshot p=Push(315,0U);
    const uint32_t id=p.measurement_id,rev=p.state_revision;
    SoundPairResult pair=PairMm(315,100U);pair.a_session++;
    CHECK(MeasurementResult_PublishTrackedPair(&pair,&temperature,false,1100U,
        current_ns+100000000ULL)==RANGING_DECISION_REJECT);
    MeasurementResult_GetPresentation(&p);CHECK(p.has_value && p.measurement_id==id && p.state_revision==rev);
    MeasurementResult_Init(0U);
    CHECK(MeasurementResult_PublishTrackedPair(&pair,&temperature,false,1100U,
        current_ns+100000000ULL)==RANGING_DECISION_REJECT);
    Setup();context.source_floor_ns=1200000000ULL;
    MeasurementResult_Update(&context,&temperature,1000U,current_ns);
    pair=PairMm(315,100U);
    CHECK(MeasurementResult_PublishTrackedPair(&pair,&temperature,false,1100U,
        current_ns+100000000ULL)==RANGING_DECISION_REJECT);
    MeasurementResult_GetPresentation(&p);CHECK(!p.has_value);
}
/** @brief 验证远程完整载荷冲突、同ID撤销不可逆及派生年龄不污染原载荷。 */
static void TestRemoteRevocation(void)
{
    Setup();MeasurementPresentationSnapshot p=Push(315,0U);
    p=Push(314,2000U);p=Push(316,4000U);
    const MeasurementPresentationSnapshot original=p;
    MeasurementResult_Init(0U);context.authority=false;context.local_clock_epoch=9U;
    MeasurementResult_Update(&context,NULL,5050U,current_ns);
    CHECK(MeasurementResult_PublishRemotePresentation(&original,7U,5050U));
    p=original;p.captured_ms++;p.source_age_ms=UINT32_MAX;
    CHECK(MeasurementResult_PublishRemotePresentation(&p,7U,5051U));
    MeasurementResult_GetPresentation(&p);CHECK(p.source_age_ms==20U);
    p=original;p.reason++;CHECK(!MeasurementResult_PublishRemotePresentation(&p,7U,5052U));
    p=original;p.detection_version++;CHECK(!MeasurementResult_PublishRemotePresentation(&p,7U,5052U));
    p=original;p.source_key_crc32++;CHECK(!MeasurementResult_PublishRemotePresentation(&p,7U,5052U));
    p=original;p.calibration_version++;CHECK(!MeasurementResult_PublishRemotePresentation(&p,7U,5052U));
    p=original;p.state_revision++;p.state=MEASUREMENT_PRESENTATION_FAULT;
    p.has_value=p.fresh=p.formal=p.provisional=false;
    p.measurement_id=0U;p.distance_mm=0;p.filtered_delta_ns=0;p.direction=0U;
    CHECK(MeasurementResult_PublishRemotePresentation(&p,7U,5053U));
    p=original;p.state_revision+=2U;
    CHECK(!MeasurementResult_PublishRemotePresentation(&p,7U,5054U));
    p.measurement_id++;CHECK(!MeasurementResult_PublishRemotePresentation(&p,7U,5054U));
    p.source_common_ns+=2000000000ULL;p.fresh_deadline_ns+=2000000000ULL;
    p.display_deadline_ns+=2000000000ULL;current_ns+=2000000000ULL;
    MeasurementResult_Update(&context,NULL,7050U,current_ns);
    CHECK(MeasurementResult_PublishRemotePresentation(&p,7U,7051U));
    MeasurementResult_GetPresentation(&p);CHECK(p.has_value && p.fresh);
    const MeasurementPresentationSnapshot newest=p;
    context.stream_valid=false;
    MeasurementResult_Update(&context,NULL,7052U,current_ns+1000U);
    context.stream_valid=true;
    MeasurementResult_Update(&context,NULL,7053U,current_ns+2000U);
    CHECK(MeasurementResult_PublishRemotePresentation(&newest,7U,7054U));
    MeasurementResult_GetPresentation(&p);CHECK(!p.has_value);
}

/** @brief 验证标定变化重建合同，保留单调ID，并记录真实接受窗口成员。 */
static void TestCalibrationContract(void)
{
    Setup();MeasurementResult_SetCalibration(20,5U);
    (void)Push(315,0U);(void)Push(314,2000U);
    MeasurementPresentationSnapshot p=Push(316,4000U);
    const uint32_t id=p.measurement_id;
    MeasurementDiagnosticsSnapshot diagnostic;
    CHECK(MeasurementResult_GetDiagnostics(&diagnostic));
    CHECK(diagnostic.schema==2U && diagnostic.accepted_count==3U && diagnostic.stable_count==3U);
    CHECK(diagnostic.stable_mm[0]==335 && diagnostic.stable_mm[1]==334 && diagnostic.stable_mm[2]==336);
    CHECK(diagnostic.accepted_source_common_ns==p.source_common_ns);
    MeasurementResult_SetCalibration(30,6U);
    MeasurementResult_GetPresentation(&p);
    CHECK(!p.has_value && p.state==MEASUREMENT_PRESENTATION_ACQUIRING);
    p=Push(315,6000U);
    CHECK(p.measurement_id>id && p.distance_mm==345 && p.calibration_version==6U);
}
/** @brief 执行生命周期、同源温补与接收重放反例；成功返回0。 */
int main(void)
{
    TestProvisionalRecovery();TestNewSoundAfterExpiry();TestTrustedInput();TestRemoteRevocation();TestCalibrationContract();
    Setup();MeasurementResult_SetCalibration(20,5U);
    MeasurementPresentationSnapshot p=Push(315,0U);CHECK(p.measurement_id==1U);
    uint32_t rev=p.state_revision;
    p=Push(314,2000U);CHECK(p.measurement_id==2U && p.state_revision>rev);rev=p.state_revision;
    p=Push(316,4000U);CHECK(p.measurement_id==3U && p.state_revision>rev);
    CHECK(p.distance_mm==335 && p.calibration_version==5U);
    const uint32_t id=p.measurement_id;rev=p.state_revision;
    const uint64_t deadline=p.display_deadline_ns,source=p.source_common_ns;
    temperature.sound_speed_mm_s=350000U;temperature.version=10U;
    MeasurementResult_Update(&context,&temperature,5050U,current_ns+1000000ULL);
    MeasurementResult_GetPresentation(&p);
    CHECK(p.measurement_id==id && p.state_revision>rev && p.distance_mm==341);
    CHECK(p.source_common_ns==source && p.display_deadline_ns==deadline);
    rev=p.state_revision;
    MeasurementResult_Update(&context,&temperature,8000U,source+2500000000ULL);
    MeasurementResult_GetPresentation(&p);
    CHECK(p.state==MEASUREMENT_PRESENTATION_HOLD && p.has_value && !p.fresh);
    CHECK(p.measurement_id==id && p.state_revision>rev);
    MeasurementResult_Update(&context,&temperature,12000U,deadline);
    MeasurementResult_GetPresentation(&p);
    CHECK(p.state==MEASUREMENT_PRESENTATION_STALE && !p.has_value);
    context.domain_valid=false;
    MeasurementResult_Update(&context,&temperature,12001U,deadline+1000000ULL);
    MeasurementResult_GetPresentation(&p);CHECK(!p.has_value && p.state==MEASUREMENT_PRESENTATION_FAULT);

    Setup();p=Push(315,0U);p=Push(314,2000U);p=Push(316,4000U);
    const MeasurementPresentationSnapshot original=p;
    MeasurementResult_Init(0U);context.authority=false;context.local_clock_epoch=9U;
    MeasurementResult_Update(&context,&temperature,5050U,current_ns);
    CHECK(MeasurementResult_PublishRemotePresentation(&p,7U,5050U));
    p.distance_mm++;
    CHECK(!MeasurementResult_PublishRemotePresentation(&p,7U,5051U));
    p=original;p.state_revision++;p.display_deadline_ns++;
    CHECK(!MeasurementResult_PublishRemotePresentation(&p,7U,5052U));
    p=original;p.state_revision++;p.temperature_version++;p.distance_mm++;
    CHECK(MeasurementResult_PublishRemotePresentation(&p,7U,5053U));
    MeasurementResult_Update(&context,&temperature,12000U,original.display_deadline_ns);
    MeasurementResult_GetPresentation(&p);CHECK(!p.has_value && p.state==MEASUREMENT_PRESENTATION_STALE);
    CHECK(!MeasurementResult_PublishRemotePresentation(&original,7U,12001U));
    p=original;p.state_revision+=2U;p.authority_clock_epoch++;
    CHECK(!MeasurementResult_PublishRemotePresentation(&p,7U,12002U));
    context.domain_valid=false;
    MeasurementResult_Update(&context,&temperature,12003U,original.display_deadline_ns+1000U);
    MeasurementResult_GetPresentation(&p);CHECK(!p.has_value && p.state==MEASUREMENT_PRESENTATION_FAULT);
    puts("PASS presentation: trusted context, monotonic IDs/revisions, source expiry, calibration/temp, conflict and replay guards");
    return 0;
}
