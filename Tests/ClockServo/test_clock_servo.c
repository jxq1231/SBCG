/**
 * @file        test_clock_servo.c
 * @brief       实际纯C伺服的确定性闭环与边界测试。
 * @details     仿真B时钟的相位和频率，实际调用Observe/Commit，不替换控制算法。
 * @note        主机结果只证明数学控制和状态逻辑，不证明板端网络或MAC硬件。
 */
#include "Sync/clock_servo.h"
#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned assertions;
/** @brief 统计并报告真实断言；失败从当前布尔测试返回false，不修改伺服状态。 */
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition); return false; } } while (0)

/** @brief 提交测试测量并可选模拟动作成功；所有参数为调用者持有值。
 * @param s 非NULL状态。 @param ms 毫秒。 @param ref A纳秒。
 * @param offset B-A纳秒。 @param apply 是否确认执行。
 * @return 本次动作，状态依真实接口更新，无硬件访问。
 */
static ClockServoAction Sample(ClockServo *s, uint32_t ms, uint64_t ref,
    int64_t offset, bool apply)
{
    ClockServoAction a;
    (void)ClockServo_Observe(s,ms,ref,offset*2,200000,&a);
    if (apply && a.kind != CLOCK_SERVO_NONE) ClockServo_Commit(s,ms,&a,true);
    return a;
}

/** @brief 初始三样本后才允许正负相位对齐，提交前不清历史。
 * @return true为测试通过；不访问硬件。
 */
static bool BootstrapAndCommit(void)
{
    const int64_t offsets[] = {INT64_C(3000000000),-INT64_C(3000000000)};
    for (unsigned j=0;j<2;j++)
    {
        ClockServo s; ClockServo_Init(&s,0);
        CHECK(strcmp(ClockServo_StateName(&s),"ACQUIRING")==0);
        CHECK(Sample(&s,0,10000000000,offsets[j],true).kind==CLOCK_SERVO_NONE);
        CHECK(Sample(&s,250,10250000000,offsets[j],true).kind==CLOCK_SERVO_NONE);
        ClockServoAction a=Sample(&s,500,10500000000,offsets[j],false);
        CHECK(a.kind==CLOCK_SERVO_STEP && a.phase_ns==-offsets[j]);
        CHECK(s.valid_samples==3 && s.have_sample && !s.internal_locked);
        CHECK(s.pending.kind==CLOCK_SERVO_STEP);
        ClockServo_Commit(&s,500,&a,true);
        CHECK(!s.have_sample && s.lock_count==0 && s.frequency_ppb==0);
    }
    return true;
}

/** @brief 真实闭环仿真；观测抖动固定种子，频率只由成功提交动作改变。
 * @param initial_ns 初始B-A纳秒。 @param natural_ppb 自然频漂。
 * @param jitter_ns 均匀测量抖动幅度纳秒。 @return true为30秒内收敛且长期保持。
 */
