/**
 * @file        test_bsp_pps.c
 * @brief       使用真实vendor声明与寄存器模型验证PPS的前台安全边界。
 * @details     模拟TIM进展、ETH目标状态读清和HAL启动，检查软件判断与寄存器写入顺序。
 * @note        不模拟引脚电气边沿和硬件时钟精度，不作为实物验收。
 */
#include "BSP/bsp_pps.h"
#include "BSP/bsp_ptp.h"
#include "main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

ETH_TypeDef test_eth;
RCC_TypeDef test_rcc;
TIM_TypeDef test_tim;
GPIO_TypeDef test_gpio;
TIM_HandleTypeDef htim2;
static uint32_t tick_ms, reads, epoch, hclk_hz, pclk_hz, ccr_active, false_edges, starts, acks, arms;
static bool frozen, tick_frozen, ptp_bad, halt_start, reject_stop;
static BSP_PtpTime now;
static volatile uint32_t *trigger_on_read;
static unsigned trigger_skip;
static void Trigger(void);

/** @brief 模拟寄存器读及只读状态清除。 @param reg 有效模拟寄存器。 @return 原始或更新后的值。 @note 不访问硬件。 */
uint32_t PpsTest_Read(volatile uint32_t *reg)
{
    ++reads;
    if (reg==trigger_on_read)
    {
        if (trigger_skip!=0U) { --trigger_skip; }
        else { trigger_on_read=NULL; Trigger();test_tim.CNT=0U; }
    }
    if (reg == &test_tim.CNT && (test_tim.CR1 & TIM_CR1_CEN) && !frozen) { ++test_tim.CNT; }
    const uint32_t result = *reg;
    if (reg == &test_eth.PTPTSSR) { ++acks; test_eth.PTPTSSR = 0U; test_eth.MACSR &= ~ETH_MACSR_TSTS; }
    return result;
}
/** @brief 模拟写入、UG和触发请求顺序。 @param reg 有效寄存器。 @param value 写入值。 @return 无。 @note 检查通道关闭先于计数停止。 */
void PpsTest_Write(volatile uint32_t *reg, uint32_t value)
{
    if (reg == &test_tim.CCER)
    {
        if (reject_stop && !(value & TIM_CCER_CC1E)) { return; }
        if (!(test_tim.CCER & TIM_CCER_CC1E) && (value & TIM_CCER_CC1E) && test_tim.CNT < ccr_active) { ++false_edges; }
    }
    if (reg == &test_tim.CR1 && !(value & TIM_CR1_CEN)) { assert(!(test_tim.CCER & TIM_CCER_CC1E) || reject_stop); }
    if (reg == &test_tim.SR) { test_tim.SR &= value; return; }
    if (reg == &test_tim.EGR && (value & TIM_EGR_UG))
    { assert(!(test_tim.CCER & TIM_CCER_CC1E)); test_tim.CNT = 0U; ccr_active = test_tim.CCR1; test_tim.SR |= TIM_SR_UIF; }
    if (reg == &test_eth.PTPTSCR && !(test_eth.PTPTSCR & ETH_PTPTSCR_TSITE) && (value & ETH_PTPTSCR_TSITE))
    {
        assert(test_eth.PTPTTHR > now.seconds);
        assert(test_eth.PTPTTLR == 0U && !(test_eth.MACSR & ETH_MACSR_TSTS));
        assert(acks > arms && (test_tim.CCER & TIM_CCER_CC1E));
        ++arms;
    }
    *reg = value;
}
/** @brief 模拟HCLK。 @return Hz。 @note 可注入错误频率。 */
uint32_t HAL_RCC_GetHCLKFreq(void) { return hclk_hz; }
/** @brief 模拟PCLK1。 @return Hz。 @note 与RCC分频共同校验。 */
uint32_t HAL_RCC_GetPCLK1Freq(void) { return pclk_hz; }
/** @brief 模拟可冻结毫秒时钟。 @return ms。 @note 不负责推进PTP时间。 */
uint32_t HAL_GetTick(void) { if (!tick_frozen) { ++tick_ms; } return tick_ms; }
/** @brief 模拟HAL启动。 @param h 模拟句柄。 @param channel 仅CH1。 @return HAL结果。 @note 保留真实使能顺序。 */
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *h, uint32_t channel)
{
    assert(h == &htim2 && channel == TIM_CHANNEL_1); ++starts;
    if (halt_start) { return HAL_ERROR; }
    SET_BIT(h->Instance->CCER, TIM_CCER_CC1E); SET_BIT(h->Instance->CR1, TIM_CR1_CEN);
    h->ChannelState[0] = HAL_TIM_CHANNEL_STATE_BUSY; return HAL_OK;
}
/** @brief 模拟HAL停止。 @param h 模拟句柄。 @param channel 仅CH1。 @return HAL_OK。 @note 真实停止顺序。 */
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *h, uint32_t channel)
{
    assert(h == &htim2 && channel == TIM_CHANNEL_1);
    CLEAR_BIT(h->Instance->CCER, TIM_CCER_CC1E); CLEAR_BIT(h->Instance->CR1, TIM_CR1_CEN);
    h->ChannelState[0] = HAL_TIM_CHANNEL_STATE_READY; return HAL_OK;
}
/** @brief 提供模拟本地PTP快照。 @param out 非NULL输出。 @return 可注入错误。 @note 不推进秒时间。 */
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *out) { if (ptp_bad) { return BSP_PTP_BAD_CONFIG; } *out = now; return BSP_PTP_OK; }
/** @brief 返回模拟epoch。 @return 代际。 @note 由测试推进。 */
uint32_t BSP_PTP_GetEpoch(void) { return epoch; }

