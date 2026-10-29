/**
 * @file        test_bsp_ptp_fine.c
 * @brief       以实际STM32声明和模拟寄存器完成机制验证B板PTP接口。
 * @details     检查启动顺序、正负修正、范围、代际、失败失效及冻结tick下的有限等待；不访问实物。
 * @note        在同一翻译单元纳入BSP实现以直接构造代际耗尽，不向生产接口加入测试注入方法。
 */
#include "BSP/bsp_ptp.h"
#include "main.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Including the implementation permits direct exhaustion tests of private
   generations without billions of hardware initialization commands. */
#include "../../App/BSP/bsp_ptp.c"

ETH_TypeDef test_eth;
RCC_TypeDef test_rcc;
ETH_HandleTypeDef heth;
static uint32_t tick_ms, tick_calls, writes, hal_config_calls, commands, stuck_mask;
static uint32_t requests[8], targets_sec[8], targets_ns[8], expected_hclk;
static bool frozen_tick, read_failure;

/**
 * @brief   记录并模拟一次寄存器写入。
 * @param   reg 模拟寄存器地址，不可为NULL。
 * @param   value 要写入的32位数值。
 * @return  无返回值。
 * @note    仅本测试编译的WRITE_REG替代，不代表总线时序。
 */
void PtpFine_TestWrite(volatile uint32_t *reg, uint32_t value)
{ ++writes; *reg = value; }

/**
 * @brief   记录请求顺序，并按卡住掩码模拟PTP命令完成。
 * @param   reg 模拟寄存器地址，不可为NULL。
 * @param   bits 本次置位的掩码。
 * @return  无返回值。
 * @note    TSSTI完成时复制目标到模拟当前时间；不模拟真实PTP频率或精度。
 */
void PtpFine_TestSet(volatile uint32_t *reg, uint32_t bits)
{
    ++writes;
    *reg |= bits;
    if (reg != &test_eth.PTPTSCR) { return; }
    const uint32_t command = bits & (ETH_PTPTSCR_TSSTI | ETH_PTPTSCR_TSSTU | ETH_PTPTSCR_TSARU);
    if (command == 0U) { return; }
    assert(commands < 8U);
    assert(command != ETH_PTPTSCR_TSSTU); /* No ambiguous signed HAL update. */
    assert(command == ETH_PTPTSCR_TSARU || command == ETH_PTPTSCR_TSSTI);
    requests[commands] = command;
    targets_sec[commands] = test_eth.PTPTSHUR;
    targets_ns[commands] = test_eth.PTPTSLUR;
    ++commands;
    if (command == ETH_PTPTSCR_TSARU && commands == 1U)
    {
        assert((test_eth.PTPTSCR & ETH_PTPTSCR_TSFCU) == 0U);
        assert(test_eth.PTPSSIR == 10U && test_eth.PTPTSAR == UINT32_C(0x80000000));
        assert((test_eth.MACIMR & ETH_MACIMR_TSTIM) != 0U);
    }
    if ((stuck_mask & command) != 0U) { return; }
    if (command == ETH_PTPTSCR_TSSTI)
    {
        assert((test_eth.PTPTSLUR & ETH_PTPTSLUR_TSUPNS) == 0U);
        assert(test_eth.PTPTSLUR < 1000000000U);
        test_eth.PTPTSHR = test_eth.PTPTSHUR;
        test_eth.PTPTSLR = test_eth.PTPTSLUR;
    }
    *reg &= ~command;
}

/** @brief 返回测试设置的HCLK。 @return 频率，单位Hz。 @note 不读取真实RCC。 */
uint32_t HAL_RCC_GetHCLKFreq(void) { return expected_hclk; }
/** @brief 模拟可冻结的HAL tick。 @return 毫秒低32位。 @note 每调用一次计数，支持自然回绕。 */
uint32_t HAL_GetTick(void)
{ ++tick_calls; if (!frozen_tick) { ++tick_ms; } return tick_ms; }
/**
 * @brief   供旧coarse路径使用的HAL配置替代。
 * @param   handle 模拟ETH句柄，不可为NULL。
 * @param   c 本次只读配置，不可为NULL。
 * @return  HAL_OK。
 * @note    fine路径必须完全不调用此替代，以检查其独立有界初始化。
 */