static bool Simulate(int64_t initial_ns,int32_t natural_ppb,int32_t jitter_ns)
{
    ClockServo s; ClockServo_Init(&s,0);
    int64_t phase_ns=initial_ns;
    int32_t applied_ppb=0;
    uint32_t rng=1,locked_ms=0,last_rate_ms=0,longest_rate_ms=0;
    bool saw_lock=false;
    unsigned steps=0,late_locked_samples=0;
    for (uint32_t ms=0;ms<=60000;ms+=250)
    {
        if (ms) phase_ns+=(natural_ppb+applied_ppb)/4;
        rng=rng*UINT32_C(1664525)+UINT32_C(1013904223);
        int64_t jitter=jitter_ns ? (int64_t)(rng%(2u*(uint32_t)jitter_ns+1u))-jitter_ns : 0;
        ClockServoAction a;
        CHECK(ClockServo_Observe(&s,ms,UINT64_C(10000000000)+(uint64_t)ms*1000000,
            (phase_ns+jitter)*2,200000,&a));
        CHECK(s.frequency_ppb==applied_ppb);
        if (a.kind==CLOCK_SERVO_STEP)
        {
            phase_ns+=a.phase_ns; ++steps;
            CHECK(steps==1);
        }
        if (a.kind==CLOCK_SERVO_FREQUENCY)
        {
            CHECK(a.frequency_ppb>=-100000 && a.frequency_ppb<=100000);
            CHECK(ms-last_rate_ms>=1000);
            applied_ppb=a.frequency_ppb;
            if (saw_lock && ms-last_rate_ms>longest_rate_ms) longest_rate_ms=ms-last_rate_ms;
            last_rate_ms=ms;
        }
        if (a.kind!=CLOCK_SERVO_NONE) ClockServo_Commit(&s,ms,&a,true);
        if (s.internal_locked)
        {
            CHECK(s.lock_count>=8 && s.drift_valid);
            CHECK(phase_ns>=-10000 && phase_ns<=10000);
            if (!saw_lock) { saw_lock=true; locked_ms=ms; }
        }
        if (ms>=30000)
        {
            CHECK(phase_ns>=-10000 && phase_ns<=10000);
            if (s.internal_locked) ++late_locked_samples;
        }
    }
    if (60000-last_rate_ms>longest_rate_ms) longest_rate_ms=60000-last_rate_ms;
    CHECK(saw_lock && locked_ms<=30000 && longest_rate_ms>=3000);
    /* 变频后的新分段重新验证漂移；不继承旧分段的八个合格样本。 */
    CHECK(late_locked_samples>=60);
    CHECK(applied_ppb+natural_ppb>=-500 && applied_ppb+natural_ppb<=500);
    printf("SIM initial_ns=%" PRId64 " drift_ppb=%" PRId32 " jitter_ns=%" PRId32
        " lock_ms=%" PRIu32 " final_offset_ns=%" PRId64 " rate_ppb=%" PRId32
        " longest_stable_rate_ms=%" PRIu32 "\n",initial_ns,natural_ppb,jitter_ns,
        locked_ms,phase_ns,applied_ppb,longest_rate_ms);
    return true;
}

/** @brief 启动后或一次近零测量不能提前锁定，锁定要求真实漂移窗口。
 * @return true为测试通过。
 */
static bool LockAndHoldover(void)
{
    ClockServo s; ClockServo_Init(&s,0);
    for(uint32_t ms=0;ms<=2500;ms+=250)
    {
        (void)Sample(&s,ms,UINT64_C(1000000000)+(uint64_t)ms*1000000,0,true);
        if(ms<2500) CHECK(!s.internal_locked);
    }
    (void)Sample(&s,2750,3750000000,0,true);
    CHECK(s.internal_locked && s.lock_count>=8);
    ClockServo_Tick(&s,4749); CHECK(s.internal_locked);
    ClockServo_Tick(&s,4750);
    CHECK(!s.internal_locked && strcmp(ClockServo_StateName(&s),"HOLDOVER")==0);
    ClockServo_Tick(&s,7750);
    CHECK(strcmp(ClockServo_StateName(&s),"UNSYNCED")==0);
    CHECK(Sample(&s,8000,9000000000,0,true).kind==CLOCK_SERVO_NONE);
    CHECK(!s.internal_locked && s.lock_count==0);
    return true;
}

/** @brief 边界、路径异常、参考倒退与tick回绕不产生不安全动作。
 * @return true为测试通过。
 */
