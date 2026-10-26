/**
 * @file        bsp_pps.c
 * @brief       使用ETH目标时间比较与TIM2 PWM1生成前台提前安排的整秒脉冲。
 * @details     校验CubeMX资源；先安全装载计数再使能通道，目标硬件触发产生上升沿，前台消费硬件状态。
 * @note        仅串行前台；不新增IRQ/DMA，不修改GPIO、时钟、模式或remap。MAC时间触发被屏蔽IRQ但内部
 *              比较仍生效。RM0385的独立PPS输出不连接本ITR1；不使用vendor位号有争议的SSR_TSTTR宏。
 *              软件观察不是实物验收；CPU完全停止约4295秒后仍可能因ARR自然回绕产生额外PWM。
 */
#include "bsp_pps.h"
#include "bsp_ptp.h"
#include "main.h"
#include <stddef.h>

#define PPS_HCLK_HZ 200000000U
#define PPS_PCLK1_HZ 50000000U
#define PPS_PRESCALER 99U
#define PPS_WIDTH_US 100U
#define PPS_MARGIN_NS UINT64_C(100000000)
#define PPS_NS_PER_SECOND UINT64_C(1000000000)
#define PPS_NS_PER_MS UINT64_C(1000000)
#define PPS_WAIT_MS 2U
#define PPS_WAIT_POLLS 20000U
#define PPS_SNAPSHOT_ATTEMPTS 32U
#define PPS_PTP_COMMANDS (ETH_PTPTSCR_TSSTI | ETH_PTPTSCR_TSSTU | ETH_PTPTSCR_TSARU)

extern TIM_HandleTypeDef htim2;
static BSP_PpsSnapshot state;
static uint32_t last_target_seconds;
static uint32_t last_target_epoch;
static bool have_last_target;

/**
 * @brief   对诊断计数饱和递增，避免长时间运行后回绕误读。
 * @param   count 本模块拥有的非NULL计数地址。
 * @return  无返回值。
 * @note    仅串行前台，不读硬件。
 */
static void Increment(uint32_t *count)
{ if (*count != UINT32_MAX) { ++*count; } }

/**
 * @brief   校验生成资源和时钟，允许当前通道处于运行或停止状态。
 * @return  true表示本模块依赖的配置匹配。
 * @note    不写任何配置；固定APB1分频4且TIMPRE=0，因此TIM2输入为100MHz。
 */
