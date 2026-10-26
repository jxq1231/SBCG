/**
 * @file        bsp_ptp.c
 * @brief       ETH PTP本地时钟读写、接收戳及B板有界驯服接口。
 * @details     保留A板5ns粗更新；B板采用10ns精细更新与名义addend0x80000000，提供频率修正及新鲜读时后的
 *              TSSTI绝对步进。时钟epoch与速率代际帮助上层隔离旧交换、锚点和插值窗口。
 * @note        仅NO_SYS前台，依赖200MHz HCLK、ETH初始化及HAL_ETH_USE_PTP一致编译。校正前由调用方确认
 *              TxClean为空闲并退出网络回调。超时使时钟失效，不承诺已发硬件命令回滚，不证明跨板精度。
 */

#include "bsp_ptp.h"
#ifdef SBCG_USE_PPS
#include "bsp_pps.h"
#endif
#include "main.h"

#ifndef HAL_ETH_USE_PTP
#error "PTP must be enabled consistently for application, HAL and LwIP"
#endif

#define PTP_HCLK_HZ 200000000U
#define PTP_INCREMENT_NS (1000000000U / PTP_HCLK_HZ)
#define PTP_INIT_TIMEOUT_MS 20U
#define PTP_INIT_POLL_LIMIT 100000U
#define PTP_SNAPSHOT_ATTEMPTS 3U
#define PTP_FINE_INCREMENT_NS 10U
#define PTP_NOMINAL_ADDEND UINT32_C(0x80000000)
#define PTP_MAX_CORRECTION_PPB 100000
#define PTP_COMMAND_MASK (ETH_PTPTSCR_TSSTI | ETH_PTPTSCR_TSSTU | ETH_PTPTSCR_TSARU)
#define PTP_MAX_TIME_NS UINT64_C(4294967295999999999)

extern ETH_HandleTypeDef heth;
static uint8_t ready;
static uint32_t clock_epoch, rate_revision;
static uint8_t fine_mode;
static uint32_t expected_addend;
static int32_t applied_frequency_ppb;

/**
 * @brief   将 PTP 时间戳模块标记为不可用并关闭时间戳使能。
 * @param   status 要原样传回给初始化调用者的错误状态。
 * @return  原样返回 status。
 * @note    仅已确认heth.Instance为ETH的命令失败或代际耗尽路径；使epoch/速率代际饱和递增，清TSE和HAL标记。
 *          未关闭PTP时钟门，不清未完成请求位，也不表示硬件命令回滚。
 */
static BSP_PtpStatus DisableClock(BSP_PtpStatus status)
{
#ifdef SBCG_USE_PPS
    if (BSP_PPS_Stop()!=BSP_PPS_OK) { status=BSP_PTP_BAD_CONFIG; }
#endif
    ready = 0U;
    if (clock_epoch != UINT32_MAX) { ++clock_epoch; }
    if (rate_revision != UINT32_MAX) { ++rate_revision; }
    CLEAR_BIT(heth.Instance->PTPTSCR, ETH_PTPTSCR_TSE);
    heth.IsPtpConfigured = HAL_ETH_PTP_NOT_CONFIGURED;
    return status;
}

/**
 * @brief   检查PTP时钟门、当前粗/精细模式、十进制回绕、子秒增量及fine目标addend。
 * @return  非零表示受检查字段匹配；零表示不匹配。
 * @note    依赖有效 ETH 句柄；只校验本模块用到的字段，不表示所有 PTP 配置或跨板时钟同步已成立。
 */
static uint8_t ConfigMatches(void)
{
    const uint32_t mask = ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSSSR | ETH_PTPTSCR_TSFCU;
    const uint32_t mode = fine_mode ? ETH_PTPTSCR_TSFCU : 0U;
    const uint32_t increment_ns = fine_mode ? PTP_FINE_INCREMENT_NS : PTP_INCREMENT_NS;
    return __HAL_RCC_ETHMACPTP_IS_CLK_ENABLED() &&
           ((READ_REG(heth.Instance->PTPTSCR) & mask) ==
            (ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSSSR | mode)) &&
           ((READ_REG(heth.Instance->PTPSSIR) & ETH_PTPSSIR_STSSI) == increment_ns) &&
           (!fine_mode || READ_REG(heth.Instance->PTPTSAR) == expected_addend);
}

