/**
 * @file        bsp_ptp.c
 * @brief       本地 ETH PTP 时钟及最近接收帧硬件戳的访问。
 * @details     以 200 MHz HCLK、5 ns 子秒增量建立粗更新本地时钟；通过秒复核读取快照，并从 HAL 最近完成接收帧
 *          缓存提取秒/纳秒。
 * @note        仅前台，依赖 HAL_ETH_USE_PTP 一致编译、已有 ETH/LwIP 初始化和增强描述符。提供本地观测时钟，不
 *          代表跨板同步或声学时间验收。
 */

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

/**
 * @brief   将 PTP 时间戳模块标记为不可用并关闭时间戳使能。
 * @param   status 要原样传回给初始化调用者的错误状态。
 * @return  原样返回 status。
 * @note    仅已确认 heth.Instance 为 ETH 的初始化失败路径调用；清 TSE 和 HAL 配置标记，未关闭已开启的
 *          ETHMACPTP 时钟门。
 */
static BSP_PtpStatus DisableClock(BSP_PtpStatus status)
{
    ready = 0U;
    CLEAR_BIT(heth.Instance->PTPTSCR, ETH_PTPTSCR_TSE);
    heth.IsPtpConfigured = HAL_ETH_PTP_NOT_CONFIGURED;
    return status;
}

/**
 * @brief   检查 PTP 时钟门、粗更新/十进制回绕模式及子秒增量。
 * @return  非零表示受检查字段匹配；零表示不匹配。
 * @note    依赖有效 ETH 句柄；只校验本模块用到的字段，不表示所有 PTP 配置或跨板时钟同步已成立。
 */
static uint8_t ConfigMatches(void)
{
    const uint32_t mask = ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSSSR | ETH_PTPTSCR_TSFCU;
    return __HAL_RCC_ETHMACPTP_IS_CLK_ENABLED() &&
           ((READ_REG(heth.Instance->PTPTSCR) & mask) ==
            (ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSSSR)) &&
           ((READ_REG(heth.Instance->PTPSSIR) & ETH_PTPSSIR_STSSI) == PTP_INCREMENT_NS);
}

/**
 * @brief   按当前 200 MHz HCLK 初始化本地 ETH PTP 粗更新时钟。
 * @return  OK 表示 HAL 配置完成且请求位在双重预算内消退；其它区分 ETH、HCLK、HAL、超时和配置错误。
 * @note    仅主循环、MX_LWIP_Init 之后且 HAL tick 运行时调用；增量 5 ns，十进制纳秒回绕并对全部接收帧打戳。开
 *          启 PTP 时钟门是已有实现，不做跨节点校时/驯服，也不改 CubeMX 系统时钟。
 */
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
    config.TimestampAll = ENABLE;
    config.TimestampSubsecondInc = PTP_INCREMENT_NS;
    /* Coarse local clock with all-frame RX timestamps: no addend update.
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

/**
 * @brief   读取并校验本地 ETH PTP 秒/纳秒快照。
 * @param   time 调用者拥有的输出；NULL 返回 BAD_SNAPSHOT，失败不改原值，成功纳秒范围为 0..999999999。
 * @return  OK 表示秒二次核对一致；其它为未就绪、配置/HAL 失败或三次读取仍跨秒等快照错误。
 * @note    仅前台，依赖成功初始化；HAL 读秒与纳秒后再次检查秒寄存器，避免跨秒混合读。输出只代表本机硬件时钟。
 */
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

/**
 * @brief   提取最近完成接收帧的 HAL 缓存硬件时间戳。
 * @param   time 调用者拥有的输出；NULL 返回 BAD_SNAPSHOT，失败保持原值，成功单位为秒与纳秒。
 * @return  OK 表示配置、完成帧状态和纳秒字段有效；其它为未就绪、配置错误、HAL 失败或快照不合格。
 * @note    仅前台，ETH 须 STARTED，使用增强描述符及全帧接收时间戳；必须在读取下一帧前同步消费此缓存。只校验当
 *          前完成帧，不自行关联 UDP 请求。
 */
BSP_PtpStatus BSP_PTP_ReadRx(BSP_PtpTime *time)
{
    if (time == NULL) { return BSP_PTP_BAD_SNAPSHOT; }
    if (!ready || heth.Instance != ETH || heth.gState != HAL_ETH_STATE_STARTED ||
        heth.IsPtpConfigured != HAL_ETH_PTP_CONFIGURED)
    {
        return BSP_PTP_NOT_READY;
    }
    if (!ConfigMatches() || (READ_REG(heth.Instance->PTPTSCR) & ETH_PTPTSCR_TSSARFE) == 0U ||
        (READ_REG(heth.Instance->DMABMR) & ETH_DMABMR_EDE) == 0U)
    {
        return BSP_PTP_BAD_CONFIG;
    }
    /* This field stores RDES0's value, not an address. In enhanced mode bit
       zero signals extended status; test ES, never its legacy error alias. */
    const uint32_t status = heth.RxDescList.pRxLastRxDesc;
    if ((status & (ETH_DMARXDESC_OWN | ETH_DMARXDESC_LS | ETH_DMARXDESC_ES)) != ETH_DMARXDESC_LS)
    {
        return BSP_PTP_BAD_SNAPSHOT;
    }
    ETH_TimeStampTypeDef sample;
    if (HAL_ETH_PTP_GetRxTimestamp(&heth, &sample) != HAL_OK) { return BSP_PTP_HAL_ERROR; }
    if (sample.TimeStampLow >= 1000000000U) { return BSP_PTP_BAD_SNAPSHOT; }
    *time = (BSP_PtpTime){sample.TimeStampHigh, sample.TimeStampLow};
    return BSP_PTP_OK;
}
