#ifndef SBCG_TEMPERATURE_SERVICE_H
#define SBCG_TEMPERATURE_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*TemperatureLog)(const char *line, int length);

typedef struct
{
    bool manual;
    bool valid;
    uint32_t version;
    uint32_t age_ms;
    int32_t temperature_milli_c;
    uint32_t sound_speed_mm_s;
} TemperatureSnapshot;

/* Main-loop only. Sampling is enabled only for the development UID allowlist
 * and a non-NULL logger. Reinitialization releases an owned start-low pulse. */
void TemperatureService_Init(uint32_t now_ms, const uint32_t uid_words[3],
                             TemperatureLog log);
void TemperatureService_Process(uint32_t now_ms);
/* Main-loop query: application UART output must wait until DATA is released. */
bool TemperatureService_IsStartLowActive(void);
/* Main-loop only; mode changes reject an owned start-low pulse. Manual values
 * remain valid until AUTO/reset, even if sensor initialization failed. */
bool TemperatureService_SetManual(uint32_t now_ms, int32_t temperature_milli_c);
bool TemperatureService_UseAuto(void);
void TemperatureService_GetSnapshot(uint32_t now_ms, TemperatureSnapshot *out);

#endif
