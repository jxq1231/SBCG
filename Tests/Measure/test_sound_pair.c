/**
 * @file test_sound_pair.c
 * @brief 验证纯C配对的类型隔离、旧标量模式及波形证据失败拒绝边界。
 * @note 仅主机计算；成功回调的1m/2m结果不代表声学精度验收。
 */
#include "Measure/sound_pair.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static SoundEventRecord Event(SoundEventSide side,uint32_t seq,uint64_t time_ns)
{
    SoundEventRecord e={0};e.valid=true;e.type=SOUND_EVENT_CHIRP;e.quality=80U;
    e.side=side;e.time_domain=7U;e.session=1U;e.sequence=seq;e.time_ns=time_ns;return e;
}
static SoundPairConfig Config(void)
{
    const SoundPairConfig c={40000000U,10000U,343000U,50U,50U,0U,0U,0U};return c;
}
static int64_t refiner_delta;static uint32_t refiner_calls;
static bool Refiner(uint32_t a_seq,uint32_t b_seq,int64_t *delta)
{(void)a_seq;(void)b_seq;++refiner_calls;*delta=refiner_delta;return true;}
static bool RefinerFail(uint32_t a_seq,uint32_t b_seq,int64_t *delta)
{(void)a_seq;(void)b_seq;(void)delta;return false;}
/** @brief 距离差mm转1/65536帧时间偏移，测试用343m/s。 */
static int32_t OffsetQ16(int32_t delta_mm)
{
    const double ns=(double)delta_mm*1e9/343000.0;
    return (int32_t)(ns*48000.0/1e9*65536.0);
}
int main(void)
{
    const SoundPairConfig config=Config();SoundPairResult result;
    /* 1ms => 343mm, positive tB-tA means source on A side. */
    SoundEventRecord a=Event(SOUND_SIDE_A,1U,1000000000U);
    SoundEventRecord b=Event(SOUND_SIDE_B,1U,1001000000U);
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_OK);
    assert(result.valid && result.distance_mm==343U && result.direction==SOUND_DIRECTION_A);
    assert(result.delta_ns==1000000 && result.quality==80U);
    /* O1被动元数据必须逐字段来自输入，不能改变原有配对决定。 */
    assert(result.a_session==1U && result.b_session==1U);
    assert(result.a_sequence==1U && result.b_sequence==1U);
    assert(result.a_time_ns==1000000000U && result.b_time_ns==1001000000U);
    b.time_ns=999000000U;
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_OK);
    assert(result.direction==SOUND_DIRECTION_B && result.distance_mm==343U);
    /* Explicit identity/time/quality/range rejections. */
    b=Event(SOUND_SIDE_B,2U,1001000000U);b.type=SOUND_EVENT_CLAP;
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_TYPE && !result.valid);
    b=Event(SOUND_SIDE_B,2U,1001000000U);b.time_domain=8U;
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_DOMAIN);
    assert(!result.valid && result.b_sequence==2U && result.b_time_ns==1001000000U);
    b=Event(SOUND_SIDE_B,2U,1001000000U);b.quality=49U;
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_QUALITY);
    b=Event(SOUND_SIDE_B,2U,1041000000U);
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_TDOA_RANGE);
    b=Event(SOUND_SIDE_B,2U,1000000000U+40000000U);
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_DISTANCE_RANGE);
    /* 低温真实10m在343m/s粗算约10745mm：旧10m门先拒绝，放宽到11m后由温补终判。 */
    b=Event(SOUND_SIDE_B,2U,1000000000U+31327000U);
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_DISTANCE_RANGE);
    SoundPairConfig widened=config;widened.max_distance_mm=11000U;
    assert(SoundPair_Compute(&widened,&a,&b,&result)==SOUND_PAIR_OK);
    assert(result.valid && result.distance_mm==10745U);

    /* Queue pairs regardless of arrival order and is one-shot. */
    SoundPairManager manager;
    assert(SoundPair_Init(&manager,&config));
    b=Event(SOUND_SIDE_B,2U,1001000000U);
    assert(SoundPair_Submit(&manager,&b,0U));
    assert(SoundPair_Submit(&manager,&a,0U));
    SoundPair_Process(&manager,0U);
    assert(SoundPair_Take(&manager,&result) && result.valid && result.distance_mm==343U);
    assert(!SoundPair_Take(&manager,&result));
    SoundPairSnapshot snap;SoundPair_GetSnapshot(&manager,&snap);
    assert(snap.accepted==2U && snap.paired==1U && snap.pending_a==0U && snap.pending_b==0U);

    /* Duplicate and wrong side/quality are rejected without overwriting. */
    assert(SoundPair_Init(&manager,&config));
    assert(SoundPair_Submit(&manager,&a,0U));
    assert(!SoundPair_Submit(&manager,&a,0U));
    SoundPair_GetSnapshot(&manager,&snap);
    assert(snap.duplicate==1U && snap.pending_a==1U);

    /* Multiple candidates in the same window are explicitly ambiguous, not guessed. */
    assert(SoundPair_Init(&manager,&config));
    assert(SoundPair_Submit(&manager,&a,0U));
    b=Event(SOUND_SIDE_B,2U,1000001000U);assert(SoundPair_Submit(&manager,&b,0U));
    b=Event(SOUND_SIDE_B,3U,1000002000U);assert(SoundPair_Submit(&manager,&b,0U));
    SoundPair_Process(&manager,0U);
    assert(SoundPair_Take(&manager,&result));
    assert(!result.valid && result.status==SOUND_PAIR_AMBIGUOUS);
    SoundPair_GetSnapshot(&manager,&snap);
    assert(snap.ambiguous==1U && snap.pending_a==0U && snap.pending_b==0U);

    /* 两型真实事件同时排队：同窗也各自唯一，不得全局判歧义或互相消费。 */
    for(unsigned clap_first=0U;clap_first<2U;++clap_first)
    {
        assert(SoundPair_Init(&manager,&config));
        SoundEventRecord ca=Event(SOUND_SIDE_A,10U,1000000000U);
        SoundEventRecord cb=Event(SOUND_SIDE_B,10U,1001000000U);
        SoundEventRecord ka=Event(SOUND_SIDE_A,11U,1000000000U);
        SoundEventRecord kb=Event(SOUND_SIDE_B,11U,1002000000U);
        ka.type=kb.type=SOUND_EVENT_CLAP;
        assert(SoundPair_Submit(&manager,clap_first?&ka:&ca,0U));
        assert(SoundPair_Submit(&manager,clap_first?&cb:&kb,0U));
        assert(SoundPair_Submit(&manager,clap_first?&ca:&ka,0U));
        assert(SoundPair_Submit(&manager,clap_first?&kb:&cb,0U));
        SoundPair_Process(&manager,0U);
        assert(SoundPair_Take(&manager,&result) && result.valid);
        assert(result.type==(clap_first?SOUND_EVENT_CLAP:SOUND_EVENT_CHIRP));
        assert(result.distance_mm==(clap_first?686U:343U));
        SoundPair_GetSnapshot(&manager,&snap);
        assert(snap.ambiguous==0U && snap.pending_a==1U && snap.pending_b==1U);
        SoundPair_Process(&manager,0U);
        assert(SoundPair_Take(&manager,&result) && result.valid);
        assert(result.type==(clap_first?SOUND_EVENT_CHIRP:SOUND_EVENT_CLAP));
        assert(result.distance_mm==(clap_first?343U:686U));
        SoundPair_GetSnapshot(&manager,&snap);
        assert(snap.paired==2U && snap.pending_a==0U && snap.pending_b==0U);
    }
    /* 同型多候选仍严格拒绝；另一型的完整对及未配对事件保留。 */
    for(unsigned ambiguous_clap=0U;ambiguous_clap<2U;++ambiguous_clap)
    {
        assert(SoundPair_Init(&manager,&config));
        SoundEventRecord aa=Event(SOUND_SIDE_A,20U,1000000000U);
        SoundEventRecord ab=Event(SOUND_SIDE_B,20U,1001000000U);
        SoundEventRecord extra=Event(SOUND_SIDE_B,21U,1002000000U);
        aa.type=ab.type=extra.type=ambiguous_clap?SOUND_EVENT_CLAP:SOUND_EVENT_CHIRP;
        SoundEventRecord other_a=Event(SOUND_SIDE_A,22U,1000000000U);
        SoundEventRecord other_b=Event(SOUND_SIDE_B,22U,1001000000U);
        SoundEventRecord pending=Event(SOUND_SIDE_A,23U,2000000000U);
        other_a.type=other_b.type=pending.type=ambiguous_clap?SOUND_EVENT_CHIRP:SOUND_EVENT_CLAP;
        assert(SoundPair_Submit(&manager,&aa,0U));assert(SoundPair_Submit(&manager,&ab,0U));
        assert(SoundPair_Submit(&manager,&other_a,0U));assert(SoundPair_Submit(&manager,&other_b,0U));
        assert(SoundPair_Submit(&manager,&extra,0U));assert(SoundPair_Submit(&manager,&pending,0U));
        SoundPair_Process(&manager,0U);
        assert(SoundPair_Take(&manager,&result) && !result.valid && result.status==SOUND_PAIR_AMBIGUOUS);
        assert(result.type==aa.type);
        SoundPair_GetSnapshot(&manager,&snap);
        assert(snap.ambiguous==1U && snap.pending_a==2U && snap.pending_b==1U);
        SoundPair_Process(&manager,0U);
        assert(SoundPair_Take(&manager,&result) && result.valid && result.type==other_a.type);
        SoundPair_GetSnapshot(&manager,&snap);
        assert(snap.paired==1U && snap.pending_a==1U && snap.pending_b==0U);
    }

    /* Missing partner expires and produces no guessed result. */
    assert(SoundPair_Init(&manager,&config));
    assert(SoundPair_Submit(&manager,&a,10U));
    SoundPair_Process(&manager,19U);assert(!SoundPair_Take(&manager,&result));
    SoundPair_Process(&manager,60U);
    SoundPair_GetSnapshot(&manager,&snap);
    assert(snap.expired==1U && snap.pending_a==0U && !SoundPair_Take(&manager,&result));

    /* Zero delta is valid geometry but direction unknown. */
    b=Event(SOUND_SIDE_B,1U,1000000000U);
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_OK);
    assert(result.distance_mm==0U && result.direction==SOUND_DIRECTION_UNKNOWN);
    /* B候选齿选择：基准390mm，候选含256/320/390/459，应选320而非390。 */
    {
        SoundPairConfig tooth=Config();
        tooth.nominal_distance_mm=320U;tooth.tooth_jump_mm=32U;tooth.tooth_confirm=3U;
        SoundPairManager tm;
        assert(SoundPair_Init(&tm,&tooth));
        SoundEventRecord ta=Event(SOUND_SIDE_A,10U,2000000000U);
        SoundEventRecord tb=Event(SOUND_SIDE_B,10U,2000000000U+1137026U);
        tb.toa_candidate_count=4U;
        tb.toa_candidate_q16[0]=OffsetQ16(-134);
        tb.toa_candidate_q16[1]=OffsetQ16(-70);
        tb.toa_candidate_q16[2]=0;
        tb.toa_candidate_q16[3]=OffsetQ16(69);
        assert(SoundPair_Submit(&tm,&ta,0U));
        assert(SoundPair_Submit(&tm,&tb,0U));
        SoundPair_Process(&tm,0U);
        assert(SoundPair_Take(&tm,&result));
        assert(result.valid && result.distance_mm==320U);
        assert(result.b_toa_held==false && result.b_toa_selected==2U);
        /* 单次异常候选（只有晚齿390/455）只能进入待确认并保持320，不能把参考拖到390。 */
        const uint64_t a_ns=2000000000U, raw_b_ns=a_ns+1137026U;
        ta=Event(SOUND_SIDE_A,20U,a_ns);
        tb=Event(SOUND_SIDE_B,20U,raw_b_ns);
        tb.toa_candidate_count=2U;tb.toa_candidate_q16[0]=0;tb.toa_candidate_q16[1]=OffsetQ16(65);
        assert(SoundPair_Submit(&tm,&ta,0U));assert(SoundPair_Submit(&tm,&tb,0U));
        SoundPair_Process(&tm,0U);
        assert(SoundPair_Take(&tm,&result));
        assert(result.valid && result.distance_mm==320U && result.b_toa_held);
        assert(tm.pending_count==1U && tm.pending_distance_mm==390U && tm.accepted_distance_mm==320U);
        /* 下一正常候选集立即回到接受齿320：待确认清空，输出不再保持旧值。 */
        ta=Event(SOUND_SIDE_A,21U,a_ns);
        tb=Event(SOUND_SIDE_B,21U,raw_b_ns);
        tb.toa_candidate_count=2U;tb.toa_candidate_q16[0]=OffsetQ16(-70);tb.toa_candidate_q16[1]=0;
        assert(SoundPair_Submit(&tm,&ta,0U));assert(SoundPair_Submit(&tm,&tb,0U));
        SoundPair_Process(&tm,0U);
        assert(SoundPair_Take(&tm,&result));
        assert(result.valid && result.distance_mm==320U && !result.b_toa_held);
        assert(tm.pending_count==0U && tm.accepted_distance_mm==320U && result.b_toa_selected==1U);
        ta=Event(SOUND_SIDE_A,22U,a_ns);
        tb=Event(SOUND_SIDE_B,22U,raw_b_ns);
        tb.toa_candidate_count=2U;tb.toa_candidate_q16[0]=OffsetQ16(-70);tb.toa_candidate_q16[1]=0;
        assert(SoundPair_Submit(&tm,&ta,0U));assert(SoundPair_Submit(&tm,&tb,0U));
        SoundPair_Process(&tm,0U);
        assert(SoundPair_Take(&tm,&result) && result.distance_mm==320U && !result.b_toa_held);
        /* 真实一跳>32mm仍按N=3确认跟随，确认前保持旧接受距离。 */
        {
            const uint64_t jump_b_ns=a_ns+1166181U; /* 约400mm */
            for(uint32_t i=0U;i<2U;++i)
            {
                ta=Event(SOUND_SIDE_A,30U+i,a_ns);
                tb=Event(SOUND_SIDE_B,30U+i,jump_b_ns);
                tb.toa_candidate_count=1U;tb.toa_candidate_q16[0]=0;
                assert(SoundPair_Submit(&tm,&ta,0U));assert(SoundPair_Submit(&tm,&tb,0U));
                SoundPair_Process(&tm,0U);
                assert(SoundPair_Take(&tm,&result));
                assert(result.valid && result.distance_mm==320U && result.b_toa_held);
            }
            ta=Event(SOUND_SIDE_A,32U,a_ns);
            tb=Event(SOUND_SIDE_B,32U,jump_b_ns);
            tb.toa_candidate_count=1U;tb.toa_candidate_q16[0]=0;
            assert(SoundPair_Submit(&tm,&ta,0U));assert(SoundPair_Submit(&tm,&tb,0U));
            SoundPair_Process(&tm,0U);
            assert(SoundPair_Take(&tm,&result));
            assert(result.valid && result.distance_mm==400U && !result.b_toa_held);
            assert(tm.accepted_distance_mm==400U && tm.pending_count==0U);
            ta=Event(SOUND_SIDE_A,33U,a_ns);
            tb=Event(SOUND_SIDE_B,33U,jump_b_ns);
            tb.toa_candidate_count=1U;tb.toa_candidate_q16[0]=0;
            assert(SoundPair_Submit(&tm,&ta,0U));assert(SoundPair_Submit(&tm,&tb,0U));
            SoundPair_Process(&tm,0U);
            assert(SoundPair_Take(&tm,&result) && result.distance_mm==400U && !result.b_toa_held);
        }
        puts("tooth selection: B candidate set selected 320 mm and abnormal teeth cannot drag the reference");
    }
    /* 波形精化：安装回调后，失败不能把旧齿或标称距离变成一次新测量。 */
    {
        SoundPairManager wm;
        assert(SoundPair_Init(&wm,&config));
        refiner_calls=0U;refiner_delta=950000; /* 约326mm，与原始514mm无关 */
        SoundPair_SetTdoaRefiner(&wm,Refiner);
        SoundEventRecord wa=Event(SOUND_SIDE_A,40U,2000000000U);
        SoundEventRecord wb=Event(SOUND_SIDE_B,40U,2001500000U);
        assert(SoundPair_Submit(&wm,&wa,0U));assert(SoundPair_Submit(&wm,&wb,0U));
        SoundPair_Process(&wm,0U);
        assert(SoundPair_Take(&wm,&result));
        assert(result.valid && result.waveform_refined && result.distance_mm==326U);
        assert(result.b_toa_selected==0U && refiner_calls==1U);
        assert(SoundPair_Init(&wm,&config));
        SoundPair_SetTdoaRefiner(&wm,RefinerFail);
        assert(SoundPair_Submit(&wm,&wa,0U));assert(SoundPair_Submit(&wm,&wb,0U));
        SoundPair_Process(&wm,0U);
        assert(SoundPair_Take(&wm,&result));
        assert(!result.valid && !result.waveform_refined);
        assert(result.status==SOUND_PAIR_WAVEFORM_UNAVAILABLE && result.distance_mm==0U);
        assert(result.a_sequence==40U && result.b_sequence==40U);
        SoundPair_GetSnapshot(&wm,&snap);
        assert(snap.paired==0U && snap.rejected==1U);
        /* 实测距离未知时仍配置320的旧调用者，也不能在精化失败后输出320。 */
        SoundPairConfig seeded=config;
        seeded.nominal_distance_mm=320U;seeded.tooth_jump_mm=32U;seeded.tooth_confirm=3U;
        assert(SoundPair_Init(&wm,&seeded));
        SoundPair_SetTdoaRefiner(&wm,RefinerFail);
        wb.toa_candidate_count=2U;
        wb.toa_candidate_q16[0]=OffsetQ16(-195);wb.toa_candidate_q16[1]=0;
        for(uint32_t i=0U;i<4U;++i)
        {
            wa.sequence=wb.sequence=50U+i;
            assert(SoundPair_Submit(&wm,&wa,0U));assert(SoundPair_Submit(&wm,&wb,0U));
            SoundPair_Process(&wm,0U);
            assert(SoundPair_Take(&wm,&result) && !result.valid);
            assert(result.b_toa_selected==0U && !result.b_toa_held);
        }
        assert(wm.accepted_distance_mm==320U && wm.pending_count==0U);
        /* 后续成功可立即恢复1m/2m测量，不能被先前320参考或失败次数锁住。 */
        SoundPair_SetTdoaRefiner(&wm,Refiner);
        for(uint32_t metres=1U;metres<=2U;++metres)
        {
            refiner_delta=(int64_t)metres*1000000000LL/343;
            wa.sequence=wb.sequence=60U+metres;
            assert(SoundPair_Submit(&wm,&wa,0U));assert(SoundPair_Submit(&wm,&wb,0U));
            SoundPair_Process(&wm,0U);
            assert(SoundPair_Take(&wm,&result) && result.valid && result.waveform_refined);
            assert(result.distance_mm==metres*1000U && !result.b_toa_held);
        }
        puts("waveform refine: failed evidence rejects without nominal/old-tooth fallback; 1m/2m recover");
    }
    puts("PASS sound pair: distance/direction, identity gates, out-of-order, duplicate, ambiguity, timeout, tooth selection");
    return 0;
}
