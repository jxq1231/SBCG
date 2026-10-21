#include "bsp_console.h"
#include "main.h"
#include <stddef.h>

/* Only this adapter knows the physical UART chosen in CubeMX. */
extern UART_HandleTypeDef huart1;
#define CONSOLE_UART_HANDLE huart1
#define CONSOLE_TX_TIMEOUT_MS 25U
#define CONSOLE_RX_CAPACITY 256U
#define CONSOLE_RX_RETRY_MS 10U

/* Single IRQ producer / main consumer. Brief PRIMASK sections preserve the
 * caller's mask and protect pop/flush/rearm, never a UART wait or TX operation.
 * Volatile slots and indices publish each byte in order; no DMA owns these. */
static volatile uint8_t rx_bytes[CONSOLE_RX_CAPACITY];
static volatile uint32_t rx_head, rx_tail;
static volatile bool rx_lost, rx_armed, rx_enabled;
static uint8_t rx_byte;
static uint32_t rx_retry_ms;
static volatile BSP_ConsoleRxStats rx_stats;

static void saturate(volatile uint32_t *counter)
{
    if (*counter != UINT32_MAX) { ++*counter; }
}

static bool arm_receive(void)
{
    rx_armed = HAL_UART_Receive_IT(&CONSOLE_UART_HANDLE, &rx_byte, 1U) == HAL_OK;
    if (!rx_armed)
    {
        saturate(&rx_stats.rearm_errors);
        rx_lost = true;
    }
    return rx_armed;
}

bool BSP_Console_RxInit(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    rx_enabled = rx_armed = false;
    rx_head = rx_tail = 0U;
    rx_lost = false;
    rx_stats.errors = rx_stats.overflows = rx_stats.rearm_errors = 0U;
    rx_retry_ms = HAL_GetTick();
    bool ok = HAL_UART_AbortReceive(&CONSOLE_UART_HANDLE) == HAL_OK;
    if (ok) { rx_enabled = true; ok = arm_receive(); }
    else { saturate(&rx_stats.errors); }
    __set_PRIMASK(mask);
    return ok;
}

void BSP_Console_RxProcess(void)
{
    uint32_t now_ms = HAL_GetTick();
    if ((uint32_t)(now_ms - rx_retry_ms) < CONSOLE_RX_RETRY_MS) { return; }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    rx_retry_ms = now_ms;
    if (rx_enabled && !rx_armed) { (void)arm_receive(); }
    __set_PRIMASK(mask);
}

BSP_ConsoleRxResult BSP_Console_Read(uint8_t *out)
{
    if (out == NULL) { return BSP_CONSOLE_RX_NONE; }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    BSP_ConsoleRxResult result = BSP_CONSOLE_RX_NONE;
    if (rx_lost)
    {
        rx_tail = rx_head;
        rx_lost = false;
        result = BSP_CONSOLE_RX_GAP;
    }
    else if (rx_tail != rx_head)
    {
        *out = rx_bytes[rx_tail];
        rx_tail = (rx_tail + 1U) % CONSOLE_RX_CAPACITY;
        result = BSP_CONSOLE_RX_BYTE;
    }
    __set_PRIMASK(mask);
    return result;
}

void BSP_Console_GetRxStats(BSP_ConsoleRxStats *out)
{
    if (out == NULL) { return; }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    *out = rx_stats;
    __set_PRIMASK(mask);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart != &CONSOLE_UART_HANDLE || !rx_enabled) { return; }
    rx_armed = false;
    /* HAL invokes this callback before ErrorCallback for RXNE+error. Do not
     * rearm here: Receive_IT clears ErrorCode and would hide HAL's ORE abort. */
    if (uart->ErrorCode != HAL_UART_ERROR_NONE) { rx_lost = true; return; }
    if (!rx_lost)
    {
        uint32_t next = (rx_head + 1U) % CONSOLE_RX_CAPACITY;
        if (next == rx_tail)
        { saturate(&rx_stats.overflows); rx_lost = true; }
        else { rx_bytes[rx_head] = rx_byte; rx_head = next; }
    }
    (void)arm_receive();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != &CONSOLE_UART_HANDLE || !rx_enabled) { return; }
    saturate(&rx_stats.errors);
    rx_lost = true;
    /* Blocking ORE ends reception before this callback. For a nonblocking
     * error without RXNE the existing one-byte transfer is still active. */
    rx_armed = uart->RxState != HAL_UART_STATE_READY;
    if (!rx_armed) { (void)arm_receive(); }
}

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
       At 115200 8N1, a 255-byte line takes less than 23 ms.
       The timeout relies on the existing SysTick; never call from an ISR. */
    return HAL_UART_Transmit(&CONSOLE_UART_HANDLE, (const uint8_t *)data,
                             length, CONSOLE_TX_TIMEOUT_MS) == HAL_OK;
}
