#include "app_main.h"
#include "BSP/bsp_console.h"
#include "Network/ethernet_monitor.h"
#include "Network/udp_heartbeat.h"
#include "Network/udp_probe.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t tick_ms;
static uint32_t write_count;
static bool write_ok;
static char last_line[160];
static uint32_t monitor_calls;
static EthernetMonitor_Log monitor_log;
static uint32_t lwip_calls;
static uint32_t last_lwip_tick_ms;
static uint32_t udp_calls;
static bool udp_initialized;
static bool probe_initialized;
static uint32_t probe_calls;

void UDPProbe_Init(uint32_t now_ms, const uint32_t uid_words[3], UDPProbe_Log log)
{
    assert(now_ms == tick_ms && log != NULL);
    assert(uid_words[0] == 0x001A002DU && uid_words[2] == 0x36363631U);
    probe_initialized = true;
    probe_calls = 0;
}
void UDPProbe_Process(uint32_t now_ms)
{
    assert(probe_initialized && now_ms == tick_ms);
    assert(lwip_calls > 0 && last_lwip_tick_ms == now_ms);
    ++probe_calls;
}

void UDPHeartbeat_Init(uint32_t now_ms, const uint32_t uid_words[3], UDPHeartbeat_Log log)
{
    assert(now_ms == tick_ms && log != NULL);
    assert(uid_words[0] == 0x001A002DU && uid_words[1] == 0x30365105U &&
           uid_words[2] == 0x36363631U);
    udp_initialized = true;
    udp_calls = 0;
}

void UDPHeartbeat_Process(uint32_t now_ms)
{
    assert(udp_initialized && now_ms == tick_ms);
    assert(lwip_calls > 0U && last_lwip_tick_ms == now_ms);
    ++udp_calls;
}

void MX_LWIP_Process(void)
{
    ++lwip_calls;
    last_lwip_tick_ms = tick_ms;
}

void EthernetMonitor_Init(uint32_t now_ms, EthernetMonitor_Log log_line)
{
    assert(now_ms == tick_ms);
    monitor_calls = 0U;
    lwip_calls = 0U;
    monitor_log = log_line;
}

void EthernetMonitor_Process(uint32_t now_ms)
{
    assert(now_ms == tick_ms && monitor_log != NULL);
    assert(lwip_calls > 0U && last_lwip_tick_ms == tick_ms);
    ++monitor_calls;
}

uint32_t BSP_Console_GetTickMs(void)
{
    return tick_ms;
}

void BSP_Console_GetInfo(BSP_ConsoleInfo *info)
{
    info->core_clock_hz = 200000000U;
    info->uid_words[0] = 0x001A002DU;
    info->uid_words[1] = 0x30365105U;
    info->uid_words[2] = 0x36363631U;
}

bool BSP_Console_Write(const char *data, uint16_t length)
{
    assert(length > 0U && length < sizeof(last_line));
    assert(data[length - 2U] == '\r' && data[length - 1U] == '\n');
    memcpy(last_line, data, length);
    last_line[length] = '\0';
    ++write_count;
    return write_ok;
}

static void Reset(uint32_t start_ms, bool initial_write_ok)
{
    tick_ms = start_ms;
    write_count = 0U;
    write_ok = initial_write_ok;
    udp_initialized = false;
    probe_initialized = false;
    App_Init();
}

int main(void)
{
    Reset(0U, true);
    assert(write_count == 1U);
    assert(udp_initialized);
    assert(probe_initialized);
    assert(strcmp(last_line,
        "$BOOT,FW=SBCG-M1.3-UDP,HCLK_HZ=200000000,UID_W0=001A002D,"
        "UID_W1=30365105,UID_W2=36363631\r\n") == 0);
    puts("PASS: startup identifies firmware, clock and UID");

    App_Process();
    tick_ms = 999U;
    App_Process();
    assert(write_count == 1U);
    assert(monitor_calls == 2U);
    assert(lwip_calls == 2U);
    assert(udp_calls == 2U);
    assert(probe_calls == 2U);
    tick_ms = 1000U;
    App_Process();
    assert(write_count == 2U);
    assert(strcmp(last_line,
        "$HEARTBEAT,SEQ=1,TICK_MS=1000,TX_ERRORS=0\r\n") == 0);
    App_Process();
    assert(write_count == 2U);
    puts("PASS: no heartbeat before 1000 ms, one at the deadline");

    Reset(UINT32_MAX - 499U, true);
    tick_ms = 499U;
    App_Process();
    assert(write_count == 1U);
    tick_ms = 500U;
    App_Process();
    assert(write_count == 2U);
    assert(strstr(last_line, "SEQ=1,TICK_MS=500,") != NULL);
    puts("PASS: heartbeat scheduling survives uint32 tick wrap");

    Reset(0U, true);
    tick_ms = 10000U;
    App_Process();
    assert(write_count == 2U);
    tick_ms = 10001U;
    App_Process();
    assert(write_count == 2U);
    tick_ms = 11000U;
    App_Process();
    assert(write_count == 3U);
    puts("PASS: delayed main loop does not burst missed heartbeats");

    Reset(0U, false);
    tick_ms = 1000U;
    App_Process();
    assert(write_count == 2U);
    assert(strstr(last_line, "TX_ERRORS=1\r\n") != NULL);
    App_Process();
    assert(write_count == 2U);
    write_ok = true;
    tick_ms = 2000U;
    App_Process();
    assert(write_count == 3U);
    assert(strcmp(last_line,
        "$HEARTBEAT,SEQ=2,TICK_MS=2000,TX_ERRORS=2\r\n") == 0);
    tick_ms = 3000U;
    App_Process();
    assert(strstr(last_line, "TX_ERRORS=2\r\n") != NULL);
    puts("PASS: failures are counted and recovery remains rate limited");
    return 0;
}
