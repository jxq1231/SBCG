#ifndef SBCG_AUDIO_TIME_MAP_MONITOR_H
#define SBCG_AUDIO_TIME_MAP_MONITOR_H
#include <stdbool.h>
#include <stdint.h>
typedef void (*AudioTimeMapLog)(const char *line, int length);
/* Foreground only. A diagnostic three-anchor holdout observer, not an event
   detector or PCM timestamp query service. No hardware configuration. */
void AudioTimeMapMonitor_Init(AudioTimeMapLog log);
void AudioTimeMapMonitor_Process(uint32_t now_ms, bool processing_allowed);
#endif
