#include "app_main.h"
#include "BSP/bsp_console.h"

#include <inttypes.h>
#include <stdio.h>

#define APP_FIRMWARE_ID "SBCG-M0.2"
#define APP_HEARTBEAT_PERIOD_MS 1000U
#define APP_LOG_LINE_CAPACITY 160U

static uint32_t last_heartbeat_ms;
static uint32_t heartbeat_seq;
/* Also observable in a debugger if the UART cannot send any text. */
static volatile uint32_t app_tx_error_count;

static void SendLine(const char *line, int length)
{
    if ((length <= 0) || ((size_t)length >= APP_LOG_LINE_CAPACITY))
    {
        ++app_tx_error_count;
        return;
    }

    if (!BSP_Console_Write(line, (uint16_t)length))
    {
        ++app_tx_error_count;
    }
}

void App_Init(void)
{
    BSP_ConsoleInfo info;
    char line[APP_LOG_LINE_CAPACITY];

    heartbeat_seq = 0U;
    app_tx_error_count = 0U;
    BSP_Console_GetInfo(&info);
    const int length = snprintf(line, sizeof(line),
        "$BOOT,FW=" APP_FIRMWARE_ID ",HCLK_HZ=%" PRIu32
        ",UID_W0=%08" PRIX32 ",UID_W1=%08" PRIX32 ",UID_W2=%08" PRIX32 "\r\n",
        info.core_clock_hz, info.uid_words[0], info.uid_words[1], info.uid_words[2]);
    SendLine(line, length);
    last_heartbeat_ms = BSP_Console_GetTickMs();
}

void App_Process(void)
{
    const uint32_t now_ms = BSP_Console_GetTickMs();
    if ((uint32_t)(now_ms - last_heartbeat_ms) < APP_HEARTBEAT_PERIOD_MS)
    {
        return;
    }

    /* Unsigned subtraction handles tick wrap. Skip missed periods instead of
       flooding the UART after a delayed main loop or an unsuccessful write. */
    last_heartbeat_ms = now_ms;
    ++heartbeat_seq;
    char line[APP_LOG_LINE_CAPACITY];
    const int length = snprintf(line, sizeof(line),
        "$HEARTBEAT,SEQ=%" PRIu32 ",TICK_MS=%" PRIu32 ",TX_ERRORS=%" PRIu32 "\r\n",
        heartbeat_seq, now_ms, (uint32_t)app_tx_error_count);
    SendLine(line, length);
}
