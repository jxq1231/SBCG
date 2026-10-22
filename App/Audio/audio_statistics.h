#ifndef SBCG_AUDIO_STATISTICS_H
#define SBCG_AUDIO_STATISTICS_H
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    int16_t minimum, maximum;
    uint32_t peak, rms;
} AudioChannelStatistics;
typedef struct { AudioChannelStatistics channel[2]; } AudioStatistics;

/* Interleaved signed PCM16, two channels. Bounded to UINT16_MAX frames.
   RMS is floor(sqrt(mean(square))); invalid input leaves output unchanged. */
bool AudioStatistics_Compute(const volatile int16_t *samples,
                             uint32_t frame_count, AudioStatistics *output);
#endif
