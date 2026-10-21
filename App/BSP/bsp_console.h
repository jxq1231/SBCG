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

typedef enum
{
    BSP_CONSOLE_RX_NONE,
    BSP_CONSOLE_RX_BYTE,
    BSP_CONSOLE_RX_GAP
} BSP_ConsoleRxResult;

typedef struct
{
    uint32_t errors;
    uint32_t overflows;
    uint32_t rearm_errors;
} BSP_ConsoleRxStats;

/* Main context only. GAP invalidates the partial command and all queued bytes.
 * HAL callbacks only enqueue bytes/mark loss/rearm; no parsing or UART TX. */
bool BSP_Console_RxInit(void);
void BSP_Console_RxProcess(void);
BSP_ConsoleRxResult BSP_Console_Read(uint8_t *out);
void BSP_Console_GetRxStats(BSP_ConsoleRxStats *out);

#endif
