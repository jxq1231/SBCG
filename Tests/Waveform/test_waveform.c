/** @file test_waveform.c @brief 验证真实借用交接、共同时间窗、拒绝旧域与固定内存。 */
#include "UI/waveform_service.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS];
static uint32_t noise_seed=1;
static double tone_period_frames=96;
static RangingSessionSnapshot domain = {true, 7, 9, 1};
/** @brief 提交连续真实PCM，phase为弧度；End失败由调用方指定。 */
static void Feed(uint64_t frame, double phase, bool accepted, uint32_t ms)
{
    BSP_AudioBlock b = {0};
    for (unsigned i=0;i<BSP_AUDIO_HALF_FRAMES;i++) {
        noise_seed=noise_seed*1664525U+1013904223U;
        pcm[2*i]=phase<0 ? (int16_t)(noise_seed>>16) :
            (int16_t)(12000*sin((double)(frame+i)*6.283185307179586/tone_period_frames+phase));
        pcm[2*i+1]=0;
    }
    b.samples=pcm; b.frame_count=BSP_AUDIO_HALF_FRAMES; b.first_frame=frame;
    b.session=1; b.index_valid=true; b.contiguous=frame!=0;
    WaveformService_StageBlock(&b);
    memset(pcm,0,sizeof pcm); /* stage不能保留借用指针 */
    WaveformService_CompleteBlock(accepted,ms);
}
/** @brief 两个整秒锚点，PCM零基帧n对应NDTR进度n+1。 */
static void Anchors(void)
{
    AudioTimeAnchorObservation a={0};
    a.anchor.valid=true; a.anchor.session=1;
    a.sequence=1; a.captured_ms=1000;
    a.anchor.frame_lo=a.anchor.frame_hi=48000; a.anchor.sec=11;
    WaveformService_ObserveAnchor(&a);
    a.sequence=2; a.captured_ms=2000;
    a.anchor.frame_lo=a.anchor.frame_hi=96000; a.anchor.sec=12;
    WaveformService_ObserveAnchor(&a);
}
/** @brief 准备足够历史跨越多个DMA半块。 */
static void History(double phase)
{
    for (uint64_t i=65536;i<114688;i+=8192) Feed(i,phase,true,(uint32_t)(i/48));
}
/**
 * @brief 验证2kHz及997Hz在不同块首帧/相位和连续共同计划下的参考相位稳定性。
 * @note 1400 PCM差小于满幅12000单音的2%周期最大斜率误差(约1508 PCM)；
 *       同时比较解析正弦与跨计划快照，避免只检查过零一点。
 */
