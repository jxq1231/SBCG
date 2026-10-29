/**
 * @file        test_ranging_tracker.c
 * @brief       纯跟踪器状态、身份、算术及时间边界的主机回归。
 * @details     使用独立合成声事件直接调用公开接口，覆盖确认、保持与硬恢复。
 * @note        无硬件操作；测试纳秒值只验证软件合同，不代表声学精度。
 */
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

/** @brief 验证既有确认、保持、方向、失效和过期路径；无返回值。 */
static void TestLegacy(void)
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
}

/**
 * @brief 构造指定有符号纳秒差的事件，递增两侧身份。
 * @param delta_ns 本次B减A时间差，单位ns。
 * @param source_ms A时刻相对1s的毫秒偏移。
 * @return 返回按值拥有的合成事件。
 */
static RangingObservation MakeDelta(int64_t delta_ns,uint32_t source_ms)
{
    RangingObservation observation;
    MakeObservation(0,source_ms,&observation);
    observation.delta_ns=delta_ns;
    observation.b_time_ns=delta_ns<0 ?
        observation.a_time_ns-(uint64_t)(-(delta_ns+1))-1U :
        observation.a_time_ns+(uint64_t)delta_ns;
    return observation;
}

/**
 * @brief 在真实共同读时不早于源的条件下提交事件。
 * @param observation 非NULL，只在调用期间借用的输入。
 * @param recv_ms 可回绕的前台接收毫秒计数。
 * @return 返回真实跟踪器决策。
 */
static RangingDecision PushAt(const RangingObservation *observation,uint32_t recv_ms)
{
    const uint64_t common_ns=observation->a_time_ns>observation->b_time_ns ?
        observation->a_time_ns:observation->b_time_ns;
    return RangingTracker_Push(&tracker,observation,recv_ms,common_ns);
}

/** @brief 验证完整候选跨度及能够保留的最新一致后缀；无返回值。 */
static void TestSpan(void)
{
    Setup();
    const int64_t values[]={900000,920000,880000};
    for(uint32_t i=0U;i<3U;++i)
    {
        RangingObservation observation=MakeDelta(values[i],i*2000U);
        assert(PushAt(&observation,1000U+i*2000U)==RANGING_DECISION_CANDIDATE);
    }
    assert(!Snapshot().has_value && Snapshot().candidate_count==1U);
    Setup();
    const int64_t suffix[]={900000,920000,930000,925000};
    for(uint32_t i=0U;i<4U;++i)
    {
        RangingObservation observation=MakeDelta(suffix[i],i*2000U);
        assert(PushAt(&observation,1000U+i*2000U)==
            (i==3U?RANGING_DECISION_ACCEPT:RANGING_DECISION_CANDIDATE));
    }
    assert(Snapshot().filtered_delta_ns==925000);
}