static bool ConfigMatches(void)
{
    const uint32_t gpio_mode_mask = UINT32_C(3) << 30;
    const uint32_t gpio_af_mask = UINT32_C(15) << 28;
    const uint32_t ccmr_mask = TIM_CCMR1_CC1S | TIM_CCMR1_OC1M | TIM_CCMR1_OC1FE |
                               TIM_CCMR1_OC1PE | TIM_CCMR1_OC1CE;
    const uint32_t cr1_mask = TIM_CR1_DIR | TIM_CR1_CMS | TIM_CR1_CKD | TIM_CR1_OPM |
                              TIM_CR1_UDIS | TIM_CR1_URS | TIM_CR1_ARPE;
    return htim2.Instance == TIM2 && PPS_OUT_GPIO_Port == GPIOA && PPS_OUT_Pin == GPIO_PIN_15 &&
        HAL_RCC_GetHCLKFreq() == PPS_HCLK_HZ && HAL_RCC_GetPCLK1Freq() == PPS_PCLK1_HZ &&
        (READ_REG(RCC->CFGR) & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV4 &&
        (READ_REG(RCC->DCKCFGR1) & RCC_DCKCFGR1_TIMPRE) == 0U &&
        (READ_REG(RCC->APB1ENR) & RCC_APB1ENR_TIM2EN) != 0U &&
        (READ_REG(RCC->AHB1ENR) & RCC_AHB1ENR_GPIOAEN) != 0U &&
        (READ_REG(GPIOA->MODER) & gpio_mode_mask) == (UINT32_C(2) << 30) &&
        (READ_REG(GPIOA->PUPDR) & gpio_mode_mask) == (UINT32_C(2) << 30) &&
        (READ_REG(GPIOA->OTYPER) & GPIO_PIN_15) == 0U &&
        (READ_REG(GPIOA->AFR[1]) & gpio_af_mask) == (UINT32_C(1) << 28) &&
        READ_REG(TIM2->PSC) == PPS_PRESCALER && READ_REG(TIM2->ARR) == UINT32_MAX &&
        READ_REG(TIM2->CCR1) == PPS_WIDTH_US && READ_REG(TIM2->DIER) == 0U &&
        (READ_REG(TIM2->CR1) & cr1_mask) == 0U &&
        (READ_REG(TIM2->CCMR1) & ccmr_mask) == (TIM_OCMODE_PWM1 | TIM_CCMR1_OC1PE) &&
        (READ_REG(TIM2->CCER) & ~TIM_CCER_CC1E) == 0U &&
        (READ_REG(TIM2->SMCR) & (TIM_SMCR_SMS | TIM_SMCR_TS | TIM_SMCR_ECE)) ==
            (TIM_SLAVEMODE_RESET | TIM_TS_ITR1) &&
        (READ_REG(TIM2->OR) & TIM_OR_ITR1_RMP) == TIM_TIM2_ETH_PTP;
}

/**
 * @brief   关闭输出、计数及目标，并验证关闭结果。
 * @return  OK表示已确认关闭；其它表示无法确认停用。
 * @note    不依赖PTP读时或初始化状态，不启用任何时钟；CC1E=0时由CubeMX下拉保持低态。
 */
BSP_PpsStatus BSP_PPS_Stop(void)
{
    /* 运行后时钟门丢失时无法确认关闭输出，不擅自重开CubeMX时钟。 */
    bool safe = !state.configured || (READ_REG(RCC->APB1ENR) & RCC_APB1ENR_TIM2EN) != 0U;
    if (state.armed && (READ_REG(RCC->AHB1ENR) & RCC_AHB1ENR_ETHMACEN) == 0U) { safe = false; }
    if ((READ_REG(RCC->APB1ENR) & RCC_APB1ENR_TIM2EN) != 0U)
    {
        CLEAR_BIT(TIM2->CCER, TIM_CCER_CC1E);
        CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);
        if (htim2.Instance == TIM2 && HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1) != HAL_OK) { safe = false; }
        WRITE_REG(TIM2->SR, ~(TIM_SR_TIF | TIM_SR_UIF | TIM_SR_CC1IF));
        safe = safe && (READ_REG(TIM2->CCER) & TIM_CCER_CC1E) == 0U &&
            (READ_REG(TIM2->CR1) & TIM_CR1_CEN) == 0U;
    }
    if ((READ_REG(RCC->AHB1ENR) & RCC_AHB1ENR_ETHMACEN) != 0U)
    {
        SET_BIT(ETH->MACIMR, ETH_MACIMR_TSTIM);
        CLEAR_BIT(ETH->PTPTSCR, ETH_PTPTSCR_TSITE);
        /* PTPTSSR为读清寄存器；只读一次同时清MACSR.TSTS，不写清除值。 */
        (void)READ_REG(ETH->PTPTSSR);
        safe = safe && (READ_REG(ETH->PTPTSCR) & ETH_PTPTSCR_TSITE) == 0U;
    }
    if (state.armed) { Increment(&state.missed); }
    state.armed = false;
    if (!safe) { Increment(&state.errors); return BSP_PPS_BAD_CONFIG; }
    return BSP_PPS_OK;
}

/**
 * @brief   统计异常并撤销硬件目标与输出。
 * @param   status 需要返回的原始错误码。
 * @return  原始status；停止不能确认时优先返回BAD_CONFIG。
 * @note    仅前台；停止失败已在Stop中计数。
 */
static BSP_PpsStatus Fail(BSP_PpsStatus status)
{
    Increment(&state.errors);
    return BSP_PPS_Stop() == BSP_PPS_OK ? status : BSP_PPS_BAD_CONFIG;
}

/**
 * @brief   校验生成配置并建立停用的诊断状态。
 * @return  OK或配置/停用错误。
 * @note    不调用PTP读时，不改CubeMX配置；初始化会清除本模块的软件累计观察。
 */
BSP_PpsStatus BSP_PPS_Init(void)
{
    const BSP_PpsStatus stop_status = BSP_PPS_Stop();
    state = (BSP_PpsSnapshot){0};
    have_last_target = false;
    if (stop_status != BSP_PPS_OK) { return Fail(stop_status); }
    if (!ConfigMatches()) { return Fail(BSP_PPS_BAD_CONFIG); }
    state.configured = true;
    return BSP_PPS_OK;
}

/**
 * @brief   有界等待运行中的计数器达到低电平对应范围。
 * @param   minimum_us 计数器最小值，单位微秒。
 * @return  true表示达到阈值，false表示2ms或20000次轮询内未达到。
 * @note    独立轮询上限保证HAL tick冻结时仍退出；仅输出关闭预热或硬件脉冲尾部短等待使用。
 */
