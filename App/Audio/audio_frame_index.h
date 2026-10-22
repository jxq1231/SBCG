#ifndef SBCG_AUDIO_FRAME_INDEX_H
#define SBCG_AUDIO_FRAME_INDEX_H
#include <stdint.h>

#define AUDIO_INDEX_BLOCK_FRAMES 8192U
#define AUDIO_INDEX_MAX_INTERVAL_MS 250U
#define AUDIO_INDEX_ORDER 1U
#define AUDIO_INDEX_LATE 2U
#define AUDIO_INDEX_OVERFLOW 4U
#define AUDIO_INDEX_HAL_ERROR 8U

/* Counts observed DMA boundaries, not an independently measured sample clock.
   Single writer; callers must synchronize copies shared with IRQ context. */
typedef struct
{
    uint64_t frames;
    uint32_t faults, previous_ms;
    uint8_t expected_half;
} AudioFrameIndex;
void AudioFrameIndex_Init(AudioFrameIndex *index, uint32_t now_ms);
void AudioFrameIndex_Complete(AudioFrameIndex *index, uint8_t half, uint32_t now_ms);
#endif