/**
 * @brief   为将要发生的时间或速率变化预留不回绕代际。
 * @param   change_epoch 非零表示同时改变时间epoch；速率代际始终改变。
 * @return  OK表示已预留；耗尽时关闭时钟并返回GENERATION_EXHAUSTED。
 * @note    仅前台且ETH句柄已有效；预留在发出命令之前，失败的请求也不能复用旧代际。
 */
static BSP_PtpStatus ReserveGeneration(uint8_t change_epoch)
{
    if (clock_epoch == UINT32_MAX || rate_revision == UINT32_MAX)
    { return DisableClock(BSP_PTP_GENERATION_EXHAUSTED); }
    if (change_epoch) { ++clock_epoch; }
    ++rate_revision;
    return BSP_PTP_OK;
}

/**
 * @brief   以HAL毫秒截止时间及独立轮询上限等待PTP请求完成。
 * @param   request_mask 本次要等待清零的控制寄存器请求位，不可为零。
 * @param   timeout_status 超时要返回的初始化或更新错误状态。
 * @return  OK表示已清零；超时关闭时钟、提升失效代际并返回timeout_status。
 * @note    仅前台；即使HAL tick冻结也最多100000次轮询，任何超时都不承诺回滚。
 */
static BSP_PtpStatus WaitCommand(uint32_t request_mask, BSP_PtpStatus timeout_status)
{
    const uint32_t start_ms = HAL_GetTick();
    uint32_t polls = 0U;
    while ((READ_REG(heth.Instance->PTPTSCR) & request_mask) != 0U)
    {
        if ((uint32_t)(HAL_GetTick() - start_ms) >= PTP_INIT_TIMEOUT_MS ||
            ++polls >= PTP_INIT_POLL_LIMIT)
        { return DisableClock(timeout_status); }
    }
    return BSP_PTP_OK;
}

/**
 * @brief   校验B板fine更新的运行模式、频率前提及无待决命令。
 * @return  OK表示可在当前串行前台发出命令；未就绪、模式丢失、频率错误或忙返回对应状态。
 * @note    不检查TX所有权，调用方必须先确保BSP_PTP_TxClean()==OK且不处于网络包装回调。
 */
static BSP_PtpStatus CheckFineUpdate(void)
{
    if (!ready || !fine_mode || heth.Instance != ETH ||
        heth.IsPtpConfigured != HAL_ETH_PTP_CONFIGURED ||
        (heth.gState != HAL_ETH_STATE_READY && heth.gState != HAL_ETH_STATE_STARTED))
    { return BSP_PTP_NOT_READY; }
    if (HAL_RCC_GetHCLKFreq() != PTP_HCLK_HZ) { return BSP_PTP_BAD_CLOCK; }
    if (!ConfigMatches()) { return BSP_PTP_BAD_CONFIG; }
    if ((READ_REG(heth.Instance->PTPTSCR) & PTP_COMMAND_MASK) != 0U) { return BSP_PTP_BUSY; }
    return BSP_PTP_OK;
}

/**
 * @brief   按当前 200 MHz HCLK 初始化本地 ETH PTP 粗更新时钟。
 * @return  OK 表示 HAL 配置完成且请求位在双重预算内消退；其它区分 ETH、HCLK、HAL、超时和配置错误。
 * @note    仅主循环、MX_LWIP_Init 之后且 HAL tick 运行时调用；增量 5 ns，十进制纳秒回绕并对全部接收帧打戳。开
 *          启 PTP 时钟门是已有实现，不做跨节点校时/驯服，也不改 CubeMX 系统时钟。
 */
