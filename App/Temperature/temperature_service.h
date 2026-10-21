#ifndef SBCG_TEMPERATURE_SERVICE_H
#define SBCG_TEMPERATURE_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*TemperatureLog)(const char *line, int length);

/* Main-loop only. Sampling is enabled only for the development UID allowlist
 * and a non-NULL logger. Reinitialization releases an owned start-low pulse. */
void TemperatureService_Init(uint32_t now_ms, const uint32_t uid_words[3],
                             TemperatureLog log);
void TemperatureService_Process(uint32_t now_ms);
/* Main-loop query: application UART output must wait until DATA is released. */
bool TemperatureService_IsStartLowActive(void);

#endif