HAL_StatusTypeDef HAL_ETH_PTP_SetConfig(ETH_HandleTypeDef *handle, ETH_PTP_ConfigTypeDef *c)
{
    ++hal_config_calls;
    handle->IsPtpConfigured = HAL_ETH_PTP_CONFIGURED;
    test_eth.PTPTSCR = ETH_PTPTSCR_TSE | ETH_PTPTSCR_TSSSR | ETH_PTPTSCR_TSSARFE;
    test_eth.PTPSSIR = c->TimestampSubsecondInc;
    return HAL_OK;
}
/**
 * @brief   从模拟寄存器提供HAL读时，或注入读失败。
 * @param   handle 必须指向测试ETH句柄。
 * @param   t 调用方可写输出，不可为NULL；注入失败时保持原值。
 * @return  HAL_OK或HAL_ERROR。
 * @note    不推进时间，跨秒竞态由原有BSP测试覆盖。
 */
HAL_StatusTypeDef HAL_ETH_PTP_GetTime(ETH_HandleTypeDef *handle, ETH_TimeTypeDef *t)
{
    assert(handle == &heth);
    if (read_failure) { return HAL_ERROR; }
    t->Seconds = test_eth.PTPTSHR; t->NanoSeconds = test_eth.PTPTSLR;
    return HAL_OK;
}
/**
 * @brief   返回模拟当前时刻作为最近RX戳。
 * @param   handle 必须指向测试ETH句柄。
 * @param   t 调用方可写输出，不可为NULL。
 * @return  HAL_OK。
 * @note    仅验证fine配置也允许现有RX接口，不模拟真实DMA接收。
 */
HAL_StatusTypeDef HAL_ETH_PTP_GetRxTimestamp(ETH_HandleTypeDef *handle, ETH_TimeStampTypeDef *t)
{
    assert(handle == &heth);
    t->TimeStampHigh = test_eth.PTPTSHR; t->TimeStampLow = test_eth.PTPTSLR;
    return HAL_OK;
}