static bool RejectAndReacquire(void)
{
    ClockServo s; ClockServoAction a; ClockServo_Init(&s,0);
    CHECK(!ClockServo_Observe(&s,0,1,INT64_MIN,0,&a));
    CHECK(a.kind==CLOCK_SERVO_NONE);
    CHECK(!ClockServo_Observe(&s,0,UINT64_MAX,0,0,&a));
    CHECK(!ClockServo_Observe(&s,0,1,0,-1,&a));
    CHECK(!ClockServo_Observe(&s,0,1,0,4000001,&a));
    CHECK(s.rejected_samples==4);
    CHECK(ClockServo_Observe(&s,0,1000000000,0,4000000,&a));
    CHECK(!ClockServo_Observe(&s,250,999999999,0,0,&a));
    CHECK(!s.internal_locked);
    ClockServo_Invalidate(&s,300);
    CHECK(ClockServo_Observe(&s,300,1000000000,0,0,&a));
    CHECK(!ClockServo_Observe(&s,550,2000000000,0,0,&a));
    ClockServo_Init(&s,UINT32_MAX-500);
    for(uint32_t i=0;i<20;i++)
    {
        uint32_t ms=UINT32_MAX-500+i*250;
        CHECK(ClockServo_Observe(&s,ms,UINT64_C(1000000000)+(uint64_t)i*250000000,0,0,&a));
        CHECK(a.kind==CLOCK_SERVO_NONE);
    }
    CHECK(s.internal_locked);
    CHECK(!ClockServo_Observe(&s,4500,6000000000,0,4000001,&a));
    CHECK(!s.internal_locked && s.lock_count==0);
    return true;
}

/** @brief 单个大离群相位只能触发重新获取，不能立即跳时。
 * @return true为测试通过。
 */
static bool PhaseOutlier(void)
{
    ClockServo s; ClockServo_Init(&s,0);
    for(uint32_t i=0;i<16;i++) (void)Sample(&s,i*250,UINT64_C(1000000000)+(uint64_t)i*250000000,0,true);
    CHECK(s.internal_locked);
    CHECK(Sample(&s,4000,5000000000,5000000,false).kind==CLOCK_SERVO_NONE);
    CHECK(!s.internal_locked);
    CHECK(Sample(&s,4250,5250000000,0,false).kind==CLOCK_SERVO_NONE);
    CHECK(Sample(&s,4500,5500000000,0,false).kind==CLOCK_SERVO_NONE);
    return true;
}

/** @brief 失败或伪造提交永久撤锁，且不得假定频率已生效。
 * @return true为测试通过。
 */
static bool FailedCommit(void)
{
    ClockServo s; ClockServo_Init(&s,0);
    (void)Sample(&s,0,1000000000,2000000,true);
    (void)Sample(&s,250,1250000000,2000000,true);
    ClockServoAction a=Sample(&s,500,1500000000,2000000,false);
    CHECK(a.kind==CLOCK_SERVO_STEP);
    ClockServo_Commit(&s,500,&a,false);
    CHECK(strcmp(ClockServo_StateName(&s),"FAULT")==0);
    CHECK(!ClockServo_Observe(&s,750,1750000000,0,0,&a));
    CHECK(a.kind==CLOCK_SERVO_NONE && s.frequency_ppb==0);
    ClockServo_Invalidate(&s,800); CHECK(s.state==CLOCK_SERVO_FAULT);
    ClockServo_Init(&s,1000);
    a=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,100};
    ClockServo_Commit(&s,1000,&a,true);
    CHECK(s.state==CLOCK_SERVO_FAULT && s.frequency_ppb==0);
    return true;
}

/** @brief 频率限幅、未提交请求及真实命令跨毫秒的确认行为。
 * @return true为测试通过；模拟成功提交不代替硬件证明。
 */
