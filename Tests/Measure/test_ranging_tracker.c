#include "Measure/ranging_tracker.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)

static RangingTracker tracker;
static RangingTrackerContext context;
static uint32_t next_a_sequence=1U;
static uint32_t next_b_sequence=1U;
static uint32_t last_a_sequence;
static uint32_t last_b_sequence;

static int64_t DeltaForMm(int32_t mm)
{
    const int64_t magnitude=mm<0?-(int64_t)mm:(int64_t)mm;
    const int64_t delta=(magnitude*1000000000LL+343000LL/2LL)/343000LL;
    return mm<0?-delta:delta;
}

/**
 * @brief 构造一个独立、同域、同会话的合成事件；源时刻严格递增。
 * @param mm 期望的有符号声程差毫米值。
 * @param source_ms 源时间相对1s基准的毫秒偏移。
 * @param out 非NULL输出事件。
 * @return 无返回值；a/b会话和序号由测试递增。
 */
static void MakeObservation(int32_t mm,uint32_t source_ms,RangingObservation *out)
{
    memset(out,0,sizeof *out);
    out->time_domain=context.time_domain;
    out->a_session=context.a_session;
    out->b_session=context.b_session;
    out->a_sequence=next_a_sequence++;
    out->b_sequence=next_b_sequence++;
    out->delta_ns=DeltaForMm(mm);
    out->a_time_ns=1000000000ULL+(uint64_t)source_ms*1000000ULL;
    out->b_time_ns=(uint64_t)((int64_t)out->a_time_ns+out->delta_ns);
    out->proxy_quality=98U;
    out->evidence_kind=RANGING_EVIDENCE_PROXY;
    out->geometry_assumed_valid=true;
    last_a_sequence=out->a_sequence;
    last_b_sequence=out->b_sequence;
}

static RangingDecision PushMm(int32_t mm,uint32_t source_ms)
{
    RangingObservation observation;
    MakeObservation(mm,source_ms,&observation);
    return RangingTracker_Push(&tracker,&observation,
        (uint32_t)(1000U+source_ms),(uint64_t)1000000000ULL+
        (uint64_t)source_ms*1000000ULL);
}

static RangingTrackerSnapshot Snapshot(void)
{
    RangingTrackerSnapshot snapshot;
    RangingTracker_Get(&tracker,&snapshot);
    return snapshot;
}

static void Setup(void)
{
    RangingTrackerConfig config=RangingTracker_DefaultConfig();
    config.sound_speed_mm_s=343000U;
    context=(RangingTrackerContext){
        .time_domain=7U,.a_session=11U,.b_session=12U,.a_clock_epoch=3U,
        .domain_valid=true,.stream_valid=true,.temperature_valid=true};
    RangingTracker_Init(&tracker,&config);
    RangingTracker_SetContext(&tracker,&context,1000U,1000000000ULL);
    next_a_sequence=1U;next_b_sequence=1U;
}

