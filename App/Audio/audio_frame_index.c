#include "audio_frame_index.h"

void AudioFrameIndex_Init(AudioFrameIndex *index, uint32_t now_ms)
{
    *index = (AudioFrameIndex){.previous_ms = now_ms};
}

void AudioFrameIndex_Complete(AudioFrameIndex *index, uint8_t half, uint32_t now_ms)
{
    if (half != index->expected_half) { index->faults |= AUDIO_INDEX_ORDER; }
    if ((uint32_t)(now_ms - index->previous_ms) > AUDIO_INDEX_MAX_INTERVAL_MS)
    { index->faults |= AUDIO_INDEX_LATE; }
    if (index->frames > UINT64_MAX - AUDIO_INDEX_BLOCK_FRAMES)
    { index->faults |= AUDIO_INDEX_OVERFLOW; }
    else { index->frames += AUDIO_INDEX_BLOCK_FRAMES; }
    index->expected_half = half ^ 1U;
    index->previous_ms = now_ms;
}
