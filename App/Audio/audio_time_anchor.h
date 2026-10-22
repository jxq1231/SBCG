#ifndef SBCG_AUDIO_TIME_ANCHOR_H
#define SBCG_AUDIO_TIME_ANCHOR_H
#include <stdbool.h>
#include <stdint.h>
#include "audio_time_map.h"

#define AUDIO_ANCHOR_MAX_WIDTH_FRAMES 4U
typedef void (*AudioTimeAnchorLog)(const char *line, int length);
typedef struct
{
    uint32_t sequence;
    uint32_t captured_ms;
    AudioTimeMapAnchor anchor;
} AudioTimeAnchorObservation;
/* Foreground, after audio and local PTP initialization. No peripheral setup. */
void AudioTimeAnchor_Init(uint32_t now_ms, AudioTimeAnchorLog log);
void AudioTimeAnchor_Process(uint32_t now_ms, bool processing_allowed);
/* Foreground-only single-slot handoff. Each attempt replaces the slot; gaps
   remain visible in sequence. Failed attempts contain a cleared invalid anchor.
   NULL does not consume; empty reads clear a non-NULL output and return false. */
bool AudioTimeAnchor_TakeObservation(AudioTimeAnchorObservation *observation);
#endif
