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
    const SoundPairConfig c={40000000U,10000U,343000U,50U,50U};return c;
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
    b.time_ns=999000000U;
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_OK);
    assert(result.direction==SOUND_DIRECTION_B && result.distance_mm==343U);
    /* Explicit identity/time/quality/range rejections. */
    b=Event(SOUND_SIDE_B,2U,1001000000U);b.type=SOUND_EVENT_CLAP;
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_TYPE && !result.valid);
    b=Event(SOUND_SIDE_B,2U,1001000000U);b.time_domain=8U;
    assert(SoundPair_Compute(&config,&a,&b,&result)==SOUND_PAIR_DOMAIN);
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
    puts("PASS sound pair: distance/direction, identity gates, out-of-order, duplicate, ambiguity, timeout");
    return 0;
}
