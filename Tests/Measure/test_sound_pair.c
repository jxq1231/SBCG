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
    puts("PASS sound pair: distance/direction, identity gates, out-of-order, duplicate, ambiguity, timeout, tooth selection");
    return 0;
}
