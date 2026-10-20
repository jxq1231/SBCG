#include "bsp_ptp.h"
#include "main.h"

#ifndef HAL_ETH_USE_PTP
#error "PTP must be enabled consistently for application, HAL and LwIP"
#endif

#define PTP_HCLK_HZ 200000000U
#define PTP_INCREMENT_NS (1000000000U / PTP_HCLK_HZ)
#define PTP_INIT_TIMEOUT_MS 20U
#define PTP_INIT_POLL_LIMIT 100000U
#define PTP_SNAPSHOT_ATTEMPTS 3U

extern ETH_HandleTypeDef heth;
static uint8_t ready;

static BSP_PtpStatus DisableClock(BSP_PtpStatus status)
{
    ready = 0U;
    CLEAR_BIT(heth.Instance->PTPTSCR, ETH_PTPTSCR_TSE);
    heth.IsPtpConfigured = HAL_ETH_PTP_NOT_CONFIGURED;
    return status;
}

static uint8_t ConfigMatches(void)
{
    const uint32_t mask = ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSSSR | ETH_PTPTSCR_TSFCU;
    return __HAL_RCC_ETHMACPTP_IS_CLK_ENABLED() &&
           ((READ_REG(heth.Instance->PTPTSCR) & mask) ==
            (ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSSSR)) &&
           ((READ_REG(heth.Instance->PTPSSIR) & ETH_PTPSSIR_STSSI) == PTP_INCREMENT_NS);
}

BSP_PtpStatus BSP_PTP_Init(void)
{
    ready = 0U;
    if ((heth.Instance != ETH) ||
        ((heth.gState != HAL_ETH_STATE_READY) && (heth.gState != HAL_ETH_STATE_STARTED)))
    {
        return BSP_PTP_NOT_READY;
    }
    if (HAL_RCC_GetHCLKFreq() != PTP_HCLK_HZ)
    {
        return BSP_PTP_BAD_CLOCK;
    }
    /* RM0385 requires no pending initialize/update before a new update. */
    if ((READ_REG(heth.Instance->PTPTSCR) &
         (ETH_PTPTSCR_TSSTI | ETH_PTPTSCR_TSSTU | ETH_PTPTSCR_TSARU)) != 0U)
    {
        return BSP_PTP_NOT_READY;
    }

    /* CubeMX's __HAL_RCC_ETH_CLK_ENABLE covers MAC/TX/RX, not the PTP gate. */
    __HAL_RCC_ETHMACPTP_CLK_ENABLE();
    ETH_PTP_ConfigTypeDef config = {0};
    config.Timestamp = ETH_PTPTSCR_TSE;
    config.TimestampRolloverMode = ENABLE;
    config.TimestampSubsecondInc = PTP_INCREMENT_NS;
    /* Coarse local clock only: no addend update or RX frame timestamping.
       Keeping AddendUpdate disabled avoids the vendor's unbounded loop. */
    if (HAL_ETH_PTP_SetConfig(&heth, &config) != HAL_OK)
    {
        return DisableClock(BSP_PTP_HAL_ERROR);
    }

    /* This HAL sets both initialize/update requests. Wait for completion,
       with a poll cap as well as a deadline in case SysTick is unavailable. */
    const uint32_t start_ms = HAL_GetTick();
    uint32_t polls = 0U;
    while ((READ_REG(heth.Instance->PTPTSCR) &
            (ETH_PTPTSCR_TSSTI | ETH_PTPTSCR_TSSTU)) != 0U)
    {
        if (((uint32_t)(HAL_GetTick() - start_ms) >= PTP_INIT_TIMEOUT_MS) ||
            (++polls >= PTP_INIT_POLL_LIMIT))
        {
            return DisableClock(BSP_PTP_INIT_TIMEOUT);
        }
    }
    if (!ConfigMatches())
    {
        return DisableClock(BSP_PTP_BAD_CONFIG);
    }
    ready = 1U;
    return BSP_PTP_OK;
}

BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *time)
{
    if (time == NULL) { return BSP_PTP_BAD_SNAPSHOT; }
    if (!ready || heth.Instance != ETH || heth.IsPtpConfigured != HAL_ETH_PTP_CONFIGURED)
    {
        return BSP_PTP_NOT_READY;
    }
    if (!ConfigMatches()) { return BSP_PTP_BAD_CONFIG; }
    for (uint32_t attempt = 0U; attempt < PTP_SNAPSHOT_ATTEMPTS; ++attempt)
    {
        ETH_TimeTypeDef sample;
        if (HAL_ETH_PTP_GetTime(&heth, &sample) != HAL_OK)
        {
            return BSP_PTP_HAL_ERROR;
        }
        /* HAL reads seconds before nanoseconds. Recheck seconds AFTER both;
           comparing two HAL samples alone can miss rollover in the second. */
        if (sample.Seconds == READ_REG(heth.Instance->PTPTSHR))
        {
            if (sample.NanoSeconds >= 1000000000U) { return BSP_PTP_BAD_SNAPSHOT; }
            *time = (BSP_PtpTime){sample.Seconds, sample.NanoSeconds};
            return BSP_PTP_OK;
        }
    }
    return BSP_PTP_BAD_SNAPSHOT;
}