static bool WaitCounter(uint32_t minimum_us)
{
    const uint32_t start_ms = HAL_GetTick();
    for (uint32_t poll = 0U; poll < PPS_WAIT_POLLS; ++poll)
    {
        if (READ_REG(TIM2->CNT) >= minimum_us) { return true; }
        if ((uint32_t)(HAL_GetTick() - start_ms) >= PPS_WAIT_MS) { return false; }
    }
    return false;
}

/**
 * @brief   在关闭通道时装载影子寄存器并预热计数，再开放低电平PWM输出。
 * @return  OK表示成功运行且未出现待决触发，其它为启动/状态错误。
 * @note    UG只在关闭输出时发出；CNT从100开始，避免软件启动制造上升沿。
 */
static BSP_PpsStatus StartLow(void)
{
    if (BSP_PPS_Stop() != BSP_PPS_OK) { return BSP_PPS_BAD_CONFIG; }
    WRITE_REG(TIM2->EGR, TIM_EGR_UG);
    WRITE_REG(TIM2->CNT, PPS_WIDTH_US);
    SET_BIT(TIM2->CR1, TIM_CR1_CEN);
    if (!WaitCounter(PPS_WIDTH_US + 1U)) { return BSP_PPS_START_TIMEOUT; }
    WRITE_REG(TIM2->SR, ~(TIM_SR_TIF | TIM_SR_UIF | TIM_SR_CC1IF));
    if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK) { return BSP_PPS_START_TIMEOUT; }
    if ((READ_REG(TIM2->CCER) & TIM_CCER_CC1E) == 0U ||
        (READ_REG(TIM2->CR1) & TIM_CR1_CEN) == 0U ||
        (READ_REG(TIM2->SR) & (TIM_SR_TIF | TIM_SR_UIF)) != 0U)
    { return BSP_PPS_TARGET_ERROR; }
    return BSP_PPS_OK;
}

/**
 * @brief   将本机PTP快照转换为不丢失精度的绝对纳秒值。
 * @param   sample 调用者提供的有效只读快照，不可为NULL。
 * @return  uint64范围内的绝对纳秒值。
 * @note    秒为uint32且纳秒已由PTP接口校验。
 */
static uint64_t TimeNs(const BSP_PtpTime *sample)
{ return (uint64_t)sample->seconds * PPS_NS_PER_SECOND + sample->nanoseconds; }

/**
 * @brief   检查目标到达证据，并在预算和调度许可内安排下一整秒。
 * @param   valid_for_ms 当前剩余资格，单位毫秒；0立即停止。
 * @param   schedule_allowed 是否允许新增安排，false仍检查和消费已触发状态。
 * @return  OK或已撤销输出的具体异常。
 * @note    真实边沿由ETH比较经ITR1驱动；触发观察需TIF、TSTS、自清TSITE、目标时间和epoch一致。
 */
