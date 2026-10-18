#include "main.h"
#include "BSP/bsp_ethernet.h"
#include "Network/ethernet_monitor.h"
#include "lan8742.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static ETH_TypeDef registers;
ETH_HandleTypeDef heth = { &registers, HAL_ETH_STATE_READY };
static uint32_t phy_regs[32][32];
static uint32_t read_calls;
static uint32_t divider_calls;
static uint32_t write_calls;
static uint32_t tick_ms;
static bool fail_next_read;
static char lines[32][160];
static unsigned int line_count;

uint32_t HAL_GetTick(void) { return tick_ms; }
void HAL_ETH_SetMDIOClockRange(ETH_HandleTypeDef *handle)
{
    assert(handle == &heth);
    ++divider_calls;
}
HAL_StatusTypeDef HAL_ETH_ReadPHYRegister(ETH_HandleTypeDef *handle,
    uint32_t address, uint32_t reg, uint32_t *value)
{
    assert(handle == &heth && address < 32U && reg < 32U);
    ++read_calls;
    if (fail_next_read)
    {
        fail_next_read = false;
        registers.MACMIIAR = ETH_MACMIIAR_MB;
        return HAL_ERROR;
    }
    *value = phy_regs[address][reg];
    return HAL_OK;
}
HAL_StatusTypeDef HAL_ETH_WritePHYRegister(ETH_HandleTypeDef *handle,
    uint32_t address, uint32_t reg, uint32_t value)
{
    (void)handle; (void)address; (void)reg; (void)value;
    ++write_calls;
    return HAL_OK;
}
static void LogLine(const char *line, int length)
{
    assert(length > 1 && length < 160 && line_count < 32U);
    assert(line[length - 2] == '\r' && line[length - 1] == '\n');
    memcpy(lines[line_count], line, (size_t)length);
    lines[line_count++][length] = '\0';
}
static void Reset(void)
{
    for (unsigned int a = 0; a < 32U; ++a)
        for (unsigned int r = 0; r < 32U; ++r) phy_regs[a][r] = 0xFFFFU;
    phy_regs[3][LAN8742_SMR] = 0xE3U;
    phy_regs[3][LAN8742_PHYI1R] = 0x0007U;
    phy_regs[3][LAN8742_PHYI2R] = 0xC131U;
    phy_regs[3][LAN8742_BCR] = LAN8742_BCR_AUTONEGO_EN;
    phy_regs[3][LAN8742_BSR] = 0U;
    phy_regs[3][LAN8742_PHYSCSR] = 0U;
    registers.MACMIIAR = 0U;
    heth.gState = HAL_ETH_STATE_READY;
    read_calls = divider_calls = write_calls = tick_ms = line_count = 0U;
    fail_next_read = false;
}
int main(void)
{
    BSP_EthernetInfo info;
    Reset();
    assert(BSP_Ethernet_Init(&info) == BSP_ETH_OK);
    assert(info.phy_addr == 3U && info.phy_id1 == 7U && info.phy_id2 == 0xC131U);
    assert(info.autoneg_enabled && divider_calls == 1U && write_calls == 0U);
    puts("PASS: discovers actual address, accepts revision bits, reports IDs without PHY writes");

    Reset();
    phy_regs[3][LAN8742_PHYI2R] = 0xC0F0U;
    assert(BSP_Ethernet_Init(&info) == BSP_ETH_ID_MISMATCH);
    puts("PASS: rejects a different PHY model");

    Reset();
    phy_regs[3][LAN8742_SMR] = 0xFFFFU;
    assert(BSP_Ethernet_Init(&info) == BSP_ETH_NOT_FOUND);
    assert(info.phy_id1 == 0xFFFFU && info.phy_id2 == 0xFFFFU);
    puts("PASS: an absent bus returning FFFF is not identified as a PHY");

    Reset();
    fail_next_read = true;
    assert(BSP_Ethernet_Init(&info) == BSP_ETH_IO_ERROR);
    assert(read_calls == 1U);
    puts("PASS: one MDIO failure does not repeat blocking HAL calls on a busy bus");

    Reset();
    EthernetMonitor_Init(0U, LogLine);
    EthernetMonitor_Process(0U);
    assert(line_count == 2U && strstr(lines[0], "INIT=OK,PHY_ADDR=3") != NULL);
    assert(strstr(lines[1], "LINK=DOWN") != NULL);
    phy_regs[3][LAN8742_BSR] = LAN8742_BSR_LINK_STATUS;
    EthernetMonitor_Process(249U);
    assert(line_count == 2U);
    EthernetMonitor_Process(250U);
    assert(strstr(lines[2], "LINK=NEGOTIATING") != NULL);
    phy_regs[3][LAN8742_PHYSCSR] = LAN8742_PHYSCSR_AUTONEGO_DONE | LAN8742_PHYSCSR_100BTX_FD;
    EthernetMonitor_Process(500U);
    assert(strstr(lines[3], "LINK=UP,SPEED_MBPS=100,DUPLEX=FULL") != NULL);
    phy_regs[3][LAN8742_BSR] = 0U;
    EthernetMonitor_Process(750U);
    assert(strstr(lines[4], "LINK=DOWN") != NULL);
    phy_regs[3][LAN8742_BSR] = LAN8742_BSR_LINK_STATUS;
    phy_regs[3][LAN8742_PHYSCSR] = LAN8742_PHYSCSR_AUTONEGO_DONE | LAN8742_PHYSCSR_10BT_HD;
    EthernetMonitor_Process(1000U);
    assert(strstr(lines[5], "LINK=UP,SPEED_MBPS=10,DUPLEX=HALF") != NULL);
    puts("PASS: down, negotiating, 100/full, unplug and 10/half replug are reported");

    EthernetMonitor_Process(1250U);
    assert(line_count == 6U);
    EthernetMonitor_Process(5999U);
    assert(line_count == 6U);
    EthernetMonitor_Process(6250U);
    assert(line_count == 7U);
    puts("PASS: stable links log only at the bounded refresh interval");

    fail_next_read = true;
    EthernetMonitor_Process(6500U);
    assert(line_count == 8U && strstr(lines[7], "LINK=READ_ERROR") != NULL);
    registers.MACMIIAR = 0U;
    EthernetMonitor_Process(8499U);
    assert(line_count == 8U);
    EthernetMonitor_Process(8500U);
    assert(line_count == 10U && strstr(lines[8], "INIT=OK") != NULL);
    assert(strstr(lines[9], "LINK=UP") != NULL);
    puts("PASS: link read failure is visible and recovers through a delayed rescan");

    Reset();
    heth.gState = 0U;
    EthernetMonitor_Init(UINT32_MAX - 999U, LogLine);
    EthernetMonitor_Process(UINT32_MAX - 999U);
    assert(line_count == 1U && read_calls == 0U);
    EthernetMonitor_Process(999U);
    assert(line_count == 1U);
    heth.gState = HAL_ETH_STATE_READY;
    EthernetMonitor_Process(1000U);
    assert(line_count == 3U && strstr(lines[1], "INIT=OK") != NULL);
    puts("PASS: failed initialization retries after 2000 ms, including tick wrap");
    assert(write_calls == 0U);
    return 0;
}
