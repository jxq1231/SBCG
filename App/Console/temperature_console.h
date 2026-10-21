#ifndef SBCG_TEMPERATURE_CONSOLE_H
#define SBCG_TEMPERATURE_CONSOLE_H
#include <stdint.h>
typedef void (*TemperatureConsole_Log)(const char *line, int length);
/* Main context: bounded work, no processing during the sensor start pulse. */
void TemperatureConsole_Init(TemperatureConsole_Log log);
void TemperatureConsole_Process(uint32_t now_ms);
#endif
