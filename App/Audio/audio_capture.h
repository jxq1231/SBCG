#ifndef SBCG_AUDIO_CAPTURE_H
#define SBCG_AUDIO_CAPTURE_H
#include <stdbool.h>
#include <stdint.h>
typedef void (*AudioCapture_Log)(const char *line, int length);
void AudioCapture_Init(uint32_t now_ms, AudioCapture_Log log);
void AudioCapture_Process(uint32_t now_ms, bool processing_allowed);
#endif
