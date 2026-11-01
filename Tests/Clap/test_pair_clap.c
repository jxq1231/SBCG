/** @file test_pair_clap.c @brief 验证击掌不调用标准音精化或齿选择。 */
#include "Measure/sound_pair.h"
#include <assert.h>
#include <stdio.h>
static unsigned calls;
/** @brief 标准音精化器桩；击掌路径不得调用。 */
static bool ChirpOnly(uint32_t a, uint32_t b, int64_t *delta)
{ (void)a; (void)b; (void)delta; ++calls; return false; }
/** @brief 覆盖已安装精化器和旧齿路径下的CLAP隔离。 */
int main(void)
{
    const SoundPairConfig config={.max_tdoa_ns=30000000U,.max_distance_mm=10000U,
        .sound_speed_mm_s=343000U,.min_quality=50U,.pair_timeout_ms=100U,
        .nominal_distance_mm=320U,.tooth_jump_mm=32U,.tooth_confirm=3U};
    SoundPairManager manager; SoundPairResult result;
    SoundEventRecord a={.valid=true,.type=SOUND_EVENT_CLAP,.quality=90U,
        .side=SOUND_SIDE_A,.time_domain=9U,.session=2U,.sequence=1U,.time_ns=1000000000U};
    SoundEventRecord b=a; b.side=SOUND_SIDE_B; b.time_ns+=1000000U;
    b.toa_candidate_count=1U; b.toa_candidate_q16[0]=65536;
    assert(SoundPair_Init(&manager,&config));
    SoundPair_SetTdoaRefiner(&manager,ChirpOnly);
    assert(SoundPair_Submit(&manager,&a,0U)); assert(SoundPair_Submit(&manager,&b,0U));
    SoundPair_Process(&manager,0U);
    assert(calls==0U);
    assert(SoundPair_Take(&manager,&result) && result.valid && result.distance_mm==343U);
    assert(!result.waveform_refined && !result.b_toa_held && result.b_toa_selected==0U);
    assert(SoundPair_Init(&manager,&config));
    assert(SoundPair_Submit(&manager,&a,0U)); assert(SoundPair_Submit(&manager,&b,0U));
    SoundPair_Process(&manager,0U);
    assert(SoundPair_Take(&manager,&result) && result.valid && result.distance_mm==343U);
    assert(result.b_toa_selected==0U && manager.pending_count==0U);
    puts("PASS CLAP pairing: no CHIRP refiner/tooth state");
    return 0;
}
