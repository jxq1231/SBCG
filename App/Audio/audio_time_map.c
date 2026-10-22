#include "audio_time_map.h"
#include <stddef.h>
#include <string.h>

#define NS_PER_SEC UINT64_C(1000000000)
#define NS_PER_SEC_X_MILLI UINT64_C(1000000000000)

static bool ValidAnchor(const AudioTimeMapAnchor *anchor)
{
    return anchor->valid && anchor->nsec < NS_PER_SEC &&
           anchor->frame_hi >= anchor->frame_lo &&
           anchor->frame_hi - anchor->frame_lo <= AUDIO_TIME_MAP_MAX_WIDTH_FRAMES;
}

AudioTimeMapStatus AudioTimeMap_Estimate(const AudioTimeMapAnchor *left,
                                        const AudioTimeMapAnchor *right,
                                        uint32_t target_session,
                                        uint64_t frame_position,
                                        AudioTimeMapResult *result)
{
    uint64_t time_left_ns, time_right_ns, elapsed_ns;
    uint64_t upper_left, upper_right, min_delta, max_delta;
    uint64_t early_product, late_product, early_ns, late_ns, late_denominator;

    if (result == NULL)
    {
        return AUDIO_TIME_MAP_BAD_ARGUMENT;
    }
    memset(result, 0, sizeof *result);
    if (left == NULL || right == NULL)
    {
        return AUDIO_TIME_MAP_BAD_ARGUMENT;
    }
    if (!ValidAnchor(left) || !ValidAnchor(right))
    {
        return AUDIO_TIME_MAP_INVALID_ANCHOR;
    }
    if (target_session == 0U || left->session != target_session ||
        right->session != target_session)
    {
        return AUDIO_TIME_MAP_SESSION;
    }
    if (left->frame_hi == UINT64_MAX || right->frame_hi == UINT64_MAX)
    {
        return AUDIO_TIME_MAP_RANGE;
    }
    upper_left = left->frame_hi + 1U;
    upper_right = right->frame_hi + 1U;
    /* uint32 seconds and <1e9 nanoseconds always fit in uint64 nanoseconds. */
    time_left_ns = (uint64_t)left->sec * NS_PER_SEC + left->nsec;
    time_right_ns = (uint64_t)right->sec * NS_PER_SEC + right->nsec;
    if (time_right_ns <= time_left_ns || right->frame_lo <= upper_left)
    {
        return AUDIO_TIME_MAP_ORDER;
    }
    elapsed_ns = time_right_ns - time_left_ns;
    if (elapsed_ns < AUDIO_TIME_MAP_MIN_WINDOW_NS ||
        elapsed_ns > AUDIO_TIME_MAP_MAX_WINDOW_NS)
    {
        return AUDIO_TIME_MAP_WINDOW;
    }
    min_delta = right->frame_lo - upper_left;
    max_delta = upper_right - left->frame_lo;
    if (max_delta > AUDIO_TIME_MAP_MAX_SPAN_FRAMES)
    {
        return AUDIO_TIME_MAP_RANGE;
    }
    /* Each product <=5e17 after window/span checks; no absolute frame index
       is multiplied. The entire endpoint-rate interval must pass the band. */
    if (min_delta * NS_PER_SEC_X_MILLI <
            elapsed_ns * AUDIO_TIME_MAP_MIN_RATE_MILLI_HZ ||
        max_delta * NS_PER_SEC_X_MILLI >
            elapsed_ns * AUDIO_TIME_MAP_MAX_RATE_MILLI_HZ)
    {
        return AUDIO_TIME_MAP_RATE;
    }
    if (frame_position < upper_left || frame_position > right->frame_lo)
    {
        return AUDIO_TIME_MAP_OUTSIDE;
    }
    /* The interpolation fraction decreases as either endpoint phase increases.
       Therefore both upper phases give the earliest time, both lower phases
       the latest time. Close half-open bins conservatively before rounding. */
    early_product = elapsed_ns * (frame_position - upper_left);
    late_product = elapsed_ns * (frame_position - left->frame_lo);
    late_denominator = right->frame_lo - left->frame_lo;
    early_ns = early_product / (upper_right - upper_left);
    late_ns = late_product / late_denominator;
    if (late_product % late_denominator != 0U)
    {
        ++late_ns;
    }
    /* Both offsets are within elapsed_ns. Adding to time_left cannot overflow
       because the valid right PTP timestamp is the absolute upper limit. */
    result->model_lower_ns = time_left_ns + early_ns;
    result->model_upper_ns = time_left_ns + late_ns;
    result->estimate_ns = result->model_lower_ns +
                         (result->model_upper_ns - result->model_lower_ns) / 2U;
    result->valid = true;
    return AUDIO_TIME_MAP_OK;
}
