#ifndef SBCG_CODEC_MONITOR_H
#define SBCG_CODEC_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*CodecMonitor_Log)(const char *line, int length);
void CodecMonitor_Init(uint32_t now_ms, CodecMonitor_Log log);
/* io_allowed must be false throughout the DHT11 start-low interval. */
void CodecMonitor_Process(uint32_t now_ms, bool io_allowed);

#endif
