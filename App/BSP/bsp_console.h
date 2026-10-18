#ifndef SBCG_BSP_CONSOLE_H
#define SBCG_BSP_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t core_clock_hz;
    uint32_t uid_words[3];
} BSP_ConsoleInfo;

uint32_t BSP_Console_GetTickMs(void);
void BSP_Console_GetInfo(BSP_ConsoleInfo *info);
/* Main context only: returns false on HAL failure; no internal retry. */
bool BSP_Console_Write(const char *data, uint16_t length);

#endif
