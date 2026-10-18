#include "bsp_console.h"
#include "main.h"

/* Only this adapter knows the physical UART chosen in CubeMX. */
extern UART_HandleTypeDef huart1;
#define CONSOLE_UART_HANDLE huart1
#define CONSOLE_TX_TIMEOUT_MS 25U

uint32_t BSP_Console_GetTickMs(void)
{
    return HAL_GetTick();
}

void BSP_Console_GetInfo(BSP_ConsoleInfo *info)
{
    info->core_clock_hz = HAL_RCC_GetHCLKFreq();
    info->uid_words[0] = HAL_GetUIDw0();
    info->uid_words[1] = HAL_GetUIDw1();
    info->uid_words[2] = HAL_GetUIDw2();
}

bool BSP_Console_Write(const char *data, uint16_t length)
{
    /* HAL consumes the stack buffer before returning. No DMA/IRQ owns it.
       At 115200 8N1, even a full 159-byte line takes less than 14 ms.
       The timeout relies on the existing SysTick; never call from an ISR. */
    return HAL_UART_Transmit(&CONSOLE_UART_HANDLE, (const uint8_t *)data,
                             length, CONSOLE_TX_TIMEOUT_MS) == HAL_OK;
}