static bool FrequencyCommitAndCap(void)
{
    for(int sign=-1;sign<=1;sign+=2)
    {
        ClockServo s; ClockServo_Init(&s,0); ClockServoAction a={0};
        for(uint32_t ms=0;ms<=1000;ms+=250)
            a=Sample(&s,ms,UINT64_C(1000000000)+(uint64_t)ms*1000000,
                (int64_t)sign*200000*ms/1000,false);
        CHECK(a.kind==CLOCK_SERVO_FREQUENCY && a.frequency_ppb==-sign*100000);
        CHECK(s.frequency_ppb==0);
        ClockServoAction other;
        CHECK(!ClockServo_Observe(&s,1001,2001000000,0,0,&other));
        CHECK(other.kind==CLOCK_SERVO_NONE && s.pending.kind==CLOCK_SERVO_FREQUENCY);
        ClockServo_Commit(&s,1020,&a,true);
        CHECK(s.state!=CLOCK_SERVO_FAULT && s.frequency_ppb==-sign*100000);
        ClockServo_Invalidate(&s,1100);
        CHECK(s.frequency_ppb==-sign*100000 && s.bootstrap_count==0 && !s.drift_valid);
        CHECK(Sample(&s,1100,2100000000,0,false).kind==CLOCK_SERVO_NONE);
    }
    ClockServo s; ClockServo_Init(&s,0);
    for(uint32_t ms=0;ms<1000;ms+=250)
        (void)Sample(&s,ms,UINT64_C(1000000000)+(uint64_t)ms*1000000,(int64_t)ms*50,false);
    ClockServoAction a=Sample(&s,1000,2000000000,50000,false);
    CHECK(a.kind==CLOCK_SERVO_FREQUENCY && s.frequency_ppb==0);
    ClockServo_Commit(&s,1000,&a,false);
    CHECK(s.state==CLOCK_SERVO_FAULT && s.frequency_ppb==0);
    return true;
}

/** @brief 短时丢包仍沿连续参考估计，长时丢包只保持已确认频率。
 * @return true为测试通过。
 */
static bool PacketLossAndLimits(void)
{
    ClockServo s; ClockServo_Init(&s,0); ClockServoAction a;
    for(uint32_t i=0;i<16;i++) (void)Sample(&s,i*250,UINT64_C(1000000000)+(uint64_t)i*250000000,0,true);
    CHECK(s.internal_locked);
    CHECK(ClockServo_Observe(&s,5000,6000000000,0,0,&a));
    CHECK(s.internal_locked);
    ClockServo_Tick(&s,7000); CHECK(s.state==CLOCK_SERVO_HOLDOVER && !s.internal_locked);
    CHECK(ClockServo_Observe(&s,7250,8250000000,0,0,&a));
    CHECK(s.lock_count==0 && !s.drift_valid);
    ClockServo_Init(&s,0);
    CHECK(ClockServo_Observe(&s,0,0,INT64_C(8589934591999999998),0,&a));
    CHECK(ClockServo_Observe(&s,250,250000000,-INT64_C(8589934591999999998),0,&a));
    CHECK(a.kind==CLOCK_SERVO_NONE);
    CHECK(!ClockServo_Observe(&s,500,500000000,INT64_MAX,0,&a));
    CHECK(!ClockServo_Observe(NULL,0,0,0,0,&a));
    CHECK(!ClockServo_Observe(&s,0,0,0,0,NULL));
    ClockServo_Init(NULL,0); ClockServo_Tick(NULL,0); ClockServo_Invalidate(NULL,0);
    ClockServo_Commit(NULL,0,NULL,false);
    CHECK(strcmp(ClockServo_StateName(NULL),"FAULT")==0);
    return true;
}

/** @brief 半纳秒边界不因整数截断放宽锁定阈值，本地tick倒退拒绝。
 * @return true为测试通过。
 */
static bool ExactThresholdAndBackwardTick(void)
{
    ClockServo s; ClockServoAction a; ClockServo_Init(&s,0);
    /* 不提交频率，测试中始终保持此相位；观察阈值是否误报锁定。 */
    for(uint32_t ms=0;ms<=2750;ms+=250)
    {
        if(s.pending.kind!=CLOCK_SERVO_NONE) break;
        CHECK(ClockServo_Observe(&s,ms,UINT64_C(1000000000)+(uint64_t)ms*1000000,
            20001,0,&a));
        CHECK(!s.internal_locked);
    }
    ClockServo_Init(&s,1000);
    CHECK(ClockServo_Observe(&s,1000,1000000000,0,0,&a));
    CHECK(!ClockServo_Observe(&s,900,1100000000,0,0,&a));
    CHECK(a.kind==CLOCK_SERVO_NONE && !s.internal_locked);
    return true;
}

