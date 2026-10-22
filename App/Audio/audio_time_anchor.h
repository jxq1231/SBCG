#ifndef SBCG_AUDIO_TIME_ANCHOR_H
#define SBCG_AUDIO_TIME_ANCHOR_H
#include <stdbool.h>
#include <stdint.h>

#define AUDIO_ANCHOR_MAX_WIDTH_FRAMES 4U
typedef void (*AudioTimeAnchorLog)(const char *line, int length);
/* Foreground, after audio and local PTP initialization. No peripheral setup. */
void AudioTimeAnchor_Init(uint32_t now_ms, AudioTimeAnchorLog log);
void AudioTimeAnchor_Process(uint32_t now_ms, bool processing_allowed);
#endif