/** @brief 构造与生成配置一致的独立场景。 @return 无。 @note 计数模型不证明时序。 */
static void Reset(void)
{
    memset(&test_eth, 0, sizeof test_eth); memset(&test_rcc, 0, sizeof test_rcc);
    memset(&test_tim, 0, sizeof test_tim); memset(&test_gpio, 0, sizeof test_gpio); memset(&htim2, 0, sizeof htim2);
    htim2.Instance = TIM2; htim2.ChannelState[0] = HAL_TIM_CHANNEL_STATE_READY;
    test_rcc.AHB1ENR = RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_ETHMACEN | RCC_AHB1ENR_ETHMACPTPEN;
    test_rcc.APB1ENR = RCC_APB1ENR_TIM2EN; test_rcc.CFGR = RCC_CFGR_PPRE1_DIV4;
    test_gpio.MODER = 2U << 30; test_gpio.PUPDR = 2U << 30; test_gpio.AFR[1] = GPIO_AF1_TIM2 << 28;
    test_tim.PSC = 99U; test_tim.ARR = UINT32_MAX; test_tim.CCR1 = 100U;
    test_tim.CCMR1 = TIM_OCMODE_PWM1 | TIM_CCMR1_OC1PE;
    test_tim.SMCR = TIM_SLAVEMODE_RESET | TIM_TS_ITR1 | TIM_SMCR_MSM;
    test_tim.OR = TIM_TIM2_ETH_PTP;
    test_eth.PTPTSCR = ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSSSR;
    tick_ms = reads = ccr_active = false_edges = starts = acks = arms = 0U;
    epoch = 1U; hclk_hz = 200000000U; pclk_hz = 50000000U;
    frozen = tick_frozen = ptp_bad = halt_start = reject_stop = false;
    now = (BSP_PtpTime){10U, 200000000U};trigger_on_read=NULL;trigger_skip=0U;
}
/** @brief 复制状态并检查非空约定。 @return 快照。 @note 不读取状态寄存器。 */
static BSP_PpsSnapshot Snap(void) { BSP_PpsSnapshot s; assert(BSP_PPS_GetSnapshot(&s)); return s; }
/** @brief 检查停用结果。 @return 无。 @note 不等同引脚电气检查。 */
static void Stopped(void)
{ assert(!(test_tim.CCER & TIM_CCER_CC1E) && !(test_tim.CR1 & TIM_CR1_CEN) && !(test_eth.PTPTSCR & ETH_PTPTSCR_TSITE)); assert(!Snap().armed); }
/** @brief 模拟整秒目标硬件触发。 @return 无。 @note TIF与TSTS为独立观察，SSR故意不使用vendor错误宏。 */
static void Trigger(void)
{
    now.seconds = Snap().target_seconds; now.nanoseconds = 200000U;
    test_eth.PTPTSCR &= ~ETH_PTPTSCR_TSITE; test_eth.MACSR |= ETH_MACSR_TSTS;
    test_eth.PTPTSSR = 2U; test_tim.SR |= TIM_SR_TIF | TIM_SR_UIF; test_tim.CNT = 200U;
}
/** @brief 执行边界和故障场景。 @return 通过为0。 @note 首次RED用测试提供的缺失功能替代观察断言失败。 */
int main(void)
{
    Reset(); assert(BSP_PPS_Stop() == BSP_PPS_OK); Stopped();
    assert(!BSP_PPS_GetSnapshot(NULL));
    assert(BSP_PPS_Init() == BSP_PPS_OK); assert(Snap().configured); Stopped();
    assert(BSP_PPS_Process(2000U, false) == BSP_PPS_OK && !Snap().armed && starts == 0U);
    assert(BSP_PPS_Process(2000U, true) == BSP_PPS_OK && Snap().armed && Snap().target_seconds == 11U);
    assert(starts == 1U && false_edges == 0U && ccr_active == 100U);
    const uint32_t old_arms = arms;
    assert(BSP_PPS_Process(2000U, true) == BSP_PPS_OK && arms == old_arms);
    Trigger(); assert(BSP_PPS_Process(2000U, false) == BSP_PPS_OK && Snap().emitted == 1U);
    assert(!Snap().armed && arms == old_arms); Stopped();
    assert(BSP_PPS_Process(2000U, true) == BSP_PPS_OK && Snap().target_seconds == 12U);
    assert(BSP_PPS_Process(0U, false) == BSP_PPS_OK); Stopped();
    puts("PASS: safe pre-init stop, real-state trigger observation, ack before rearm and no software pulse");

    Reset(); assert(BSP_PPS_Init() == BSP_PPS_OK);
    assert(BSP_PPS_Process(899U, true) == BSP_PPS_OK && !Snap().armed);
    assert(BSP_PPS_Process(900U, true) == BSP_PPS_OK && Snap().armed);
    assert(BSP_PPS_Process(100U, false) == BSP_PPS_OK); Stopped();
    now.nanoseconds = 900000001U;
    assert(BSP_PPS_Process(1200U, true) == BSP_PPS_OK && Snap().target_seconds == 12U);
    puts("PASS: eligibility budget includes target distance plus 100ms and enforces at least 100ms lead");

    Reset(); assert(BSP_PPS_Init() == BSP_PPS_OK); frozen = tick_frozen = true;
    assert(BSP_PPS_Process(2000U, true) == BSP_PPS_START_TIMEOUT); assert(reads < 1000000U); Stopped();
    Reset(); assert(BSP_PPS_Init() == BSP_PPS_OK); halt_start = true;
    assert(BSP_PPS_Process(2000U, true) != BSP_PPS_OK); Stopped();
    puts("PASS: frozen timer/frozen tick and HAL-start failure are finite and fail closed");

    Reset(); assert(BSP_PPS_Init() == BSP_PPS_OK); assert(BSP_PPS_Process(2000U, true) == BSP_PPS_OK);
    ++epoch; assert(BSP_PPS_Process(2000U, false) == BSP_PPS_CLOCK_ERROR); Stopped();
    Reset(); assert(BSP_PPS_Init() == BSP_PPS_OK); assert(BSP_PPS_Process(2000U, true) == BSP_PPS_OK);
    ptp_bad = true; assert(BSP_PPS_Process(2000U, false) == BSP_PPS_CLOCK_ERROR); Stopped();
    Reset(); assert(BSP_PPS_Init() == BSP_PPS_OK); assert(BSP_PPS_Process(2000U, true) == BSP_PPS_OK);
    test_eth.PTPTSCR |= ETH_PTPTSCR_TSSTI;
    assert(BSP_PPS_Process(2000U, false) == BSP_PPS_CLOCK_ERROR); Stopped();
    Reset(); assert(BSP_PPS_Init() == BSP_PPS_OK); assert(BSP_PPS_Process(2000U, true) == BSP_PPS_OK);
    Trigger(); test_tim.SR &= ~TIM_SR_TIF;
    assert(BSP_PPS_Process(2000U, true) == BSP_PPS_TARGET_ERROR && Snap().emitted == 0U); Stopped();
    Reset(); assert(BSP_PPS_Init() == BSP_PPS_OK); assert(BSP_PPS_Process(2000U, true) == BSP_PPS_OK);
    test_tim.CNT = 0U; test_tim.SR |= TIM_SR_UIF;
    assert(BSP_PPS_Process(2000U, true) == BSP_PPS_TARGET_ERROR && Snap().emitted == 0U); Stopped();
    puts("PASS: epoch/health/pending command mismatch, incomplete trigger and natural overflow never claim emission");

    Reset(); test_gpio.PUPDR = 0U; assert(BSP_PPS_Init() == BSP_PPS_BAD_CONFIG); Stopped();
    Reset(); test_tim.DIER = TIM_DIER_TIE; assert(BSP_PPS_Init() == BSP_PPS_BAD_CONFIG); Stopped();
    Reset(); test_tim.DIER = TIM_DIER_TDE; assert(BSP_PPS_Init() == BSP_PPS_BAD_CONFIG); Stopped();
    Reset(); test_rcc.DCKCFGR1 = RCC_DCKCFGR1_TIMPRE; assert(BSP_PPS_Init() == BSP_PPS_BAD_CONFIG); Stopped();
    Reset(); hclk_hz = 216000000U; assert(BSP_PPS_Init() == BSP_PPS_BAD_CONFIG); Stopped();
    Reset(); test_tim.OR = 0U; assert(BSP_PPS_Init() == BSP_PPS_BAD_CONFIG); Stopped();
    Reset(); assert(BSP_PPS_Init() == BSP_PPS_OK); assert(BSP_PPS_Process(2000U, true) == BSP_PPS_OK);
    reject_stop = true; assert(BSP_PPS_Stop() != BSP_PPS_OK); reject_stop = false; assert(BSP_PPS_Stop() == BSP_PPS_OK);
    Reset(); assert(BSP_PPS_Init() == BSP_PPS_OK);
    test_rcc.APB1ENR &= ~RCC_APB1ENR_TIM2EN;
    assert(BSP_PPS_Stop() == BSP_PPS_BAD_CONFIG);
    puts("PASS: generated resource mutations and non-confirmable stop are rejected");
    for (unsigned race=0U;race<4U;++race)
    {
        Reset();assert(BSP_PPS_Init()==BSP_PPS_OK);assert(BSP_PPS_Process(2000U,true)==BSP_PPS_OK);
        volatile uint32_t *points[]={&test_tim.SR,&test_eth.MACSR,&test_eth.PTPTSCR,&test_tim.CNT};
        trigger_on_read=points[race];trigger_skip=race==2U ? 1U:0U;tick_frozen=true;
        assert(BSP_PPS_Process(2000U,true)==BSP_PPS_OK);
        assert(Snap().emitted==1U && Snap().errors==0U && Snap().target_seconds==12U && false_edges==0U);
    }
    puts("PASS: hardware trigger during TIF/TSTS/TSITE/CNT reads is consistently re-read without false fault or duplicate");
    return 0;
}