/** @brief 验证跨零换向与近零候选也要独立三声确认；无返回值。 */
static void TestDirection(void)
{
    Setup();
    assert(PushMm(5,0U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(5,2000U)==RANGING_DECISION_CANDIDATE);
    assert(PushMm(5,4000U)==RANGING_DECISION_ACCEPT);
    assert(PushMm(-5,6000U)==RANGING_DECISION_HOLD);
    assert(Snapshot().direction==1);
    assert(PushMm(-5,8000U)==RANGING_DECISION_REACQUIRE);
    assert(PushMm(-5,10000U)==RANGING_DECISION_ACCEPT);
    assert(Snapshot().direction==2);
    Setup();
    for(uint32_t i=0U;i<3U;++i)
    {
        RangingObservation observation=MakeDelta(i==1U?-1000:1000,i*2000U);
        assert(PushAt(&observation,1000U+i*2000U)==RANGING_DECISION_CANDIDATE);
    }
    assert(!Snapshot().has_value);
}

/** @brief 验证超过16项后同上下文硬恢复也不重用任一侧身份；无返回值。 */
static void TestHighwater(void)
{
    Setup();
    for(uint32_t i=0U;i<20U;++i) { (void)PushMm(315,i*2000U); }
    RangingTracker_Invalidate(&tracker,RANGING_INVALIDATE_STREAM,40000U);
    RangingTracker_SetContext(&tracker,&context,40000U,40000000000ULL);
    for(uint32_t i=0U;i<3U;++i)
    {
        RangingObservation replay=MakeDelta(900000,40000U+i*2000U);
        replay.a_sequence=i+1U;replay.b_sequence=i+1U;
        assert(PushAt(&replay,41000U+i*2000U)==RANGING_DECISION_REJECT);
    }
    assert(!Snapshot().has_value && Snapshot().candidate_count==0U);
    RangingObservation repeated=MakeDelta(900000,48000U);
    repeated.a_sequence=20U;
    assert(PushAt(&repeated,49000U)==RANGING_DECISION_REJECT);
    repeated.a_sequence=next_a_sequence++;
    repeated.b_sequence=20U;
    assert(PushAt(&repeated,49000U)==RANGING_DECISION_REJECT);
    ++context.a_session;
    RangingTracker_SetContext(&tracker,&context,50000U,50000000000ULL);
    repeated=MakeDelta(900000,50000U);
    repeated.a_sequence=1U;repeated.b_sequence=20U;
    assert(PushAt(&repeated,51000U)==RANGING_DECISION_REJECT);
    repeated.b_sequence=21U;
    assert(PushAt(&repeated,51000U)==RANGING_DECISION_CANDIDATE);
    ++context.b_session;
    RangingTracker_SetContext(&tracker,&context,52000U,52000000000ULL);
    repeated=MakeDelta(900000,52000U);
    repeated.a_sequence=2U;repeated.b_sequence=1U;
    assert(PushAt(&repeated,53000U)==RANGING_DECISION_CANDIDATE);
    repeated=MakeDelta(900000,54000U);
    repeated.a_session=context.a_session-1U;
    assert(PushAt(&repeated,55000U)==RANGING_DECISION_REJECT);
    assert(Snapshot().candidate_count==1U);
    ++context.time_domain;
    RangingTracker_SetContext(&tracker,&context,56000U,56000000000ULL);
    repeated=MakeDelta(900000,56000U);
    repeated.a_sequence=1U;repeated.b_sequence=1U;
    assert(PushAt(&repeated,57000U)==RANGING_DECISION_CANDIDATE);
}

/** @brief 验证纳秒差、距离乘加、截止加法和质量字段的输入边界；无返回值。 */
static void TestArithmetic(void)
{
    Setup();
    RangingObservation observation=MakeDelta(INT64_C(1)<<61,0U);
    assert(PushAt(&observation,1000U)==RANGING_DECISION_REJECT);
    Setup();
    observation=MakeDelta(DeltaForMm(10001),0U);
    assert(PushAt(&observation,1000U)==RANGING_DECISION_REJECT);
    Setup();
    observation=MakeDelta(INT64_MAX,0U);
    assert(PushAt(&observation,1000U)==RANGING_DECISION_REJECT);
    Setup();
    observation=MakeDelta(0,0U);
    observation.a_time_ns=UINT64_MAX;observation.b_time_ns=1U;
    observation.delta_ns=2;
    assert(RangingTracker_Push(&tracker,&observation,1000U,UINT64_MAX)==RANGING_DECISION_REJECT);
    Setup();
    observation=MakeDelta(0,0U);
    observation.a_time_ns=(UINT64_C(1)<<63)+1U;observation.b_time_ns=1U;
    observation.delta_ns=INT64_MIN;
    assert(PushAt(&observation,1000U)==RANGING_DECISION_REJECT);
    Setup();
    observation=MakeDelta(0,0U);
    observation.a_time_ns=UINT64_MAX-1000U;
    observation.b_time_ns=observation.a_time_ns;
    assert(PushAt(&observation,1000U)==RANGING_DECISION_REJECT);
    assert(!Snapshot().has_value && Snapshot().candidate_count==0U);
    Setup();
    observation=MakeDelta(900000,0U);observation.proxy_quality=101U;
    assert(PushAt(&observation,1000U)==RANGING_DECISION_REJECT);
    Setup();
    observation=MakeDelta(900000,0U);observation.a_sequence=0U;
    assert(PushAt(&observation,1000U)==RANGING_DECISION_REJECT);
    Setup();
    RangingTrackerConfig bad=RangingTracker_DefaultConfig();
    bad.accepted_window=1U;
    RangingTracker_Init(&tracker,&bad);
    assert(Snapshot().state==RANGING_STATE_FAULT);
    Setup();
    for(uint32_t i=0U;i<3U;++i)
    {
        observation=MakeDelta(2000,i*2000U);
        observation.a_time_ns=(uint64_t)INT64_MAX-1000U+(uint64_t)i*2000000000ULL;
        observation.b_time_ns=observation.a_time_ns+2000U;
        assert(PushAt(&observation,1000U+i*2000U)==
            (i==2U?RANGING_DECISION_ACCEPT:RANGING_DECISION_CANDIDATE));
    }
    assert(Snapshot().filtered_delta_ns==2000);
}

/** @brief 验证新声速不得使旧候选带入越量程支持窗口；无返回值。 */
static void TestSoundSpeed(void)
{
    Setup();
    RangingObservation observation=MakeDelta(DeltaForMm(9000),0U);
    assert(PushAt(&observation,1000U)==RANGING_DECISION_CANDIDATE);
    RangingTracker_SetSoundSpeed(&tracker,1000000U);
    assert(Snapshot().candidate_count==0U && !Snapshot().has_value);
    Setup();
    for(uint32_t i=0U;i<3U;++i)
    {
        observation=MakeDelta(DeltaForMm(9000),i*2000U);
        (void)PushAt(&observation,1000U+i*2000U);
    }
    RangingTracker_SetSoundSpeed(&tracker,UINT32_MAX);
    assert(Snapshot().state==RANGING_STATE_FAULT && !Snapshot().has_value);
}

/** @brief 验证共同读时失败或倒退立即撤值，并保留消费身份；无返回值。 */
static void TestClock(void)
{
    Setup();
    for(uint32_t i=0U;i<3U;++i) { (void)PushMm(315,i*2000U); }
    RangingTracker_Tick(&tracker,6000U,0U);
    assert(Snapshot().state==RANGING_STATE_FAULT && !Snapshot().has_value);
    RangingTracker_SetContext(&tracker,&context,7000U,7000000000ULL);
    for(uint32_t i=0U;i<3U;++i) { (void)PushMm(315,8000U+i*2000U); }
    RangingTracker_Tick(&tracker,14000U,12000000000ULL);
    assert(Snapshot().state==RANGING_STATE_FAULT && !Snapshot().has_value);
    Setup();
    RangingObservation observation=MakeDelta(900000,0U);
    assert(RangingTracker_Push(&tracker,&observation,1000U,0U)==RANGING_DECISION_FAULT);
    assert(!Snapshot().has_value);
}

/** @brief 验证候选接收超时、200ms抖动、毫秒回绕和4s旧簇返回；无返回值。 */
static void TestReceiveGap(void)
{
    Setup();
    for(uint32_t i=0U;i<3U;++i)
    {
        RangingObservation observation=MakeDelta(900000,i*2000U);
        assert(PushAt(&observation,1000U+i*2600U)==RANGING_DECISION_CANDIDATE);
    }
    assert(Snapshot().candidate_count==1U);
    Setup();
    for(uint32_t i=0U;i<3U;++i)
    {
        RangingObservation observation=MakeDelta(900000,i*2000U);
        const uint32_t recv_ms=i==1U?2800U:1200U+i*2000U;
        assert(PushAt(&observation,recv_ms)==
            (i==2U?RANGING_DECISION_ACCEPT:RANGING_DECISION_CANDIDATE));
    }
    assert(PushMm(315,8000U)==RANGING_DECISION_ACCEPT);
    Setup();
    for(uint32_t i=0U;i<3U;++i)
    {
        RangingObservation observation=MakeDelta(900000,i*2000U);
        assert(PushAt(&observation,UINT32_MAX-1000U+i*2000U)==
            (i==2U?RANGING_DECISION_ACCEPT:RANGING_DECISION_CANDIDATE));
    }
}

/** @brief 验证质量/证据只来自实际支持窗口，淘汰低分后允许恢复；无返回值。 */
static void TestQuality(void)
{
    Setup();
    for(uint32_t i=0U;i<3U;++i)
    {
        RangingObservation observation=MakeDelta(900000,i*2000U);
        observation.proxy_quality=i==1U?70U:95U;
        observation.evidence_kind=RANGING_EVIDENCE_CODED;
        (void)PushAt(&observation,1000U+i*2000U);
    }
    assert(Snapshot().quality==70U && Snapshot().evidence_kind==RANGING_EVIDENCE_CODED);
    RangingObservation outlier=MakeDelta(700000,6000U);
    outlier.proxy_quality=50U;outlier.evidence_kind=RANGING_EVIDENCE_PROXY;
    assert(PushAt(&outlier,7000U)==RANGING_DECISION_HOLD);
    assert(Snapshot().quality==70U && Snapshot().evidence_kind==RANGING_EVIDENCE_CODED);
    for(uint32_t i=0U;i<5U;++i)
    {
        RangingObservation observation=MakeDelta(900000,8000U+i*2000U);
        observation.proxy_quality=95U;observation.evidence_kind=RANGING_EVIDENCE_MATCHED;
        assert(PushAt(&observation,9000U+i*2000U)==RANGING_DECISION_ACCEPT);
    }
    assert(Snapshot().quality==95U && Snapshot().evidence_kind==RANGING_EVIDENCE_MATCHED);
}

/**
 * @brief 逐组运行跟踪器回归，可按名称单独复现RED。
 * @param argc 进程参数数量。
 * @param argv 参数仅在本函数内借用；第二项可选测试名。
 * @return 成功返回0；未知名称返回2，断言失败立即返回非零。
 */
int main(int argc,char **argv)
{
    const struct {const char *name;void (*run)(void);} cases[]={
        {"span",TestSpan},{"direction",TestDirection},{"highwater",TestHighwater},
        {"arithmetic",TestArithmetic},{"sound_speed",TestSoundSpeed},{"clock",TestClock},
        {"receive_gap",TestReceiveGap},{"quality",TestQuality},{"legacy",TestLegacy}};
    bool found=false;
    for(size_t i=0U;i<sizeof cases/sizeof cases[0];++i)
    {
        if(argc>1 && strcmp(argv[1],cases[i].name)!=0) { continue; }
        cases[i].run();found=true;
        printf("PASS tracker %s\n",cases[i].name);
    }
    printf("sizeof RangingTracker=%zu\n",sizeof tracker);
    return found?0:2;
}