/** @brief 重新初始化必须采用已确认非零硬件频率，越界不能继续发动作。
 * @return true为测试通过；输入频率是假定已由外部BSP确认的测试值。
 */
static bool InitWithKnownFrequency(void)
{
    ClockServo s; ClockServoAction a;
    const int32_t rates[]={-100000,-50000,50000,100000};
    for(unsigned j=0;j<sizeof(rates)/sizeof(rates[0]);j++)
    {
        CHECK(ClockServo_InitKnown(&s,0,rates[j]));
        CHECK(s.state==CLOCK_SERVO_ACQUIRING && s.frequency_ppb==rates[j]);
        for(uint32_t ms=0;ms<=3000;ms+=250)
        {
            CHECK(ClockServo_Observe(&s,ms,UINT64_C(1000000000)+(uint64_t)ms*1000000,0,0,&a));
            CHECK(a.kind==CLOCK_SERVO_NONE && s.frequency_ppb==rates[j]);
        }
        CHECK(s.internal_locked);
    }
    CHECK(!ClockServo_InitKnown(&s,0,100001));
    CHECK(s.state==CLOCK_SERVO_FAULT);
    CHECK(!ClockServo_Observe(&s,0,0,0,0,&a) && a.kind==CLOCK_SERVO_NONE);
    CHECK(!ClockServo_InitKnown(&s,0,-100001) && s.state==CLOCK_SERVO_FAULT);
    CHECK(!ClockServo_InitKnown(NULL,0,0));
    CHECK(ClockServo_InitKnown(&s,1000,12345) && s.frequency_ppb==12345);
    ClockServo_Invalidate(&s,1100); CHECK(s.frequency_ppb==12345);
    ClockServo_Init(&s,1200); CHECK(s.frequency_ppb==0);
    return true;
}

/**
 * @brief 未完成动作的过期、错配、失效、超时和空确认均不能假定成功。
 * @return true表示五条故障路径均禁止后续动作。
 * @note 全部调用实际纯C状态机，不向硬件提交命令。
 */
static bool PendingActionSafety(void)
{
    for(unsigned mode=0;mode<5;mode++)
    {
        ClockServo s; ClockServo_Init(&s,0);
        (void)Sample(&s,0,1000000000,2000000,true);
        (void)Sample(&s,250,1250000000,2000000,true);
        ClockServoAction a=Sample(&s,500,1500000000,2000000,false);
        CHECK(a.kind==CLOCK_SERVO_STEP);
        switch(mode)
        {
        case 0: ClockServo_Commit(&s,601,&a,true); break;
        case 1: ++a.phase_ns; ClockServo_Commit(&s,500,&a,true); break;
        case 2: ClockServo_Invalidate(&s,501); break;
        case 3: ClockServo_Tick(&s,2500); break;
        default: ClockServo_Commit(&s,500,NULL,true); break;
        }
        CHECK(s.state==CLOCK_SERVO_FAULT && !s.internal_locked && s.frequency_ppb==0);
        CHECK(!ClockServo_Observe(&s,3000,4000000000,0,0,&a));
        CHECK(a.kind==CLOCK_SERVO_NONE);
    }
    return true;
}

/**
 * @brief 持续质量拒绝不能伪造样本新鲜度，覆盖tick回绕及正常恢复。
 * @return true表示真实最后有效样本控制2秒/5秒状态，恢复重新积累证据。
 * @note 采用已确认非零频率验证拒绝不会重置实际控制值，无硬件访问。
 */