BSP_PtpStatus BSP_PTP_Init(void)
{
#ifdef SBCG_USE_PPS
    if (BSP_PPS_Stop()!=BSP_PPS_OK) { return BSP_PTP_BAD_CONFIG; }
#endif
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

    BSP_PtpStatus generation_status = ReserveGeneration(1U);
    if (generation_status != BSP_PTP_OK) { return generation_status; }
    fine_mode = 0U;
    applied_frequency_ppb = 0;

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
 * @brief   按RM0385顺序启动B板10ns精细更新，不调用包含无界等待的HAL配置路径。
 * @return  OK表示先加载名义addend再初始化计数成功；其它状态给出拒绝或有界失败原因。
 * @note    仅启动前台、ETH初始化之后；拒绝在线切换已就绪时钟，不修改RCC频率、RMII、IRQ或DMA。
 */
BSP_PtpStatus BSP_PTP_InitFine(void)
{
#ifdef SBCG_USE_PPS
    if (BSP_PPS_Stop()!=BSP_PPS_OK) { return BSP_PTP_BAD_CONFIG; }
#endif
    if (heth.Instance != ETH || ready || heth.IsPtpConfigured == HAL_ETH_PTP_CONFIGURED ||
        (heth.gState != HAL_ETH_STATE_READY && heth.gState != HAL_ETH_STATE_STARTED))
    { return BSP_PTP_NOT_READY; }
    if (HAL_RCC_GetHCLKFreq() != PTP_HCLK_HZ) { return BSP_PTP_BAD_CLOCK; }
    if ((READ_REG(heth.Instance->PTPTSCR) & PTP_COMMAND_MASK) != 0U) { return BSP_PTP_BUSY; }
    BSP_PtpStatus status = ReserveGeneration(1U);
    if (status != BSP_PTP_OK) { return status; }
    fine_mode = 1U;
    expected_addend = PTP_NOMINAL_ADDEND;
    applied_frequency_ppb = 0;
    __HAL_RCC_ETHMACPTP_CLK_ENABLE();
    /* RM0385 38.7.8：屏蔽触发IRQ，启用计数，先加载addend再选择fine，最后只请求初始化。 */
    SET_BIT(heth.Instance->MACIMR, ETH_MACIMR_TSTIM);
    WRITE_REG(heth.Instance->PTPTSCR, ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSSSR | ETH_PTPTSCR_TSSARFE);
    WRITE_REG(heth.Instance->PTPSSIR, PTP_FINE_INCREMENT_NS);
    WRITE_REG(heth.Instance->PTPTSAR, expected_addend);
    SET_BIT(heth.Instance->PTPTSCR, ETH_PTPTSCR_TSARU);
    status = WaitCommand(ETH_PTPTSCR_TSARU, BSP_PTP_INIT_TIMEOUT);
    if (status != BSP_PTP_OK) { return status; }
    SET_BIT(heth.Instance->PTPTSCR, ETH_PTPTSCR_TSFCU);
    WRITE_REG(heth.Instance->PTPTSHUR, 0U);
    WRITE_REG(heth.Instance->PTPTSLUR, 0U);
    SET_BIT(heth.Instance->PTPTSCR, ETH_PTPTSCR_TSSTI);
    status = WaitCommand(ETH_PTPTSCR_TSSTI, BSP_PTP_INIT_TIMEOUT);
    if (status != BSP_PTP_OK) { return status; }
    if (!ConfigMatches()) { return DisableClock(BSP_PTP_BAD_CONFIG); }
    heth.IsPtpConfigured = HAL_ETH_PTP_CONFIGURED;
    ready = 1U;
    return BSP_PTP_OK;
}

/**
 * @brief   将有界有符号ppb换算为名义addend上的绝对目标并等待加载。
 * @param   correction_ppb -100000..100000的十亿分比，正值加快B时钟。
 * @return  OK表示加载完成或目标未变；失败返回范围、模式、忙、代际或超时状态。
 * @note    仅前台，调用方先确认TX清理完成；64位乘积并对称舍入，不累计旧修正。
 */
BSP_PtpStatus BSP_PTP_SetFrequencyPpb(int32_t correction_ppb)
{
    BSP_PtpStatus status = CheckFineUpdate();
    if (status != BSP_PTP_OK) { return status; }
    if (correction_ppb < -PTP_MAX_CORRECTION_PPB || correction_ppb > PTP_MAX_CORRECTION_PPB)
    { return BSP_PTP_OUT_OF_RANGE; }
    if (correction_ppb == applied_frequency_ppb) { return BSP_PTP_OK; }
    const int64_t product = (int64_t)PTP_NOMINAL_ADDEND * correction_ppb;
    const int64_t delta = product >= 0 ? (product + INT64_C(500000000)) / INT64_C(1000000000) :
        -((-product + INT64_C(500000000)) / INT64_C(1000000000));
    const uint32_t target = (uint32_t)((int64_t)PTP_NOMINAL_ADDEND + delta);
    status = ReserveGeneration(0U);
    if (status != BSP_PTP_OK) { return status; }
    WRITE_REG(heth.Instance->PTPTSAR, target);
    SET_BIT(heth.Instance->PTPTSCR, ETH_PTPTSCR_TSARU);
    status = WaitCommand(ETH_PTPTSCR_TSARU, BSP_PTP_UPDATE_TIMEOUT);
    if (status != BSP_PTP_OK) { return status; }
    expected_addend = target;
    if (!ConfigMatches()) { return DisableClock(BSP_PTP_BAD_CONFIG); }
    applied_frequency_ppb = correction_ppb;
    return BSP_PTP_OK;
}

/**
 * @brief   用一次新鲜快照加有符号校正得到绝对目标，并以TSSTI覆盖计数。
 * @param   correction_ns 有符号纳秒；正值推进，目标不得小于零或超过uint32秒时间域。
 * @return  OK表示覆盖完成或零修正；失败返回读时、范围、模式、忙、代际或超时错误。
 * @note    仅前台且TX已清理；不用TSSTU或HAL_AddTimeOffset。读写延迟保留为后续伺服残差。
 */
BSP_PtpStatus BSP_PTP_Step(int64_t correction_ns)
{
    BSP_PtpStatus status = CheckFineUpdate();
    if (status != BSP_PTP_OK) { return status; }
    if (correction_ns == 0) { return BSP_PTP_OK; }
    BSP_PtpTime snapshot;
    status = BSP_PTP_Read(&snapshot);
    if (status != BSP_PTP_OK) { return status; }
    const uint64_t now_ns = (uint64_t)snapshot.seconds * UINT64_C(1000000000) + snapshot.nanoseconds;
    uint64_t target_ns;
    if (correction_ns < 0)
    {
        const uint64_t magnitude_ns = (uint64_t)(-(correction_ns + 1)) + 1U;
        if (magnitude_ns > now_ns) { return BSP_PTP_OUT_OF_RANGE; }
        target_ns = now_ns - magnitude_ns;
    }
    else
    {
        if ((uint64_t)correction_ns > PTP_MAX_TIME_NS - now_ns) { return BSP_PTP_OUT_OF_RANGE; }
        target_ns = now_ns + (uint64_t)correction_ns;
    }
#ifdef SBCG_USE_PPS
    /* 绝对覆盖可能跨过已装载目标；必须在写TSSTI之前同步停通道并取消目标。 */
    if (BSP_PPS_Stop()!=BSP_PPS_OK) { return BSP_PTP_BAD_CONFIG; }
#endif
    status = ReserveGeneration(1U);
    if (status != BSP_PTP_OK) { return status; }
    WRITE_REG(heth.Instance->PTPTSHUR, (uint32_t)(target_ns / UINT64_C(1000000000)));
    WRITE_REG(heth.Instance->PTPTSLUR, (uint32_t)(target_ns % UINT64_C(1000000000)));
    SET_BIT(heth.Instance->PTPTSCR, ETH_PTPTSCR_TSSTI);
    status = WaitCommand(ETH_PTPTSCR_TSSTI, BSP_PTP_UPDATE_TIMEOUT);
    if (status != BSP_PTP_OK) { return status; }
    if (!ConfigMatches()) { return DisableClock(BSP_PTP_BAD_CONFIG); }
    return BSP_PTP_OK;
}

/**
 * @brief   返回初始化、非零相位步进或硬件命令失败的时间代际。
 * @return  当前非回绕uint32 epoch；数值不证明当前时钟可用。
 * @note    前台纯读，失效饱和到UINT32_MAX后不得继续发出新命令。
 */
uint32_t BSP_PTP_GetEpoch(void) { return clock_epoch; }

/**
 * @brief   返回初始化、实际频率设置变化、步进或失效的模型代际。
 * @return  当前不回绕的速率代际，供上层清理插值窗口。
 * @note    前台纯读，不代表时钟同步状态。
 */
uint32_t BSP_PTP_GetRateRevision(void) { return rate_revision; }

/**
 * @brief   返回最近成功初始化或加载的频率修正记录。
 * @return  有符号ppb，名义fine及coarse初始化后为0。
 * @note    前台纯读；失效后保留历史成功记录，不能据此判断硬件完成状态。
 */
int32_t BSP_PTP_GetFrequencyPpb(void) { return applied_frequency_ppb; }

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