static void CheckFractionalPhase(void)
{
    const double frequencies[]={2000.0,997.0};
    const unsigned block_shifts[]={0,1,17,35};
    const double phases[]={0.11,0.94,1.77,0.26};
    WaveformSnapshot baseline={0}, snapshot;
    double maximum_error=0.0, maximum_jitter=0.0;
    for (unsigned frequency=0;frequency<2;frequency++) {
        tone_period_frames=48000.0/frequencies[frequency];
        for (unsigned variant=0;variant<4;variant++) {
            double phase=phases[variant];
            uint64_t next=65536+block_shifts[variant];
            WaveformService_Init();
            for (unsigned block=0;block<6;block++,next+=8192)
                Feed(next,phase,true,2300);
            Anchors();
            WaveformPlan plan={0};
            for (unsigned tick=0;tick<5;tick++) {
                uint64_t common_ns=UINT64_C(12300000000)+(uint64_t)tick*UINT64_C(200000000);
                uint32_t now_ms=2400+tick*200;
                uint64_t wanted_end=115400+tick*9600;
                while (next<wanted_end) {
                    Feed(next,phase,true,now_ms);
                    next+=8192;
                }
                WaveformService_Update(BOARD_ROLE_A,true,&domain,common_ns,now_ms);
                assert(WaveformService_TakePlan(&plan));
                WaveformService_Update(BOARD_ROLE_A,true,&domain,plan.display_deadline_ns,now_ms+100);
                WaveformService_GetSnapshot(&snapshot);
                assert(snapshot.valid && snapshot.synced && snapshot.period_valid);
                if (variant==0 && tick==0) baseline=snapshot;
                for (unsigned i=0;i<WAVEFORM_POINT_COUNT;i++) {
                    double angle=6.283185307179586*frequencies[frequency]*
                        snapshot.window_span_ns*1e-9*i/(WAVEFORM_POINT_COUNT-1U);
                    double expected=12000*sin(angle);
                    double error=fabs(snapshot.points[i]-expected);
                    double jitter=fabs((double)snapshot.points[i]-baseline.points[i]);
                    if (error>maximum_error) maximum_error=error;
                    if (jitter>maximum_jitter) maximum_jitter=jitter;
                    assert(error<1400.0);
                    assert(jitter<1400.0);
                }
            }
            /* B移动后的两种相移仍相对A共同起点绘制，禁止插值改动暗中重置B相位。 */
            for (unsigned move=0;move<2;move++) {
                double phase_shift=1.5707963267948966+move*0.5;
                WaveformService_Init();
                for (uint64_t frame=65536+block_shifts[variant];frame<next;frame+=8192)
                    Feed(frame,phase+phase_shift,true,3200);
                Anchors();
                WaveformService_Update(BOARD_ROLE_B,true,&domain,
                    plan.display_deadline_ns-UINT64_C(100000000),3200);
                assert(WaveformService_OnPlan(&plan,3200));
                WaveformService_Update(BOARD_ROLE_B,true,&domain,plan.display_deadline_ns,3300);
                WaveformSnapshot shifted;
                WaveformService_GetSnapshot(&shifted);
                assert(shifted.valid && shifted.synced && shifted.window_start_ns==snapshot.window_start_ns);
                for (unsigned i=0;i<WAVEFORM_POINT_COUNT;i++) {
                    double angle=6.283185307179586*frequencies[frequency]*
                        shifted.window_span_ns*1e-9*i/(WAVEFORM_POINT_COUNT-1U);
                    assert(fabs(shifted.points[i]-12000*sin(angle+phase_shift))<1400.0);
                }
            }
        }
    }
    printf("fractional phase: max analytic error %.1f PCM, frame jitter %.1f PCM\n",
           maximum_error,maximum_jitter);
    tone_period_frames=96;
}
/** @brief 验证线性插值右邻帧必须已提交；末样本前0.5帧可绘，后0.5帧拒绝。 */
static void CheckInterpolationBoundary(void)
{
    const uint64_t starts_ns[]={UINT64_C(12379312500),UINT64_C(12379333333)};
    for (unsigned boundary=0;boundary<2;boundary++) {
        WaveformService_Init(); History(0); Anchors();
        WaveformService_Update(BOARD_ROLE_B,true,&domain,UINT64_C(12750000000),2400);
        WaveformPlan plan={7,9,1,UINT64_C(12850000000),starts_ns[boundary],10000000,0};
        assert(WaveformService_OnPlan(&plan,2400));
        WaveformService_Update(BOARD_ROLE_B,true,&domain,plan.display_deadline_ns,2500);
        WaveformSnapshot snapshot;
        WaveformService_GetSnapshot(&snapshot);
        assert(snapshot.valid==(boundary==0));
        assert(snapshot.synced==(boundary==0));
    }
}
int main(void)
{
    WaveformSnapshot a,b; WaveformPlan p,q; uint8_t wire[WAVEFORM_PLAN_WIRE_BYTES];
    /* 初始化前连借用指针也不可读取，模拟SDRAM启动失败但标准音继续运行。 */
    BSP_AudioBlock before_init={0};
    before_init.samples=(const volatile int16_t *)(uintptr_t)1;
    before_init.frame_count=BSP_AUDIO_HALF_FRAMES; before_init.index_valid=true; before_init.session=1;
    WaveformService_StageBlock(&before_init); WaveformService_CompleteBlock(true,10);
    WaveformService_NotifyClockChange();
    WaveformService_Update(BOARD_ROLE_A,true,&domain,UINT64_C(12300000000),20);
    Anchors();
    WaveformService_GetSnapshot(&a); assert(!a.valid && a.frame_seq==0);
    assert(!WaveformService_TakePlan(&p));
    WaveformService_Init(); Feed(0,0,false,0);
    WaveformService_Update(BOARD_ROLE_A,false,NULL,0,200);
    WaveformService_GetSnapshot(&a); assert(!a.valid);
    Feed(8192,0,true,210);
    WaveformService_Update(BOARD_ROLE_A,false,NULL,0,410);
    WaveformService_GetSnapshot(&a); assert(a.valid && !a.synced);
    assert(a.points[10]!=0 || a.points[20]!=0);
    WaveformService_Update(BOARD_ROLE_A,false,NULL,0,1200);
    WaveformService_GetSnapshot(&a); assert(!a.valid); /* 断流 */
    WaveformService_Init(); History(0); Anchors();
    WaveformService_Update(BOARD_ROLE_A,true,&domain,UINT64_C(12300000000),2400);
    assert(WaveformService_TakePlan(&p));
    assert(WaveformPlanWire_Encode(&p,wire,sizeof wire));
    assert(WaveformPlanWire_Decode(wire,sizeof wire,&q));
    assert(q.window_start_ns==p.window_start_ns && q.sequence==p.sequence);
    wire[0]=0; assert(!WaveformPlanWire_Decode(wire,sizeof wire,&q));
    WaveformService_Update(BOARD_ROLE_A,true,&domain,p.display_deadline_ns,2500);
    WaveformService_GetSnapshot(&a); assert(a.valid && a.synced && a.period_valid);
    assert(a.period_milli_hz>490000 && a.period_milli_hz<510000);
    WaveformService_Init(); History(1.5707963267948966); Anchors();
    WaveformService_Update(BOARD_ROLE_B,true,&domain,UINT64_C(12300000000),2400);
    assert(WaveformService_OnPlan(&p,2400)); assert(!WaveformService_OnPlan(&p,2450));
    WaveformService_Update(BOARD_ROLE_B,true,&domain,p.display_deadline_ns,2500);
    WaveformService_GetSnapshot(&b); assert(b.valid && b.synced);
    assert(a.window_start_ns==b.window_start_ns && a.window_span_ns==b.window_span_ns);
    assert(memcmp(a.points,b.points,sizeof a.points)!=0); /* B相移必须保留 */
    assert(a.points[0]>-1500 && a.points[0]<1500 && b.points[0]>10000);
    q=p; q.sequence++; q.authority_epoch++; assert(!WaveformService_OnPlan(&q,2501));
    /* 重复旧计划不能保持SYNCED；继续供PCM以排除断流造成的失效。 */
    assert(!WaveformService_OnPlan(&p,2700));
    Feed(114688,1.5707963267948966,true,2800);
    WaveformService_Update(BOARD_ROLE_B,true,&domain,UINT64_C(12970000000),3070);
    WaveformService_GetSnapshot(&b); assert(b.valid && !b.synced);
    WaveformService_Update(BOARD_ROLE_B,false,NULL,0,2502);
    WaveformService_GetSnapshot(&b); assert(!b.synced);
    /* 重新初始化并换代：旧模型与旧计划必须撤销，不能跨域复活。 */
    WaveformService_Init(); History(0); Anchors();
    WaveformService_Update(BOARD_ROLE_B,true,&domain,UINT64_C(12300000000),2400);
    assert(WaveformService_OnPlan(&p,2400));
    RangingSessionSnapshot changed=domain; changed.authority_epoch++;
    WaveformService_Update(BOARD_ROLE_B,true,&changed,UINT64_C(12310000000),2410);
    assert(!WaveformService_OnPlan(&p,2410));
    WaveformService_GetSnapshot(&b); assert(!b.synced);
    /* 非周期噪声保持同一个公共固定窗，并显式声明无可靠周期。 */
    WaveformService_Init(); History(-1); Anchors();
    WaveformService_Update(BOARD_ROLE_A,true,&domain,UINT64_C(12300000000),2400);
    assert(WaveformService_TakePlan(&q)); assert(q.period_milli_hz==0);
    assert(q.window_span_ns==10000000U);
    WaveformService_Update(BOARD_ROLE_A,true,&domain,q.display_deadline_ns,2500);
    WaveformService_GetSnapshot(&a); assert(a.valid && a.synced && !a.period_valid);
    /* 范围端点50Hz和2kHz均可估计，不把边界本身拒绝。 */
    const unsigned periods[]={24,960};
    for (unsigned k=0;k<2;k++) {
        tone_period_frames=periods[k];
        WaveformService_Init(); History(0); Anchors();
        WaveformService_Update(BOARD_ROLE_A,true,&domain,UINT64_C(12300000000),2400);
        assert(WaveformService_TakePlan(&q));
        assert(q.period_milli_hz==48000000U/tone_period_frames);
    }
    /* 退帧与最大值锚点由AudioTimeMap一次拒绝，旧模型不可继续生成共同计划。 */
    tone_period_frames=96;
    for (unsigned k=0;k<2;k++) {
        WaveformService_Init(); History(0); Anchors();
        AudioTimeAnchorObservation bad={0};
        bad.anchor.valid=true; bad.anchor.session=1; bad.anchor.sec=13;
        bad.anchor.frame_lo=bad.anchor.frame_hi=k==0 ? 1000 : UINT64_MAX;
        WaveformService_ObserveAnchor(&bad);
        WaveformService_Update(BOARD_ROLE_A,true,&domain,UINT64_C(12300000000),2400);
        assert(!WaveformService_TakePlan(&q));
        WaveformService_GetSnapshot(&a); assert(a.valid && !a.synced);
    }
    /* stage长度入口检查：过长描述符不得越界写或发布。 */
    BSP_AudioBlock oversized={0};
    oversized.samples=pcm; oversized.session=1; oversized.index_valid=true;
    oversized.frame_count=BSP_AUDIO_HALF_FRAMES+1;
    WaveformService_StageBlock(&oversized); WaveformService_CompleteBlock(true,2501);
    WaveformService_GetSnapshot(&a); assert(!a.valid);
    for (unsigned length=0;length<44;length++)
        assert(!WaveformPlanWire_Decode(wire,(uint16_t)length,&p));
    /* 4kHz超出声明范围，不能把两倍周期误标成稳定2kHz。 */
    tone_period_frames=12;
    WaveformService_Init(); History(0); Anchors();
    WaveformService_Update(BOARD_ROLE_A,true,&domain,UINT64_C(12300000000),2400);
    assert(WaveformService_TakePlan(&q)); assert(q.period_milli_hz==0);
    /* 前台晚于首个deadline才运行时，先发布到期计划，再产生下一计划。 */
    tone_period_frames=96;
    WaveformService_Init(); History(0); Anchors();
    WaveformService_Update(BOARD_ROLE_A,true,&domain,UINT64_C(12300000000),2400);
    assert(WaveformService_TakePlan(&p));
    WaveformService_Update(BOARD_ROLE_A,true,&domain,UINT64_C(12500000000),2600);
    WaveformService_GetSnapshot(&a); assert(a.synced && a.plan_sequence==p.sequence);
    assert(WaveformService_TakePlan(&q)); assert(q.sequence!=p.sequence);
    WaveformService_Update(BOARD_ROLE_A,true,&domain,q.display_deadline_ns,2700);
    WaveformService_GetSnapshot(&a); assert(a.synced && a.plan_sequence==q.sequence);
    /* 连续调频不改变会话，但必须撤销旧斜率，保留PCM供LOCAL。 */
    WaveformService_NotifyClockChange();
    WaveformService_Update(BOARD_ROLE_A,true,&domain,UINT64_C(12610000000),2710);
    WaveformService_GetSnapshot(&a); assert(a.valid && !a.synced);
    assert(!WaveformService_TakePlan(&p));
    AudioTimeAnchorObservation fresh={0};
    fresh.anchor.valid=true; fresh.anchor.session=1;
    fresh.anchor.sec=12; fresh.anchor.nsec=500000000U;
    fresh.anchor.frame_lo=fresh.anchor.frame_hi=120000;
    fresh.captured_ms=2500;
    WaveformService_ObserveAnchor(&fresh);
    WaveformService_Update(BOARD_ROLE_A,true,&domain,UINT64_C(12700000000),2800);
    assert(!WaveformService_TakePlan(&p)); /* 一个新锚点不能跨旧斜率恢复 */
    for (uint64_t i=114688;i<155648;i+=8192) Feed(i,0,true,(uint32_t)(i/48));
    fresh.anchor.sec=13; fresh.anchor.nsec=0;
    fresh.anchor.frame_lo=fresh.anchor.frame_hi=144000;
    fresh.captured_ms=3000;
    WaveformService_ObserveAnchor(&fresh);
    WaveformService_Update(BOARD_ROLE_A,true,&domain,UINT64_C(13200000000),3300);
    assert(WaveformService_TakePlan(&p));
    CheckFractionalPhase();
    CheckInterpolationBoundary();
    puts("waveform: PASS"); return 0;
}