int main(void)
{
    Setup();
    assert(PushMm(315,0U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(314,2000U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(316,4000U)==RANGING_DECISION_ACCEPT);
    RangingTrackerSnapshot snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_TRACKING && snapshot.has_value && snapshot.fresh);
    assert(snapshot.distance_mm==315 && snapshot.direction==1);
    assert(snapshot.source_common_ns==5000921283ULL);
    const uint32_t confirmed_id=snapshot.measurement_id;

    /* 单次离群只保持旧值，不刷新源时间、ID或接受窗口。 */
    assert(PushMm(245,6000U)==RANGING_DECISION_HOLD);
    snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_HOLD && snapshot.has_value && !snapshot.fresh);
    assert(snapshot.distance_mm==315 && snapshot.measurement_id==confirmed_id);
    assert(snapshot.source_common_ns==5000921283ULL);
    assert(PushMm(245,8000U)==RANGING_DECISION_REACQUIRE);
    snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_REACQUIRE && snapshot.distance_mm==315);
    assert(PushMm(245,10000U)==RANGING_DECISION_ACCEPT);
    snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_TRACKING && snapshot.distance_mm==245);
    assert(snapshot.measurement_id!=confirmed_id);

    /* 真正的反向变化同样需要3个独立一致事件。 */
    assert(PushMm(315,12000U)==RANGING_DECISION_HOLD);
    assert(PushMm(315,14000U)==RANGING_DECISION_REACQUIRE);
    assert(PushMm(315,16000U)==RANGING_DECISION_ACCEPT);
    snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_TRACKING && snapshot.distance_mm==315);

    /* 同一源事件重复到达不能累计确认或刷新ID。 */
    RangingObservation same;
    memset(&same,0,sizeof same);
    same.time_domain=context.time_domain;
    same.a_session=context.a_session;
    same.b_session=context.b_session;
    same.a_sequence=last_a_sequence;
    same.b_sequence=last_b_sequence;
    same.a_time_ns=1000000000ULL+16000ULL*1000000ULL;
    same.b_time_ns=(uint64_t)((int64_t)same.a_time_ns+DeltaForMm(315));
    same.delta_ns=DeltaForMm(315);
    same.proxy_quality=98U;
    same.evidence_kind=RANGING_EVIDENCE_PROXY;
    same.geometry_assumed_valid=true;
    const uint32_t before_duplicate=Snapshot().measurement_id;
    assert(RangingTracker_Push(&tracker,&same,1000U,1000000000ULL)!=RANGING_DECISION_ACCEPT);
    assert(Snapshot().measurement_id==before_duplicate);

    /* 候选源间隔超限时只重建候选，不清除已确认簇。 */
    Setup();
    assert(PushMm(315,0U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(314,2000U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(316,4000U)==RANGING_DECISION_ACCEPT);
    assert(PushMm(245,6000U)==RANGING_DECISION_HOLD);
    assert(PushMm(245,9000U)==RANGING_DECISION_HOLD);
    snapshot=Snapshot();
    assert(snapshot.candidate_count==1U && snapshot.distance_mm==315);

    /* 域硬失效立即撤值；同域健康恢复后重新ACQUIRING，不复活旧值。 */
    RangingTrackerContext invalid=context;invalid.domain_valid=false;
    RangingTracker_SetContext(&tracker,&invalid,11000U,11000000000ULL);
    snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_FAULT && !snapshot.has_value);
    RangingTracker_SetContext(&tracker,&context,12000U,12000000000ULL);
    snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_ACQUIRING && !snapshot.has_value);

    /* 单次离群后回旧簇：不换窗，但新独立事件产生新ID。 */
    Setup();
    assert(PushMm(315,0U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(314,2000U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(316,4000U)==RANGING_DECISION_ACCEPT);
    const uint32_t old_cluster_id=Snapshot().measurement_id;
    assert(PushMm(245,6000U)==RANGING_DECISION_HOLD);
    assert(PushMm(315,8000U)==RANGING_DECISION_ACCEPT);
    snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_TRACKING && snapshot.distance_mm==315);
    assert(snapshot.measurement_id!=old_cluster_id);

    /* 245/385/254互不一致不能强制换簇。 */
    Setup();
    assert(PushMm(315,0U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(314,2000U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(316,4000U)==RANGING_DECISION_ACCEPT);
    assert(PushMm(245,6000U)==RANGING_DECISION_HOLD);
    assert(PushMm(385,8000U)==RANGING_DECISION_HOLD);
    assert(PushMm(254,10000U)==RANGING_DECISION_HOLD);
    snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_HOLD && snapshot.distance_mm==315);
    assert(snapshot.candidate_count==1U);

    /* 初始三点不一致不能确认错误簇。 */
    Setup();
    assert(PushMm(315,0U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(245,2000U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(316,4000U)==RANGING_DECISION_CANDIDATE);
    snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_ACQUIRING && !snapshot.has_value);

    /* 负delta给出B侧方向，仍以绝对值发布距离。 */
    Setup();
    assert(PushMm(-315,0U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(-314,2000U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(-316,4000U)==RANGING_DECISION_ACCEPT);
    snapshot=Snapshot();
    assert(snapshot.direction==2 && snapshot.distance_mm==315);

    /* 显示硬截止前先HOLD，6.5秒后STALE无数字。 */
    Setup();
    assert(PushMm(315,0U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(314,2000U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(316,4000U)==RANGING_DECISION_ACCEPT);
    RangingTracker_Tick(&tracker,8000U,8000000000ULL);
    snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_HOLD && snapshot.has_value && !snapshot.fresh);
    RangingTracker_Tick(&tracker,12000U,12000000000ULL);
    snapshot=Snapshot();
    assert(snapshot.state==RANGING_STATE_STALE && !snapshot.has_value);

    puts("PASS ranging tracker: confirm/hold/reacquire/switch, identity, candidate gap, inconsistent clusters, direction, fault and stale");
    return 0;
}
