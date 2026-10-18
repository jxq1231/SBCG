#include "ethernet_monitor.h"
#include "BSP/bsp_ethernet.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>

#define ETH_POLL_PERIOD_MS 250U
#define ETH_RETRY_PERIOD_MS 2000U
#define ETH_REFRESH_PERIOD_MS 5000U

static EthernetMonitor_Log emit_line;
static uint32_t last_attempt_ms;
static uint32_t last_poll_ms;
static uint32_t last_log_ms;
static BSP_EthernetLink last_link;
static bool ready;
static bool have_link;

static const char *StatusName(BSP_EthernetStatus status)
{
    switch (status)
    {
        case BSP_ETH_OK: return "OK";
        case BSP_ETH_NOT_READY: return "NOT_READY";
        case BSP_ETH_NOT_FOUND: return "NOT_FOUND";
        case BSP_ETH_ID_MISMATCH: return "ID_MISMATCH";
        default: return "IO_ERROR";
    }
}

void EthernetMonitor_Init(uint32_t now_ms, EthernetMonitor_Log log_line)
{
    emit_line = log_line;
    ready = false;
    have_link = false;
    last_attempt_ms = now_ms - ETH_RETRY_PERIOD_MS;
    last_poll_ms = last_log_ms = now_ms;
}

void EthernetMonitor_Process(uint32_t now_ms)
{
    char line[160];
    if (emit_line == NULL)
    {
        return;
    }
    if (!ready)
    {
        if ((uint32_t)(now_ms - last_attempt_ms) < ETH_RETRY_PERIOD_MS)
        {
            return;
        }
        BSP_EthernetInfo info;
        const BSP_EthernetStatus status = BSP_Ethernet_Init(&info);
        /* Retry interval is measured from the attempt's start tick. There is
           at most one attempt per call, even after a slow HAL access. */
        last_attempt_ms = now_ms;
        const int length = snprintf(line, sizeof(line),
            "$ETH,INIT=%s,PHY_ADDR=%" PRIu32 ",PHY_ID1=%04" PRIX16
            ",PHY_ID2=%04" PRIX16 ",AUTONEG=%u\r\n",
            StatusName(status), info.phy_addr, info.phy_id1, info.phy_id2,
            info.autoneg_enabled ? 1U : 0U);
        emit_line(line, length);
        if (status != BSP_ETH_OK)
        {
            return;
        }
        ready = true;
        have_link = false;
        last_poll_ms = now_ms - ETH_POLL_PERIOD_MS;
    }

    if ((uint32_t)(now_ms - last_poll_ms) < ETH_POLL_PERIOD_MS)
    {
        return;
    }
    last_poll_ms = now_ms;
    BSP_EthernetLink link;
    if (BSP_Ethernet_ReadLink(&link) != BSP_ETH_OK)
    {
        static const char error_line[] = "$ETH,LINK=READ_ERROR\r\n";
        emit_line(error_line, (int)(sizeof(error_line) - 1U));
        ready = false;
        last_attempt_ms = now_ms;
        return;
    }
    const bool changed = !have_link || (link.state != last_link.state) ||
        (link.speed_mbps != last_link.speed_mbps) ||
        (link.full_duplex != last_link.full_duplex);
    if (changed || ((uint32_t)(now_ms - last_log_ms) >= ETH_REFRESH_PERIOD_MS))
    {
        const char *state = link.state == BSP_ETH_LINK_UP ? "UP" :
            (link.state == BSP_ETH_LINK_NEGOTIATING ? "NEGOTIATING" : "DOWN");
        const char *duplex = link.state != BSP_ETH_LINK_UP ? "UNKNOWN" :
            (link.full_duplex ? "FULL" : "HALF");
        const int length = snprintf(line, sizeof(line),
            "$ETH,LINK=%s,SPEED_MBPS=%" PRIu32 ",DUPLEX=%s\r\n",
            state, link.speed_mbps, duplex);
        emit_line(line, length);
        last_link = link;
        have_link = true;
        last_log_ms = now_ms;
    }
}