BSP_PpsStatus BSP_PPS_Process(uint32_t valid_for_ms, bool schedule_allowed)
{
    if (valid_for_ms == 0U) { return BSP_PPS_Stop(); }
    if (!state.configured) { return BSP_PPS_NOT_READY; }
    if (!ConfigMatches()) { return Fail(BSP_PPS_BAD_CONFIG); }
    BSP_PtpTime sample;
    const uint32_t epoch = BSP_PTP_GetEpoch();
    if ((READ_REG(ETH->PTPTSCR) & PPS_PTP_COMMANDS) != 0U ||
        BSP_PTP_Read(&sample) != BSP_PTP_OK || epoch != BSP_PTP_GetEpoch())
    { return Fail(BSP_PPS_CLOCK_ERROR); }
    uint64_t now_ns = TimeNs(&sample);
    if (state.armed)
    {
        if (epoch != state.epoch) { return Fail(BSP_PPS_CLOCK_ERROR); }
        const uint64_t target_ns = (uint64_t)state.target_seconds * PPS_NS_PER_SECOND;
        bool confirmed = false;
        const uint32_t started_ms = HAL_GetTick();
        /* 目标可在任意两次总线读取间到达。先读标志/CNT，再读新鲜时间；混合快照须有界复核，
           不把旧时间+新标志或旧标志+复位CNT误判为永久故障。无触发的稳定快照可直接保留目标。 */
        for (uint32_t attempt = 0U; attempt < PPS_SNAPSHOT_ATTEMPTS; ++attempt)
        {
            const uint32_t timer_status = READ_REG(TIM2->SR);
            const bool timer_trigger = (timer_status & TIM_SR_TIF) != 0U;
            const bool target_trigger = (READ_REG(ETH->MACSR) & ETH_MACSR_TSTS) != 0U;
            const bool request_active = (READ_REG(ETH->PTPTSCR) & ETH_PTPTSCR_TSITE) != 0U;
            const uint32_t counter_us = READ_REG(TIM2->CNT);
            if (BSP_PTP_Read(&sample) != BSP_PTP_OK || BSP_PTP_GetEpoch() != epoch)
            { return Fail(BSP_PPS_CLOCK_ERROR); }
            now_ns = TimeNs(&sample);
            if (READ_REG(ETH->PTPTTHR) != state.target_seconds || READ_REG(ETH->PTPTTLR) != 0U)
            { return Fail(BSP_PPS_TARGET_ERROR); }
            if (timer_trigger && target_trigger && !request_active && now_ns >= target_ns &&
                now_ns - target_ns < PPS_NS_PER_SECOND)
            { confirmed = true; break; }
            if (!timer_trigger && !target_trigger && request_active && now_ns < target_ns &&
                (timer_status & TIM_SR_UIF) == 0U && counter_us >= PPS_WIDTH_US)
            {
                if (target_ns - now_ns + PPS_MARGIN_NS > (uint64_t)valid_for_ms * PPS_NS_PER_MS)
                { return BSP_PPS_Stop(); }
                return BSP_PPS_OK;
            }
            if ((uint32_t)(HAL_GetTick() - started_ms) >= PPS_WAIT_MS) { break; }
        }
        if (!confirmed) { return Fail(BSP_PPS_TARGET_ERROR); }
        if (!WaitCounter(PPS_WIDTH_US)) { return Fail(BSP_PPS_START_TIMEOUT); }
        Increment(&state.emitted);
        state.armed = false;
        if (BSP_PPS_Stop() != BSP_PPS_OK) { return Fail(BSP_PPS_BAD_CONFIG); }
    }
    if (!schedule_allowed) { return BSP_PPS_OK; }
    uint64_t next_seconds = (uint64_t)sample.seconds + 1U;
    if (next_seconds * PPS_NS_PER_SECOND - now_ns < PPS_MARGIN_NS) { ++next_seconds; }
    if (next_seconds > UINT32_MAX) { return Fail(BSP_PPS_TARGET_ERROR); }
    if (have_last_target && last_target_epoch == epoch && next_seconds <= last_target_seconds)
    { return Fail(BSP_PPS_TARGET_ERROR); }
    const uint64_t target_ns = next_seconds * PPS_NS_PER_SECOND;
    if (target_ns - now_ns + PPS_MARGIN_NS > (uint64_t)valid_for_ms * PPS_NS_PER_MS)
    { return BSP_PPS_Stop(); }
    BSP_PpsStatus status = StartLow();
    if (status != BSP_PPS_OK) { return Fail(status); }
    /* 开放比较前再读时；若短预热跨过100ms装载边界，取消本轮而非补发。 */
    if (BSP_PTP_Read(&sample) != BSP_PTP_OK || BSP_PTP_GetEpoch() != epoch ||
        (READ_REG(ETH->PTPTSCR) & PPS_PTP_COMMANDS) != 0U)
    { return Fail(BSP_PPS_CLOCK_ERROR); }
    if (TimeNs(&sample) + PPS_MARGIN_NS > target_ns) { return BSP_PPS_Stop(); }
    WRITE_REG(ETH->PTPTTHR, (uint32_t)next_seconds);
    WRITE_REG(ETH->PTPTTLR, 0U);
    (void)READ_REG(ETH->PTPTSSR);
    WRITE_REG(TIM2->SR, ~(TIM_SR_TIF | TIM_SR_UIF | TIM_SR_CC1IF));
    state.target_seconds = (uint32_t)next_seconds;
    state.epoch = epoch;
    SET_BIT(ETH->PTPTSCR, ETH_PTPTSCR_TSITE);
    if ((READ_REG(ETH->PTPTSCR) & ETH_PTPTSCR_TSITE) == 0U ||
        (READ_REG(ETH->MACSR) & ETH_MACSR_TSTS) != 0U ||
        (READ_REG(TIM2->SR) & (TIM_SR_TIF | TIM_SR_UIF)) != 0U)
    { return Fail(BSP_PPS_TARGET_ERROR); }
    state.armed = true;
    last_target_seconds = state.target_seconds;
    last_target_epoch = epoch;
    have_last_target = true;
    return BSP_PPS_OK;
}

/**
 * @brief   复制软件观察状态，不读取或清除硬件标志。
 * @param   out 非NULL调用者输出；NULL时不写。
 * @return  已复制为true，NULL为false。
 * @note    仅串行前台，emitted是寄存器证据统计，不是电气边沿时间戳。
 */
bool BSP_PPS_GetSnapshot(BSP_PpsSnapshot *out)
{ if (out == NULL) { return false; } *out = state; return true; }
