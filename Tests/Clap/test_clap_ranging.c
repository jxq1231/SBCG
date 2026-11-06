/** @file test_clap_ranging.c @brief 独立击掌权威、旧包屏障、重试与源寿命测试。 */
#include "Measure/clap_ranging.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static RangingSessionSnapshot session={true,9U,7U,1000000000U};
static TemperatureSnapshot temperature={.valid=true,.version=3U,
    .temperature_milli_c=20000,.sound_speed_mm_s=343000U};
static CalibrationSnapshot calibration={.calibrated=true,.enabled=true,
    .offset_mm=12,.reference_mm=500,.version=4U};
/** @brief 以调用方真实共同读时更新测试实例，A/B共用同一资格。 */
static void Update(ClapRanging *self, BoardRole role, bool healthy,uint64_t now_ns,uint32_t now_ms)
{ ClapRanging_Update(self,role,healthy,&session,now_ns,now_ms,&temperature,&calibration); }
/** @brief 产生一条有效CLAP配对，固定1ms TDOA用于温补/标定断言。 */
static SoundPairResult Pair(uint32_t seq,uint64_t a_ns)
{
    SoundPairResult pair={.valid=true,.status=SOUND_PAIR_OK,.delta_ns=1000000,
        .distance_mm=1U,.direction=SOUND_DIRECTION_A,.quality=85U,.time_domain=9U,
        .a_session=2U,.a_sequence=seq,.b_session=3U,.b_sequence=seq,
        .a_time_ns=a_ns,.b_time_ns=a_ns+1000000U,.type=SOUND_EVENT_CLAP};
    return pair;
}
/** @brief 验证只有A发布、共同时间期限、ACK重试与RSC2恢复屏障。 */
int main(void)
{
    ClapRanging a,b;ClapResultMessage tx,duplicate;ClapAckMessage ack;
    ClapRangingSnapshot snapshot;
    /* 每次CLAP冻结测量时温补，新温度版本及真实温变均不能撤值或改写旧结果。 */
    {
        ClapRanging frozen;ClapRanging_Init(&frozen);
        TemperatureSnapshot next_temperature=temperature;
        ClapRanging_Update(&frozen,BOARD_ROLE_A,true,&session,1100000000U,0U,
            &next_temperature,&calibration);
        SoundPairResult first=Pair(1U,1090000000U);
        assert(ClapRanging_PublishPair(&frozen,&first));
        assert(ClapRanging_TakeTx(&frozen,0U,&tx));
        const uint32_t first_id=tx.id;
        ++next_temperature.version;
        ClapRanging_Update(&frozen,BOARD_ROLE_A,true,&session,1100000001U,499U,
            &next_temperature,&calibration);
        ClapRanging_GetSnapshot(&frozen,&snapshot);
        assert(snapshot.has_value && snapshot.result.id==first_id && snapshot.result.distance_mm==355);
        assert(snapshot.result.temperature_version==3U && snapshot.result.sound_speed_mm_s==343000U);
        assert(!ClapRanging_TakeTx(&frozen,499U,&tx));
        assert(ClapRanging_TakeTx(&frozen,500U,&tx) && tx.id==first_id && tx.temperature_version==3U);
        ++next_temperature.version;next_temperature.sound_speed_mm_s=350000U;
        next_temperature.temperature_milli_c=30000;
        ClapRanging_Update(&frozen,BOARD_ROLE_A,true,&session,1100000002U,501U,
            &next_temperature,&calibration);
        ClapRanging_GetSnapshot(&frozen,&snapshot);
        assert(snapshot.has_value && snapshot.fresh && snapshot.result.distance_mm==355);
        assert(snapshot.result.source_common_ns==1091000000U && snapshot.result.temperature_version==3U);
        SoundPairResult second=Pair(2U,1091000000U);
        assert(ClapRanging_PublishPair(&frozen,&second));
        ClapRanging_GetSnapshot(&frozen,&snapshot);
        assert(snapshot.has_value && snapshot.result.id>first_id && snapshot.result.distance_mm==362);
        assert(snapshot.result.sound_speed_mm_s==350000U && snapshot.result.temperature_version==5U);
        assert(snapshot.result.temperature_milli_c==30000);
        ClapRanging_Invalidate(&frozen);
        ClapRanging_GetSnapshot(&frozen,&snapshot);
        assert(!snapshot.has_value && !ClapRanging_TakeTx(&frozen,1000U,&tx));
        assert(!ClapRanging_PublishPair(&frozen,&second));
        SoundPairResult third=Pair(3U,1092000000U);
        assert(ClapRanging_PublishPair(&frozen,&third));
        ClapRanging_GetSnapshot(&frozen,&snapshot);
        assert(snapshot.has_value && snapshot.result.id>first_id);
    }
    ClapRanging_Init(&a); ClapRanging_Init(&b);
    Update(&a,BOARD_ROLE_A,true,1100000000U,0U);
    Update(&b,BOARD_ROLE_B,true,1100000000U,200U);
    SoundPairResult pair=Pair(1U,1090000000U);
    assert(!ClapRanging_PublishPair(&b,&pair));
    assert(ClapRanging_PublishPair(&a,&pair));
    ClapRanging_GetSnapshot(&a,&snapshot);
    assert(snapshot.has_value && snapshot.fresh && snapshot.result.distance_mm==355);
    assert(snapshot.result.sound_speed_mm_s==343000U && snapshot.result.calibration_offset_mm==12);
    assert(snapshot.result.temperature_version==3U && snapshot.result.calibration_version==4U);
    assert(snapshot.result.source_common_ns==1091000000U && snapshot.result.id!=0U);
    assert(!ClapRanging_PublishPair(&a,&pair));
    assert(ClapRanging_TakeTx(&a,0U,&tx));
    assert(!ClapRanging_TakeTx(&a,499U,&duplicate));
    assert(ClapRanging_TakeTx(&a,500U,&duplicate)); /* 首次发送失败也退避。 */
    assert(!ClapRanging_OnMessage(&a,&tx));
    assert(ClapRanging_OnMessage(&b,&tx));
    assert(ClapRanging_TakeAck(&b,&ack));
    ++ack.id;assert(!ClapRanging_OnAck(&a,&ack));--ack.id;
    ++ack.domain;assert(!ClapRanging_OnAck(&a,&ack));--ack.domain;
    ++ack.authority_epoch;assert(!ClapRanging_OnAck(&a,&ack));--ack.authority_epoch;
    ++ack.source_common_ns;assert(!ClapRanging_OnAck(&a,&ack));--ack.source_common_ns;
    assert(ClapRanging_OnAck(&a,&ack));
    assert(!ClapRanging_TakeTx(&a,1000U,&duplicate));
    assert(!ClapRanging_TakeAck(&b,&ack));
    Update(&b,BOARD_ROLE_B,true,UINT64_C(3591000000),1000U);
    assert(ClapRanging_OnMessage(&b,&tx)); /* 重复包ACK，但源已不新鲜。 */
    assert(ClapRanging_TakeAck(&b,&ack));
    ClapRanging_GetSnapshot(&b,&snapshot);
    assert(snapshot.has_value && !snapshot.fresh && snapshot.source_age_ms==2500U);
    Update(&b,BOARD_ROLE_B,true,UINT64_C(7591000000),1100U);
    ClapRanging_GetSnapshot(&b,&snapshot);
    assert(!snapshot.has_value); /* 6.5s，接收毫秒未决定寿命。 */

    /* B失域立即撤；同资格恢复不能用旧重复包复活。 */
    Update(&b,BOARD_ROLE_B,false,UINT64_C(7592000000),1200U);
    assert(!ClapRanging_OnMessage(&b,&tx));
    ClapRanging_GetSnapshot(&b,&snapshot);assert(!snapshot.has_value);
    Update(&b,BOARD_ROLE_B,true,UINT64_C(7593000000),1300U);
    assert(!ClapRanging_OnMessage(&b,&tx));

    /* 新RSC2域/代际只接受source_floor之后的真实声事件。 */
    session.authority_epoch=8U; session.source_floor_ns=8000000000ULL;
    Update(&a,BOARD_ROLE_A,true,UINT64_C(8100000000),2000U);
    Update(&b,BOARD_ROLE_B,true,UINT64_C(8100000000),2000U);
    assert(!ClapRanging_OnMessage(&b,&tx));
    pair=Pair(2U,7999000000ULL);assert(!ClapRanging_PublishPair(&a,&pair));
    pair=Pair(2U,8090000000ULL);assert(ClapRanging_PublishPair(&a,&pair));
    assert(ClapRanging_TakeTx(&a,2000U,&tx));
    assert(ClapRanging_OnMessage(&b,&tx));
    duplicate=tx;duplicate.distance_mm+=1;
    assert(!ClapRanging_OnMessage(&b,&duplicate)); /* 同键不可偷换值。 */
    duplicate=tx;duplicate.source_common_ns-=1U;++duplicate.id;
    assert(!ClapRanging_OnMessage(&b,&duplicate));
    duplicate=tx;duplicate.authority_epoch-=1U;++duplicate.id;
    assert(!ClapRanging_OnMessage(&b,&duplicate));
    duplicate=tx;duplicate.domain+=1U;++duplicate.id;
    assert(!ClapRanging_OnMessage(&b,&duplicate));
    duplicate=tx;duplicate.source_common_ns=8102000001ULL;++duplicate.id;
    assert(!ClapRanging_OnMessage(&b,&duplicate));

    /* 入口限制类型、身份、时差自洽和质量，拒绝不刷新已接受结果。 */
    pair=Pair(3U,8091000000ULL);pair.type=SOUND_EVENT_CHIRP;
    assert(!ClapRanging_PublishPair(&a,&pair));
    pair.type=SOUND_EVENT_CLAP;pair.delta_ns=0;
    assert(!ClapRanging_PublishPair(&a,&pair));
    pair.delta_ns=1000000;pair.quality=101U;
    assert(!ClapRanging_PublishPair(&a,&pair));
    pair.quality=85U;pair.a_session=0U;
    assert(!ClapRanging_PublishPair(&a,&pair));
    pair=Pair(2U,8092000000ULL);assert(!ClapRanging_PublishPair(&a,&pair));
    pair=Pair(3U,8092000000ULL);pair.time_domain=8U;
    assert(!ClapRanging_PublishPair(&a,&pair));

    /* 同资格短暂失健康后，旧接受源和旧id都不能恢复数值。 */
    Update(&b,BOARD_ROLE_B,false,8100000001ULL,2100U);
    Update(&b,BOARD_ROLE_B,true,8100000002ULL,2101U);
    assert(ClapRanging_OnMessage(&b,&tx)); /* 可ACK精确重复，但保持无值。 */
    ClapRanging_GetSnapshot(&b,&snapshot);assert(!snapshot.has_value);

    /* 标定变化撤旧值，新CLAP使用新温补参数，B不使用自己的温度重算。 */
    temperature.sound_speed_mm_s=350000U;++temperature.version;
    calibration.enabled=false;++calibration.version;
    Update(&a,BOARD_ROLE_A,true,8200000000ULL,3000U);
    ClapRanging_GetSnapshot(&a,&snapshot);assert(!snapshot.has_value);
    pair=Pair(3U,8190000000ULL);assert(ClapRanging_PublishPair(&a,&pair));
    assert(ClapRanging_TakeTx(&a,3000U,&tx));
    assert(tx.distance_mm==350 && tx.calibration_offset_mm==0);
    Update(&b,BOARD_ROLE_B,true,8200000000ULL,3000U);
    assert(ClapRanging_OnMessage(&b,&tx));
    temperature.valid=false;
    Update(&b,BOARD_ROLE_B,true,8200000001ULL,3001U);
    ClapRanging_GetSnapshot(&b,&snapshot);assert(snapshot.has_value && snapshot.result.distance_mm==350);
    Update(&a,BOARD_ROLE_A,true,8200000002ULL,3002U);
    ClapRanging_GetSnapshot(&a,&snapshot);
    assert(snapshot.has_value && snapshot.result.distance_mm==350); /* 温度失效不清已发布值。 */
    assert(!ClapRanging_PublishPair(&a,&pair)); /* 只拒绝新发布。 */
    temperature.valid=true;
    Update(&a,BOARD_ROLE_A,true,8200000003ULL,3003U);
    ClapRanging_GetSnapshot(&a,&snapshot);
    assert(snapshot.has_value && snapshot.result.distance_mm==350); /* 新温度不改旧结果。 */
    /* 两板都按真实源期限到期，而不是由A单方提前撤值。 */
    Update(&a,BOARD_ROLE_A,true,14691000000ULL,4000U);
    Update(&b,BOARD_ROLE_B,true,14691000000ULL,4000U);
    ClapRanging_GetSnapshot(&a,&snapshot);assert(!snapshot.has_value);
    ClapRanging_GetSnapshot(&b,&snapshot);assert(!snapshot.has_value);

    /* RST2与CLP1共用标定合同高水位：旧合同包不能复活旧系数结果。 */
    {
        ClapRanging c;ClapRanging_Init(&c);
        Update(&c,BOARD_ROLE_B,true,8200000000ULL,0U);
        ClapResultMessage m=tx;m.id=1U;m.source_common_ns=8191000000U;
        m.calibration_version=2U;m.calibration_offset_mm=0;
        assert(ClapRanging_OnMessage(&c,&m));
        ClapRanging_GetSnapshot(&c,&snapshot);assert(snapshot.has_value);
        ClapResultMessage stale=m;stale.id=2U;stale.source_common_ns+=1000000U;
        stale.calibration_version=1U;stale.calibration_offset_mm=12;
        assert(!ClapRanging_OnMessage(&c,&stale)); /* 旧合同不能写入。 */
        ClapRanging_ObserveCalibration(&c,3U,12);
        ClapRanging_GetSnapshot(&c,&snapshot);
        assert(!snapshot.has_value); /* 新合同撤旧系数值。 */
        ClapRanging_ObserveCalibration(&c,2U,0);
        assert(!ClapRanging_OnMessage(&c,&stale)); /* 迟到旧RST2不降合同。 */
        ClapResultMessage next=m;next.id=3U;next.source_common_ns+=2000000U;
        next.calibration_version=3U;next.calibration_offset_mm=12;
        assert(ClapRanging_OnMessage(&c,&next));
        ClapRanging_GetSnapshot(&c,&snapshot);
        assert(snapshot.has_value && snapshot.result.calibration_version==3U);
    }
    /* A重启换代：B已学合同随新RSC2代际作废，版本回0的新CLP1仍被接受并回ACK。 */
    {
        ClapRanging d;ClapRanging_Init(&d);
        const RangingSessionSnapshot generation={true,9U,7U,UINT64_C(1000000000)};
        ClapRanging_Update(&d,BOARD_ROLE_B,true,&generation,UINT64_C(8200000000),0U,NULL,NULL);
        ClapResultMessage before={0};
        before.domain=9U;before.authority_epoch=7U;before.id=1U;
        before.a_session=2U;before.a_sequence=1U;before.b_session=3U;before.b_sequence=1U;
        before.source_common_ns=UINT64_C(8191000000);before.delta_ns=1000000;
        before.distance_mm=355;before.sound_speed_mm_s=343000U;before.temperature_version=3U;
        before.calibration_version=5U;before.calibration_offset_mm=12;before.quality=85U;
        assert(ClapRanging_OnMessage(&d,&before));
        assert(ClapRanging_TakeAck(&d,&ack));
        ClapResultMessage rolled=before;
        rolled.id=2U;rolled.source_common_ns=UINT64_C(8192000000);
        rolled.calibration_version=0U;rolled.calibration_offset_mm=0;
        assert(!ClapRanging_OnMessage(&d,&rolled)); /* 同代际内旧高水位仍拒收。 */
        /* A重启：新权威代际与源下界，标定版本从0重新开始（RAM-only）。 */
        const RangingSessionSnapshot restarted={true,9U,8U,UINT64_C(8100000000)};
        ClapRanging_Update(&d,BOARD_ROLE_B,true,&restarted,UINT64_C(8210000000),10U,NULL,NULL);
        ClapRanging_GetSnapshot(&d,&snapshot);
        assert(!snapshot.has_value); /* 换代先撤旧展示。 */
        rolled.authority_epoch=restarted.authority_epoch; /* 新CLP1属于新代际。 */
        assert(ClapRanging_OnMessage(&d,&rolled)); /* 合同已清，版本0可接受。 */
        assert(ClapRanging_TakeAck(&d,&ack) && ack.id==rolled.id &&
               ack.source_common_ns==rolled.source_common_ns);
        ClapRanging_GetSnapshot(&d,&snapshot);
        assert(snapshot.has_value && snapshot.result.calibration_version==0U &&
               snapshot.result.distance_mm==355);
    }
    /* 共同时间倒退和0立即撤；毫秒回绕仍保持500ms退避。 */
    ClapRanging_Init(&a);
    Update(&a,BOARD_ROLE_A,true,8200000000ULL,UINT32_MAX-100U);
    assert(ClapRanging_PublishPair(&a,&pair));
    assert(ClapRanging_TakeTx(&a,UINT32_MAX-100U,&tx));
    assert(!ClapRanging_TakeTx(&a,398U,&tx));
    assert(ClapRanging_TakeTx(&a,399U,&tx));
    Update(&a,BOARD_ROLE_A,true,0U,400U);
    assert(!ClapRanging_TakeTx(&a,899U,&tx));
    puts("PASS CLAP: authority, compensation, exact ACK, retry, source expiry, RSC2 barriers, generation-scoped contract");
    return 0;
}