static bool RejectedMeasurementTimeout(void)
{
    const uint32_t starts[]={0,UINT32_MAX-4000};
    for(unsigned j=0;j<sizeof(starts)/sizeof(starts[0]);j++)
    {
        ClockServo s; ClockServoAction a;
        CHECK(ClockServo_InitKnown(&s,starts[j],12345));
        for(uint32_t ms=0;ms<=3000;ms+=250)
        {
            CHECK(ClockServo_Observe(&s,starts[j]+ms,
                UINT64_C(1000000000)+(uint64_t)ms*1000000,0,0,&a));
            CHECK(a.kind==CLOCK_SERVO_NONE);
        }
        CHECK(s.internal_locked);
        const uint32_t last_valid_ms=starts[j]+3000;
        const uint32_t valid_count=s.valid_samples;
        for(uint32_t elapsed_ms=250;elapsed_ms<=5500;elapsed_ms+=250)
        {
            ClockServo_RejectMeasurement(&s,last_valid_ms+elapsed_ms);
            ClockServo_Tick(&s,last_valid_ms+elapsed_ms);
            CHECK(s.last_sample_ms==last_valid_ms && s.valid_samples==valid_count);
            CHECK(s.rejected_samples==elapsed_ms/250);
            CHECK(!s.internal_locked && s.lock_count==0 && !s.have_sample && !s.have_anchor && !s.drift_valid);
            CHECK(s.frequency_ppb==12345);
            if(elapsed_ms<2000) CHECK(s.state==CLOCK_SERVO_ACQUIRING);
            else if(elapsed_ms<5000) CHECK(s.state==CLOCK_SERVO_HOLDOVER);
            else CHECK(s.state==CLOCK_SERVO_UNSYNCED);
        }
        for(uint32_t ms=0;ms<=3000;ms+=250)
        {
            CHECK(ClockServo_Observe(&s,last_valid_ms+5750+ms,
                UINT64_C(9750000000)+(uint64_t)ms*1000000,0,0,&a));
            CHECK(s.last_sample_ms==last_valid_ms+5750+ms && s.frequency_ppb==12345);
            if(ms<2750) CHECK(!s.internal_locked);
            CHECK(a.kind==CLOCK_SERVO_NONE);
        }
        CHECK(s.internal_locked);
    }
    ClockServo s; ClockServo_Init(&s,0);
    (void)Sample(&s,0,1000000000,2000000,true);
    (void)Sample(&s,250,1250000000,2000000,true);
    ClockServoAction a=Sample(&s,500,1500000000,2000000,false);
    CHECK(a.kind==CLOCK_SERVO_STEP);
    ClockServo_RejectMeasurement(&s,501);
    CHECK(s.state==CLOCK_SERVO_FAULT && s.last_sample_ms==500 && s.rejected_samples==1);
    ClockServo_RejectMeasurement(&s,6000);
    CHECK(s.state==CLOCK_SERVO_FAULT && s.last_sample_ms==500 && s.rejected_samples==1);
    ClockServo_RejectMeasurement(NULL,0);
    return true;
}

/** @brief 驱动所有真实核心测试并输出断言数。
 * @return 0表示全通过，1表示首个断言失败。
 */
int main(void)
{
    if(!BootstrapAndCommit() || !LockAndHoldover() || !RejectAndReacquire() ||
       !PhaseOutlier() || !FailedCommit() || !FrequencyCommitAndCap() ||
       !PacketLossAndLimits() || !ExactThresholdAndBackwardTick() ||
       !InitWithKnownFrequency() || !PendingActionSafety() ||
       !RejectedMeasurementTimeout()) return 1;
    const int64_t offsets[]={INT64_C(3000000000),-INT64_C(2000000000),0,999999,-999999};
    const int32_t drifts[]={50000,-50000};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) for(unsigned j=0;j<2;j++)
        if(!Simulate(offsets[i],drifts[j],100)) return 1;
    printf("PASS ClockServo assertions=%u (host mathematical simulation only)\n",assertions);
    return 0;
}
