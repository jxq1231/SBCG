#include "Audio/audio_time_map.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static AudioTimeMapAnchor Anchor(uint64_t lo, uint64_t hi,
                                  uint32_t sec, uint32_t nsec)
{
    AudioTimeMapAnchor a = { true, 1U, lo, hi, sec, nsec };
    return a;
}

static void ExpectFailure(const AudioTimeMapAnchor *a,
                          const AudioTimeMapAnchor *b, uint32_t session,
                          uint64_t frame, AudioTimeMapStatus expected)
{
    AudioTimeMapResult result;
    memset(&result, 0xA5, sizeof result);
    assert(AudioTimeMap_Estimate(a, b, session, frame, &result) == expected);
    assert(!result.valid && result.estimate_ns == 0U &&
           result.model_lower_ns == 0U && result.model_upper_ns == 0U);
}

int main(void)
{
    AudioTimeMapAnchor a = Anchor(100U, 100U, 1U, 0U);
    AudioTimeMapAnchor b = Anchor(48100U, 48100U, 2U, 0U);
    AudioTimeMapResult result;
    assert(AudioTimeMap_Estimate(&a, &b, 1U, 24100U, &result) == AUDIO_TIME_MAP_OK);
    assert(result.valid && result.model_lower_ns == UINT64_C(1499979166));
    assert(result.model_upper_ns == UINT64_C(1500000000));
    assert(result.estimate_ns == UINT64_C(1499989583));
    /* Zero-width anchors retain a full frame bin, and exact endpoint coverage
       must hold for every endpoint phase, not merely their midpoints. */
    assert(result.model_upper_ns > result.model_lower_ns);
    assert(AudioTimeMap_Estimate(&a, &b, 1U, 101U, &result) == AUDIO_TIME_MAP_OK);
    assert(result.model_lower_ns == UINT64_C(1000000000));
    assert(AudioTimeMap_Estimate(&a, &b, 1U, 48100U, &result) == AUDIO_TIME_MAP_OK);
    assert(result.model_upper_ns == UINT64_C(2000000000));
    ExpectFailure(&a, &b, 1U, 100U, AUDIO_TIME_MAP_OUTSIDE);
    ExpectFailure(&a, &b, 1U, 48101U, AUDIO_TIME_MAP_OUTSIDE);
    ExpectFailure(NULL, &b, 1U, 24100U, AUDIO_TIME_MAP_BAD_ARGUMENT);
    ExpectFailure(&a, NULL, 1U, 24100U, AUDIO_TIME_MAP_BAD_ARGUMENT);
    assert(AudioTimeMap_Estimate(&a, &b, 1U, 24100U, NULL) == AUDIO_TIME_MAP_BAD_ARGUMENT);
    ExpectFailure(&a, &b, 0U, 24100U, AUDIO_TIME_MAP_SESSION);
    ExpectFailure(&a, &b, 2U, 24100U, AUDIO_TIME_MAP_SESSION);
    a.valid = false;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_INVALID_ANCHOR);
    a.valid = true; b.valid = false;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_INVALID_ANCHOR);
    b.valid = true; a.session = 0U;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_SESSION);
    a.session = 1U; b.session = 2U;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_SESSION);
    b.session = 1U; a.nsec = 1000000000U;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_INVALID_ANCHOR);
    a.nsec = 0U; b.nsec = UINT32_MAX;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_INVALID_ANCHOR);
    b.nsec = 0U; a.frame_lo = 101U;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_INVALID_ANCHOR);
    a.frame_lo = 100U; a.frame_hi = 105U;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_INVALID_ANCHOR);
    a.frame_hi = 100U; b.frame_hi += 5U;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_INVALID_ANCHOR);
    b.frame_hi -= 5U;
    ExpectFailure(&b, &a, 1U, 24100U, AUDIO_TIME_MAP_ORDER);
    b.sec = 1U;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_ORDER);
    b.sec = 0U;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_ORDER);
    b.sec = 1U; b.nsec = 99999999U;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_WINDOW);
    b.sec = 11U; b.nsec = 1U;
    ExpectFailure(&a, &b, 1U, 24100U, AUDIO_TIME_MAP_WINDOW);
    /* Large absolute frame coordinates are allowed. Only differences multiply. */
    a = Anchor(UINT64_MAX - 100000U, UINT64_MAX - 100000U, UINT32_MAX - 1U, 700000000U);
    b = Anchor(UINT64_MAX - 52000U, UINT64_MAX - 52000U, UINT32_MAX, 700000000U);
    assert(AudioTimeMap_Estimate(&a, &b, 1U, UINT64_MAX - 76000U, &result) == AUDIO_TIME_MAP_OK);
    assert(result.estimate_ns > UINT64_C(4294967295000000000));
    b = Anchor(UINT64_MAX, UINT64_MAX, UINT32_MAX, 700000000U);
    ExpectFailure(&a, &b, 1U, UINT64_MAX - 76000U, AUDIO_TIME_MAP_RANGE);
    a = Anchor(UINT64_MAX, UINT64_MAX, 0U, 0U);
    ExpectFailure(&a, &b, 1U, 0U, AUDIO_TIME_MAP_RANGE);
    a = Anchor(0U, 0U, 0U, 0U);
    b = Anchor(UINT64_MAX - 1U, UINT64_MAX - 1U, 1U, 0U);
    ExpectFailure(&a, &b, 1U, 10U, AUDIO_TIME_MAP_RANGE);
    b = Anchor(1U, 1U, 1U, 0U);
    ExpectFailure(&a, &b, 1U, 1U, AUDIO_TIME_MAP_ORDER);
    b = Anchor(47000U, 47000U, 1U, 0U);
    ExpectFailure(&a, &b, 1U, 24000U, AUDIO_TIME_MAP_RATE);
    b = Anchor(49000U, 49000U, 1U, 0U);
    ExpectFailure(&a, &b, 1U, 24000U, AUDIO_TIME_MAP_RATE);
    b = Anchor(48000U, 48000U, 0U, 100000000U);
    ExpectFailure(&a, &b, 1U, 24000U, AUDIO_TIME_MAP_RATE);
    b = Anchor(4800U, 4800U, 0U, 100000000U);
    assert(AudioTimeMap_Estimate(&a, &b, 1U, 2400U, &result) == AUDIO_TIME_MAP_OK);
    b = Anchor(480000U, 480000U, 10U, 0U);
    assert(AudioTimeMap_Estimate(&a, &b, 1U, 240000U, &result) == AUDIO_TIME_MAP_OK);
    a = Anchor(0U, 4U, 1U, 950000000U);
    b = Anchor(48000U, 48004U, 2U, 950000000U);
    assert(AudioTimeMap_Estimate(&a, &b, 1U, 24000U, &result) == AUDIO_TIME_MAP_OK);
    assert(result.model_lower_ns <= result.estimate_ns && result.estimate_ns <= result.model_upper_ns);
    assert(result.model_upper_ns - result.model_lower_ns > 100000U);
    /* Q16=0 must reproduce the integer model byte-for-byte. */
    a = Anchor(100U, 100U, 1U, 0U);
    b = Anchor(48100U, 48100U, 2U, 0U);
    AudioTimeMapPosition position = {24100U, 0U};
    assert(AudioTimeMap_EstimateQ16(&a, &b, 1U, &position, &result) == AUDIO_TIME_MAP_OK);
    assert(result.model_lower_ns == UINT64_C(1499979166));
    assert(result.model_upper_ns == UINT64_C(1500000000));
    assert(result.estimate_ns == UINT64_C(1499989583));
    /* Fractional target uses exact Q16 quotient/remainder arithmetic. */
    position.fraction_q16 = 16384U;
    assert(AudioTimeMap_EstimateQ16(&a, &b, 1U, &position, &result) == AUDIO_TIME_MAP_OK);
    assert(result.model_lower_ns == UINT64_C(1499984375));
    assert(result.model_upper_ns == UINT64_C(1500005209));
    assert(result.estimate_ns == UINT64_C(1499994792));
    position.fraction_q16 = 32768U;
    assert(AudioTimeMap_EstimateQ16(&a, &b, 1U, &position, &result) == AUDIO_TIME_MAP_OK);
    assert(result.estimate_ns == UINT64_C(1500000000));
    position.fraction_q16 = 65535U;
    assert(AudioTimeMap_EstimateQ16(&a, &b, 1U, &position, &result) == AUDIO_TIME_MAP_OK);
    assert(result.model_lower_ns == UINT64_C(1499999999));
    assert(result.model_upper_ns == UINT64_C(1500020834));
    /* Fraction at the right endpoint is outside; exact right endpoint is inside. */
    position.frame = 48100U;
    position.fraction_q16 = 1U;
    assert(AudioTimeMap_EstimateQ16(&a, &b, 1U, &position, &result) == AUDIO_TIME_MAP_OUTSIDE);
    position.fraction_q16 = 0U;
    assert(AudioTimeMap_EstimateQ16(&a, &b, 1U, &position, &result) == AUDIO_TIME_MAP_OK);
    assert(result.model_upper_ns == UINT64_C(2000000000));
    /* Null position is an explicit bad argument; large absolute coordinates remain valid. */
    assert(AudioTimeMap_EstimateQ16(&a, &b, 1U, NULL, &result) == AUDIO_TIME_MAP_BAD_ARGUMENT);
    a = Anchor(UINT64_MAX - 100000U, UINT64_MAX - 100000U, UINT32_MAX - 1U, 700000000U);
    b = Anchor(UINT64_MAX - 52000U, UINT64_MAX - 52000U, UINT32_MAX, 700000000U);
    position.frame = UINT64_MAX - 76000U;
    position.fraction_q16 = 12345U;
    assert(AudioTimeMap_EstimateQ16(&a, &b, 1U, &position, &result) == AUDIO_TIME_MAP_OK);
    position.frame = UINT64_MAX;
    assert(AudioTimeMap_EstimateQ16(&a, &b, 1U, &position, &result) == AUDIO_TIME_MAP_RANGE);
    puts("audio_time_map: input rejection, phase bins, integer bounds and high-base tests PASS");
    return 0;
}