/** @brief 复位寄存器和代际以模拟独立上电场景。 @return 无返回值。 @note 仅测试内部调用。 */
static void Reset(void)
{
    memset(&test_eth, 0, sizeof test_eth);
    memset(&test_rcc, 0, sizeof test_rcc);
    memset(&heth, 0, sizeof heth);
    heth.Instance = ETH; heth.gState = HAL_ETH_STATE_READY;
    tick_ms = tick_calls = writes = hal_config_calls = commands = stuck_mask = 0U;
    expected_hclk = 200000000U; frozen_tick = read_failure = false;
    /* Private state is reset only to create independent power-on scenarios. */
    ready = 0U; clock_epoch = rate_revision = 0U;
    memset(requests, 0, sizeof requests);
    test_eth.DMABMR = ETH_DMABMR_EDE;
}
/** @brief 启动fine并断言名义初始化顺序。 @return 无返回值。 @note 每次先模拟一次独立上电。 */
static void Fine(void)
{
    Reset(); assert(BSP_PTP_InitFine() == BSP_PTP_OK);
    assert(hal_config_calls == 0U && commands == 2U);
    assert(requests[0] == ETH_PTPTSCR_TSARU && requests[1] == ETH_PTPTSCR_TSSTI);
    assert((test_eth.PTPTSCR & ETH_PTPTSCR_TSFCU) != 0U);
    assert(BSP_PTP_GetEpoch() != 0U && BSP_PTP_GetRateRevision() != 0U);
    assert(BSP_PTP_GetFrequencyPpb() == 0);
}
/** @brief 运行fine时钟、频率、相位和故障场景。 @return 全部通过为0。 @note 断言失败中止测试。 */
int main(void)
{
    Fine();
    BSP_PtpTime sample = {99U,99U};
    test_eth.PTPTSHR = 3U; test_eth.PTPTSLR = 750000000U;
    assert(BSP_PTP_Read(&sample) == BSP_PTP_OK && sample.seconds == 3U);
    heth.gState = HAL_ETH_STATE_STARTED; heth.RxDescList.pRxLastRxDesc = ETH_DMARXDESC_LS;
    assert(BSP_PTP_ReadRx(&sample) == BSP_PTP_OK && sample.nanoseconds == 750000000U);
    puts("PASS: fine startup orders nominal addend before initialization and accepts fine RX/local snapshots");

    uint32_t epoch = BSP_PTP_GetEpoch(), revision = BSP_PTP_GetRateRevision();
    assert(BSP_PTP_SetFrequencyPpb(100000) == BSP_PTP_OK);
    assert(test_eth.PTPTSAR == UINT32_C(2147698396));
    assert(BSP_PTP_GetFrequencyPpb() == 100000);
    assert(BSP_PTP_GetEpoch() == epoch && BSP_PTP_GetRateRevision() == revision+1U);
    assert(BSP_PTP_Read(&sample) == BSP_PTP_OK);
    assert(BSP_PTP_SetFrequencyPpb(-100000) == BSP_PTP_OK);
    assert(test_eth.PTPTSAR == UINT32_C(2147268900));
    assert(BSP_PTP_SetFrequencyPpb(1) == BSP_PTP_OK && test_eth.PTPTSAR == UINT32_C(2147483650));
    assert(BSP_PTP_SetFrequencyPpb(-1) == BSP_PTP_OK && test_eth.PTPTSAR == UINT32_C(2147483646));
    const uint32_t previous_writes = writes;
    const uint32_t previous_revision = BSP_PTP_GetRateRevision();
    assert(BSP_PTP_SetFrequencyPpb(-1) == BSP_PTP_OK);
    assert(BSP_PTP_GetRateRevision() == previous_revision);
    assert(BSP_PTP_SetFrequencyPpb(100001) != BSP_PTP_OK);
    assert(BSP_PTP_SetFrequencyPpb(INT32_MIN) != BSP_PTP_OK);
    assert(writes == previous_writes);
    puts("PASS: signed frequency uses bounded absolute nominal addend, advances rate revision only and rejects range");

    Fine(); test_eth.PTPTSHR = 3U; test_eth.PTPTSLR = 900000000U;
    epoch = BSP_PTP_GetEpoch(); revision = BSP_PTP_GetRateRevision();
    assert(BSP_PTP_Step(INT64_C(200000000)) == BSP_PTP_OK);
    assert(test_eth.PTPTSHR == 4U && test_eth.PTPTSLR == 100000000U);
    assert(BSP_PTP_GetEpoch() == epoch+1U && BSP_PTP_GetRateRevision() == revision+1U);
    assert(BSP_PTP_Step(-INT64_C(200000000)) == BSP_PTP_OK);
    assert(test_eth.PTPTSHR == 3U && test_eth.PTPTSLR == 900000000U);
    const uint32_t step_writes = writes;
    assert(BSP_PTP_Step(INT64_MIN) != BSP_PTP_OK);
    assert(BSP_PTP_Step(INT64_MAX) != BSP_PTP_OK);
    assert(BSP_PTP_Step(-INT64_C(4000000000)) != BSP_PTP_OK);
    test_eth.PTPTSHR = UINT32_MAX; test_eth.PTPTSLR = 999999999U;
    assert(BSP_PTP_Step(1) != BSP_PTP_OK && writes == step_writes);
    puts("PASS: phase correction takes fresh time, overwrites absolute TSSTI across seconds and checks signed overflow");

    Fine(); const uint32_t busy_writes = writes;
    const uint32_t pending[] = {ETH_PTPTSCR_TSSTU, ETH_PTPTSCR_TSSTI, ETH_PTPTSCR_TSARU};
    for (unsigned i = 0U; i < 3U; ++i)
    {
        test_eth.PTPTSCR |= pending[i];
        assert(BSP_PTP_SetFrequencyPpb(1) == BSP_PTP_BUSY && BSP_PTP_Step(1) == BSP_PTP_BUSY);
        test_eth.PTPTSCR &= ~pending[i];
    }
    assert(writes == busy_writes);
    test_eth.PTPTSCR &= ~ETH_PTPTSCR_TSSTU;
    read_failure = true;
    assert(BSP_PTP_Step(1) == BSP_PTP_HAL_ERROR && writes == busy_writes);
    read_failure = false; test_eth.PTPTSAR ^= 1U;
    sample = (BSP_PtpTime){99U,99U};
    assert(BSP_PTP_Read(&sample) == BSP_PTP_BAD_CONFIG && sample.seconds == 99U);
    puts("PASS: busy commands, failed fresh read and changed addend cannot silently alter the clock or output");

    Reset(); assert(BSP_PTP_Init() == BSP_PTP_OK);
    const uint32_t coarse_writes = writes;
    assert(BSP_PTP_SetFrequencyPpb(1) != BSP_PTP_OK && BSP_PTP_Step(1) != BSP_PTP_OK);
    assert(writes == coarse_writes);
    puts("PASS: coarse A remains readable but cannot accept fine frequency or phase commands");

    Reset(); stuck_mask = ETH_PTPTSCR_TSARU;
    assert(BSP_PTP_InitFine() != BSP_PTP_OK && tick_calls <= 25U);
    assert(!ready && heth.IsPtpConfigured == HAL_ETH_PTP_NOT_CONFIGURED);
    Reset(); stuck_mask = ETH_PTPTSCR_TSARU; frozen_tick = true;
    assert(BSP_PTP_InitFine() != BSP_PTP_OK && tick_calls <= 100002U);
    Reset(); stuck_mask = ETH_PTPTSCR_TSSTI; tick_ms = UINT32_MAX-5U;
    assert(BSP_PTP_InitFine() == BSP_PTP_INIT_TIMEOUT && tick_calls <= 26U);
    Fine(); epoch = BSP_PTP_GetEpoch(); stuck_mask = ETH_PTPTSCR_TSSTI;
    assert(BSP_PTP_Step(1) != BSP_PTP_OK);
    assert(!ready && BSP_PTP_GetEpoch() > epoch && (test_eth.PTPTSCR & ETH_PTPTSCR_TSE) == 0U);
    assert(BSP_PTP_Read(&sample) == BSP_PTP_NOT_READY);
    Fine(); epoch = BSP_PTP_GetEpoch(); stuck_mask = ETH_PTPTSCR_TSARU;
    assert(BSP_PTP_SetFrequencyPpb(1) != BSP_PTP_OK && !ready && BSP_PTP_GetEpoch() > epoch);
    Fine(); frozen_tick = true; stuck_mask = ETH_PTPTSCR_TSSTI;
    const uint32_t ticks_before = tick_calls;
    assert(BSP_PTP_Step(1) == BSP_PTP_UPDATE_TIMEOUT && !ready);
    assert(tick_calls - ticks_before <= 100002U);
    puts("PASS: launched updates fail closed with epoch invalidation and finite waits even with frozen HAL tick");

    Fine(); clock_epoch = UINT32_MAX;
    assert(BSP_PTP_Step(1) != BSP_PTP_OK && !ready && BSP_PTP_GetEpoch() == UINT32_MAX);
    Fine(); rate_revision = UINT32_MAX;
    assert(BSP_PTP_SetFrequencyPpb(1) != BSP_PTP_OK && !ready && BSP_PTP_GetRateRevision() == UINT32_MAX);
    Reset(); expected_hclk = 216000000U;
    assert(BSP_PTP_InitFine() == BSP_PTP_BAD_CLOCK && writes == 0U);
    Reset(); test_eth.PTPTSCR = ETH_PTPTSCR_TSSTI;
    assert(BSP_PTP_InitFine() != BSP_PTP_OK && writes == 0U);
    puts("PASS: generation exhaustion fails closed without wrap reuse; wrong HCLK and pending init are zero-write rejections");
    return 0;
}
