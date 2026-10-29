#include "BSP/bsp_ptp.h"
#include "main.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

ETH_TypeDef test_eth;
RCC_TypeDef test_rcc;
ETH_HandleTypeDef heth;
static uint32_t tick_ms, hclk_hz, tick_calls, config_calls, read_calls;
static bool stuck, frozen_tick, config_failure, read_failure, wrong_config;
static unsigned int rollover_reads;
static ETH_TimeStampTypeDef rx_stamp;
static bool rx_failure;

HAL_StatusTypeDef HAL_ETH_PTP_GetRxTimestamp(ETH_HandleTypeDef *handle, ETH_TimeStampTypeDef *time)
{
    assert(handle == &heth);
    if (rx_failure) { return HAL_ERROR; }
    *time = rx_stamp;
    return HAL_OK;
}

uint32_t HAL_RCC_GetHCLKFreq(void) { return hclk_hz; }
uint32_t HAL_GetTick(void)
{
    ++tick_calls;
    if (!stuck) { test_eth.PTPTSCR &= ~(ETH_PTPTSCR_TSSTI | ETH_PTPTSCR_TSSTU); }
    if (!frozen_tick) { ++tick_ms; }
    return tick_ms;
}
HAL_StatusTypeDef HAL_ETH_PTP_SetConfig(ETH_HandleTypeDef *handle, ETH_PTP_ConfigTypeDef *c)
{
    assert(handle == &heth);
    ++config_calls;
    assert((test_rcc.AHB1ENR & RCC_AHB1ENR_ETHMACPTPEN) != 0);
    assert(c->Timestamp == ETH_PTPTSCR_TSE && c->TimestampRolloverMode == ENABLE);
    assert(c->TimestampSubsecondInc == 5 && c->TimestampUpdateMode == DISABLE);
    assert(c->TimestampUpdate == DISABLE && c->TimestampAddendUpdate == DISABLE);
    assert(c->TimestampAll == ENABLE && c->TimestampIPv4 == DISABLE);
    if (config_failure) { return HAL_ERROR; }
    test_eth.PTPTSCR = ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSSSR | ETH_PTPTSCR_TSSARFE |
                      ETH_PTPTSCR_TSSTI | ETH_PTPTSCR_TSSTU;
    test_eth.PTPSSIR = wrong_config ? 6 : c->TimestampSubsecondInc;
    handle->IsPtpConfigured = HAL_ETH_PTP_CONFIGURED;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_ETH_PTP_GetTime(ETH_HandleTypeDef *handle, ETH_TimeTypeDef *time)
{
    assert(handle == &heth);
    ++read_calls;
    if (read_failure) { return HAL_ERROR; }
    time->Seconds = test_eth.PTPTSHR;
    time->NanoSeconds = test_eth.PTPTSLR;
    if (rollover_reads > 0)
    {
        /* Rollover between HAL's seconds read and its nanoseconds read. */
        --rollover_reads;
        ++test_eth.PTPTSHR;
        test_eth.PTPTSLR = 5;
        time->NanoSeconds = 5;
    }
    return HAL_OK;
}
static void Reset(void)
{
    memset(&test_eth, 0, sizeof(test_eth));
    memset(&test_rcc, 0, sizeof(test_rcc));
    memset(&heth, 0, sizeof(heth));
    heth.Instance = ETH;
    heth.gState = HAL_ETH_STATE_READY;
    hclk_hz = 200000000;
    tick_ms = config_calls = read_calls = tick_calls = 0;
    stuck = frozen_tick = config_failure = read_failure = wrong_config = false;
    rollover_reads = 0;
    test_eth.DMABMR = ETH_DMABMR_EDE;
    rx_stamp = (ETH_TimeStampTypeDef){500,7};
    rx_failure = false;
}
int main(void)
{
    Reset();
    assert(BSP_PTP_Init() == BSP_PTP_OK);
    assert(config_calls == 1 && test_eth.PTPSSIR == 5);
    BSP_PtpTime time = {99,99};
    test_eth.PTPTSHR = 2; test_eth.PTPTSLR = 999999995;
    assert(BSP_PTP_Read(&time) == BSP_PTP_OK && time.seconds == 2 && time.nanoseconds == 999999995);
    Reset(); heth.gState = HAL_ETH_STATE_STARTED;
    assert(BSP_PTP_Init() == BSP_PTP_OK && heth.gState == HAL_ETH_STATE_STARTED);
    puts("PASS: 200 MHz coarse clock uses 5 ns decimal increment and all-frame RX timestamps");

    Reset(); hclk_hz = 216000000;
    assert(BSP_PTP_Init() == BSP_PTP_BAD_CLOCK && config_calls == 0);
    Reset(); heth.gState = HAL_ETH_STATE_RESET;
    assert(BSP_PTP_Init() == BSP_PTP_NOT_READY && config_calls == 0);
    Reset(); heth.Instance = NULL;
    assert(BSP_PTP_Init() == BSP_PTP_NOT_READY && config_calls == 0);
    Reset(); test_eth.PTPTSCR = ETH_PTPTSCR_TSSTI;
    assert(BSP_PTP_Init() == BSP_PTP_NOT_READY && config_calls == 0);
    puts("PASS: wrong clock, uninitialized ETH and pending PTP commands are rejected before writes");

    Reset(); stuck = true;
    assert(BSP_PTP_Init() == BSP_PTP_INIT_TIMEOUT && tick_calls <= 25);
    assert((test_eth.PTPTSCR & ETH_PTPTSCR_TSE) == 0 && heth.IsPtpConfigured == 0);
    Reset(); stuck = frozen_tick = true;
    assert(BSP_PTP_Init() == BSP_PTP_INIT_TIMEOUT && tick_calls <= 100002);
    puts("PASS: initialization timeout is bounded even when HAL tick stops");

    Reset(); config_failure = true;
    assert(BSP_PTP_Init() == BSP_PTP_HAL_ERROR);
    Reset(); wrong_config = true;
    assert(BSP_PTP_Init() == BSP_PTP_BAD_CONFIG);
    assert(BSP_PTP_Read(&time) == BSP_PTP_NOT_READY);
    puts("PASS: HAL failure and register mismatch do not expose a valid clock");

    Reset(); assert(BSP_PTP_Init() == BSP_PTP_OK);
    test_eth.PTPTSHR = 7; test_eth.PTPTSLR = 999999995; rollover_reads = 1;
    assert(BSP_PTP_Read(&time) == BSP_PTP_OK && time.seconds == 8 && time.nanoseconds == 5);
    assert(read_calls == 2);
    rollover_reads = 3; time.seconds = 99; time.nanoseconds = 99;
    assert(BSP_PTP_Read(&time) == BSP_PTP_BAD_SNAPSHOT && read_calls == 5);
    assert(time.seconds == 99 && time.nanoseconds == 99);
    puts("PASS: second rollover retries and repeated races leave caller output unchanged");

    test_eth.PTPTSLR = 1000000000;
    assert(BSP_PTP_Read(&time) == BSP_PTP_BAD_SNAPSHOT);
    read_failure = true;
    assert(BSP_PTP_Read(&time) == BSP_PTP_HAL_ERROR);
    read_failure = false; test_eth.PTPTSCR &= ~ETH_PTPTSCR_TSSSR;
    assert(BSP_PTP_Read(&time) == BSP_PTP_BAD_CONFIG);
    assert(BSP_PTP_Read(NULL) == BSP_PTP_BAD_SNAPSHOT);
    test_eth.PTPTSCR |= ETH_PTPTSCR_TSSSR;
    test_rcc.AHB1ENR &= ~RCC_AHB1ENR_ETHMACPTPEN;
    assert(BSP_PTP_Read(&time) == BSP_PTP_BAD_CONFIG);
    puts("PASS: invalid nanoseconds, read failure, mode loss and null output are visible");

    Reset(); assert(BSP_PTP_Init() == BSP_PTP_OK);
    heth.gState = HAL_ETH_STATE_STARTED;
    heth.RxDescList.pRxLastRxDesc = ETH_DMARXDESC_LS | 1U;
    assert(BSP_PTP_ReadRx(&time) == BSP_PTP_OK && time.seconds == 7 && time.nanoseconds == 500);
    puts("PASS: RX cache uses hardware seconds/nanoseconds and extended-status bit zero is not an error");

    const uint32_t invalid_status[] = {0,ETH_DMARXDESC_LS|ETH_DMARXDESC_OWN,
                                      ETH_DMARXDESC_LS|ETH_DMARXDESC_ES};
    time = (BSP_PtpTime){99,99};
    for (unsigned int i=0; i<3; ++i)
    {
        heth.RxDescList.pRxLastRxDesc = invalid_status[i];
        assert(BSP_PTP_ReadRx(&time) == BSP_PTP_BAD_SNAPSHOT);
        assert(time.seconds == 99 && time.nanoseconds == 99);
    }
    heth.RxDescList.pRxLastRxDesc = ETH_DMARXDESC_LS;
    rx_stamp.TimeStampLow = 1000000000;
    assert(BSP_PTP_ReadRx(&time) == BSP_PTP_BAD_SNAPSHOT);
    rx_stamp.TimeStampLow = rx_stamp.TimeStampHigh = UINT32_MAX;
    assert(BSP_PTP_ReadRx(&time) == BSP_PTP_BAD_SNAPSHOT);
    rx_failure = true;
    assert(BSP_PTP_ReadRx(&time) == BSP_PTP_HAL_ERROR);
    rx_failure = false;
    test_eth.DMABMR = 0;
    assert(BSP_PTP_ReadRx(&time) == BSP_PTP_BAD_CONFIG);
    test_eth.DMABMR = ETH_DMABMR_EDE;
    test_eth.PTPTSCR &= ~ETH_PTPTSCR_TSSARFE;
    assert(BSP_PTP_ReadRx(&time) == BSP_PTP_BAD_CONFIG);
    assert(BSP_PTP_ReadRx(NULL) == BSP_PTP_BAD_SNAPSHOT);
    assert(time.seconds == 99 && time.nanoseconds == 99);
    puts("PASS: RX rejects missing/owned/error/corrupt snapshots and lost configuration without changing output");
    return 0;
}
