#ifndef SBCG_AUDIO_TIME_MAP_H
#define SBCG_AUDIO_TIME_MAP_H

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_TIME_MAP_MAX_WIDTH_FRAMES 4U
#define AUDIO_TIME_MAP_MIN_WINDOW_NS UINT64_C(100000000)
#define AUDIO_TIME_MAP_MAX_WINDOW_NS UINT64_C(10000000000)
#define AUDIO_TIME_MAP_MAX_SPAN_FRAMES UINT64_C(500000)
/* Outward-rounded millihertz limits around derived 47991.071 Hz +/-1%.
   Sanity limits only: neither instantaneous rate nor acoustic calibration. */
#define AUDIO_TIME_MAP_MIN_RATE_MILLI_HZ UINT64_C(47511160)
#define AUDIO_TIME_MAP_MAX_RATE_MILLI_HZ UINT64_C(48470982)

typedef struct
{
    bool valid;
    uint32_t session;
    uint64_t frame_lo;
    uint64_t frame_hi;
    uint32_t sec;
    uint32_t nsec;
} AudioTimeMapAnchor;

typedef struct
{
    bool valid;
    uint64_t estimate_ns;
    uint64_t model_lower_ns;
    uint64_t model_upper_ns;
} AudioTimeMapResult;

typedef enum
{
    AUDIO_TIME_MAP_OK = 0,
    AUDIO_TIME_MAP_BAD_ARGUMENT,
    AUDIO_TIME_MAP_INVALID_ANCHOR,
    AUDIO_TIME_MAP_SESSION,
    AUDIO_TIME_MAP_ORDER,
    AUDIO_TIME_MAP_WINDOW,
    AUDIO_TIME_MAP_RATE,
    AUDIO_TIME_MAP_OUTSIDE,
    AUDIO_TIME_MAP_RANGE
} AudioTimeMapStatus;

/* Pure function; no HAL, allocation, peripheral access or global state.
   Caller isolates a single BOOT/board before invoking this function: SESSION
   alone is not a globally unique epoch. Input/output storage must not overlap.

   frame_position is an integer coordinate of continuous NDTR frame progress.
   It is NOT a PCM sample's physical/acoustic timestamp. Each input closes its
   phase bin to [frame_lo, frame_hi+1]. Output bounds assume linear progress
   between anchors and cover only phase quantization/integer rounding; unknown
   in-window rate changes, codec/SAI/FIFO delays and synchronization are excluded.
   No extrapolation: target must be bracketed for every possible endpoint phase.
   On failure, a non-NULL result is cleared; valid means model-input validity. */
AudioTimeMapStatus AudioTimeMap_Estimate(const AudioTimeMapAnchor *left,
                                        const AudioTimeMapAnchor *right,
                                        uint32_t target_session,
                                        uint64_t frame_position,
                                        AudioTimeMapResult *result);

#endif
