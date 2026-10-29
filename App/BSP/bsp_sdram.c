/**
 * @file        bsp_sdram.c
 * @brief       校验生成配置，发送外部SDRAM上电命令并管理显示/诊断独占模式。
 * @details     固定当前200MHz/3、CAS3、8MB板级配置；显示只上电，诊断推进破坏性自检。
 * @note        启动/前台串行调用；不新增IRQ/DMA，保持缓存关闭和Region1非缓存XN。
 *              本版本HAL发命令未等待BUSY，本模块在前后以2ms/20000次双限界等待。
 */
#include "bsp_sdram.h"
#include "main.h"
#include <stddef.h>

#define SDRAM_HCLK_HZ UINT32_C(200000000)
#define SDRAM_CLOCK_DIVISOR 3U
#define SDRAM_REFRESH_ROWS 4096U
#define SDRAM_REFRESH_WINDOW_MS 64U
#define SDRAM_REFRESH_MARGIN_CYCLES 20U
#define SDRAM_MODE_CAS3_SINGLE_WRITE UINT32_C(0x0230)
#define SDRAM_WAIT_MS 2U
#define SDRAM_WAIT_POLLS 20000U
extern SDRAM_HandleTypeDef hsdram1;
static BSP_SdramSnapshot state;
static SDRAM_Test test;
static uint32_t start_ms;

/**
 * @brief 读取Region1/2属性并恢复原区域选择器，不改区域配置。
 * @return SDRAM普通内存及FMC共享Device窗口均匹配为true。
 * @note 仅启动前台调用，无中断客户修改MPU；只写RNR用于读回，不写RBAR/RASR。
 *       任一区域不符也必须先恢复选择器，且调用方此时尚未读取FMC寄存器。
 */
static bool MpuMatches(void)
{
    if ((MPU->CTRL & MPU_CTRL_ENABLE_Msk) == 0U) { return false; }
    const uint32_t saved_region = MPU->RNR;
    MPU->RNR = 1U;
    __DSB();
    const uint32_t data_base = MPU->RBAR & MPU_RBAR_ADDR_Msk;
    const uint32_t data_attributes = MPU->RASR;
    MPU->RNR = 2U;
    __DSB();
    const uint32_t register_base = MPU->RBAR & MPU_RBAR_ADDR_Msk;
    const uint32_t register_attributes = MPU->RASR;
    MPU->RNR = saved_region;
    __DSB();
    const uint32_t common = MPU_RASR_XN_Msk | (3U << MPU_RASR_AP_Pos) | MPU_RASR_ENABLE_Msk;
    const uint32_t normal = common | (1U << MPU_RASR_TEX_Pos) | (22U << MPU_RASR_SIZE_Pos);
    const uint32_t device = common | MPU_RASR_S_Msk | MPU_RASR_B_Msk | (11U << MPU_RASR_SIZE_Pos);
    return data_base == BSP_SDRAM_BASE && data_attributes == normal &&
        register_base == UINT32_C(0xA0000000) && register_attributes == device;
}

/**
 * @brief 先核对MPU可达性，再只读核对生成的FMC控制器、缓存及SDCKE0引脚。
 * @return 配置完全符合当前批准基线为true。
 * @note 不能仅检查0xC0000000数据区域；FMC控制器本身位于0xA0000140。
 */
static bool ConfigMatches(void)
{
    if (!MpuMatches() || (SCB->CCR & (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk)) != 0U) { return false; }
    const uint32_t control = FMC_SDRAM_COLUMN_BITS_NUM_8 | FMC_SDRAM_ROW_BITS_NUM_12 |
        FMC_SDRAM_MEM_BUS_WIDTH_16 | FMC_SDRAM_INTERN_BANKS_NUM_4 | FMC_SDRAM_CAS_LATENCY_3 |
        FMC_SDRAM_CLOCK_PERIOD_3 | FMC_SDRAM_RBURST_ENABLE;
    const uint32_t timing = (1U << FMC_SDTR1_TMRD_Pos) | (7U << FMC_SDTR1_TXSR_Pos) |
        (4U << FMC_SDTR1_TRAS_Pos) | (6U << FMC_SDTR1_TRC_Pos) |
        (2U << FMC_SDTR1_TWR_Pos) | (1U << FMC_SDTR1_TRP_Pos) | (1U << FMC_SDTR1_TRCD_Pos);
    return hsdram1.Instance == FMC_SDRAM_DEVICE && HAL_RCC_GetHCLKFreq() == SDRAM_HCLK_HZ &&
        hsdram1.Init.SDBank == FMC_SDRAM_BANK1 &&
        hsdram1.Init.ColumnBitsNumber == FMC_SDRAM_COLUMN_BITS_NUM_8 &&
        hsdram1.Init.RowBitsNumber == FMC_SDRAM_ROW_BITS_NUM_12 &&
        hsdram1.Init.MemoryDataWidth == FMC_SDRAM_MEM_BUS_WIDTH_16 &&
        hsdram1.Init.InternalBankNumber == FMC_SDRAM_INTERN_BANKS_NUM_4 &&
        hsdram1.Init.CASLatency == FMC_SDRAM_CAS_LATENCY_3 &&
        hsdram1.Init.WriteProtection == FMC_SDRAM_WRITE_PROTECTION_DISABLE &&
        hsdram1.Init.SDClockPeriod == FMC_SDRAM_CLOCK_PERIOD_3 &&
        hsdram1.Init.ReadBurst == FMC_SDRAM_RBURST_ENABLE &&
        hsdram1.Init.ReadPipeDelay == FMC_SDRAM_RPIPE_DELAY_0 &&
        (RCC->AHB3ENR & RCC_AHB3ENR_FMCEN) != 0U &&
        FMC_SDRAM_DEVICE->SDCR[0] == control && FMC_SDRAM_DEVICE->SDTR[0] == timing &&
        (GPIOC->MODER & (3U << 6U)) == (2U << 6U) &&
        (GPIOC->AFR[0] & (15U << 12U)) == (12U << 12U) &&
        (GPIOC->OTYPER & (1U << 3U)) == 0U && (GPIOC->PUPDR & (3U << 6U)) == 0U;
}

/** @brief 限时等待FMC命令结束。@return BUSY清零为true；失败保存TIMEOUT。@note 即使HAL tick不推进也最多轮询20000次。 */
static bool WaitReady(void)
{
    const uint32_t started = HAL_GetTick();
    for (uint32_t poll = 0U; poll < SDRAM_WAIT_POLLS; ++poll) {
        if ((FMC_SDRAM_DEVICE->SDSR & FMC_SDSR_BUSY) == 0U) { return true; }
        if ((uint32_t)(HAL_GetTick() - started) >= SDRAM_WAIT_MS) { break; }
    }
    state.status = BSP_SDRAM_TIMEOUT;
    return false;
}

/**
 * @brief 发送一个HAL命令并在其前后核对BUSY。
 * @param mode FMC命令模式。@param count 自动刷新次数，1或8。@param mode_word 模式字，非LOAD时为0。
 * @return 命令提交和BUSY等待均完成为true；错误后调用方停止序列。
 */
static bool Command(uint32_t mode, uint32_t count, uint32_t mode_word)
{
    ++state.command_index;
    if (!WaitReady()) { return false; }
    FMC_SDRAM_CommandTypeDef command = {0};
    command.CommandMode = mode; command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command.AutoRefreshNumber = count; command.ModeRegisterDefinition = mode_word;
    if (HAL_SDRAM_SendCommand(&hsdram1, &command, SDRAM_WAIT_MS) != HAL_OK) {
        state.status = BSP_SDRAM_COMMAND_ERROR;
        return false;
    }
    __DSB();
    return WaitReady();
}

/**
 * @brief 依赖生成配置执行一次芯片上电和刷新编程，不触碰外部内存。
 * @return 配置及上电成功为true，失败在state中保留错误。
 * @note 仅两个模式首次取得所有权时调用，允许既有1ms上电等待；不建立自检。
 */
static bool InitChip(void)
{
    state.status = BSP_SDRAM_CONFIG_ERROR;
    start_ms = HAL_GetTick();
    if (!ConfigMatches()) { return false; }
    state.sdclk_hz = HAL_RCC_GetHCLKFreq() / SDRAM_CLOCK_DIVISOR;
    /* 先以64位计算，避免把整数SDCLK舍入误差放大；当前结果为1021。 */
    state.refresh_count = (uint32_t)(((uint64_t)HAL_RCC_GetHCLKFreq() * SDRAM_REFRESH_WINDOW_MS) /
        (SDRAM_CLOCK_DIVISOR * SDRAM_REFRESH_ROWS * 1000U)) - SDRAM_REFRESH_MARGIN_CYCLES;
    if (!Command(FMC_SDRAM_CMD_CLK_ENABLE, 1U, 0U)) { return false; }
    HAL_Delay(1U);
    if (!Command(FMC_SDRAM_CMD_PALL, 1U, 0U) ||
        !Command(FMC_SDRAM_CMD_AUTOREFRESH_MODE, 8U, 0U) ||
        !Command(FMC_SDRAM_CMD_LOAD_MODE, 1U, SDRAM_MODE_CAS3_SINGLE_WRITE)) { return false; }
    state.command_index = 5U;
    if (HAL_SDRAM_ProgramRefreshRate(&hsdram1, state.refresh_count) != HAL_OK ||
        ((FMC_SDRAM_DEVICE->SDRTR & FMC_SDRTR_COUNT) >> FMC_SDRTR_COUNT_Pos) != state.refresh_count) {
        state.status = BSP_SDRAM_REFRESH_ERROR;
        return false;
    }
    __DSB();
    return true;
}

/** @brief 依赖生成配置初始化芯片并建立内存测试状态。@return 上电成功为true，不代表内存验收通过。@note 重复调用不重启测试；显示模式不可进入诊断，仅启动前台调用。 */
bool BSP_SDRAM_Init(void)
{
    if (state.status != BSP_SDRAM_OFF) {
        return state.status == BSP_SDRAM_TESTING || state.status == BSP_SDRAM_PASS;
    }
    if (!InitChip()) { return false; }
    if (!SDRAM_TestInit(&test, (volatile void *)(uintptr_t)BSP_SDRAM_BASE, BSP_SDRAM_BYTES)) {
        state.status = BSP_SDRAM_MEMORY_ERROR;
        return false;
    }
    state.status = BSP_SDRAM_TESTING;
    return true;
}

/**
 * @brief 执行芯片上电后把外部内存交给显示，不创建破坏性自检。
 * @return 显示持有READY为true；诊断占用或上电失败为false。
 * @note 仅启动前台调用，重复调用不重发命令/不访问内存，失败后需复位。
 */
bool BSP_SDRAM_InitDisplay(void)
{
    if (state.status != BSP_SDRAM_OFF) { return state.status == BSP_SDRAM_READY; }
    if (!InitChip()) { return false; }
    state.status = BSP_SDRAM_READY;
    return true;
}

/** @brief 只读检查显示是否独占已上电内存。@return 仅显示READY为true。@note 不查询/访问外部内存，不把诊断PASS转成显示模式。 */
bool BSP_SDRAM_IsDisplayReady(void)
{ return state.status == BSP_SDRAM_READY; }

/** @brief 前台推进一个自检分片。@param now_ms HAL毫秒。@param allow_work false完全跳过目标内存访问。@return 无返回值。 */
void BSP_SDRAM_Process(uint32_t now_ms, bool allow_work)
{
    if (!allow_work || state.status != BSP_SDRAM_TESTING) { return; }
    SDRAM_TestStep(&test, now_ms);
    state.elapsed_ms = now_ms - start_ms;
    state.phase = test.phase; state.pass = test.pass;
    state.verified_bytes = test.verified_words * 4U;
    state.error_offset_bytes = test.error_offset_bytes;
    state.expected = test.expected; state.actual = test.actual;
    if (test.phase == SDRAM_TEST_PASS) { state.status = BSP_SDRAM_PASS; }
    else if (test.phase == SDRAM_TEST_FAIL) { state.status = BSP_SDRAM_MEMORY_ERROR; }
}

/** @brief 输出本模块状态副本。@param snapshot 输出指针，NULL无操作。@return 无返回值；不读取目标内存。 */
void BSP_SDRAM_GetSnapshot(BSP_SdramSnapshot *snapshot)
{ if (snapshot != NULL) { *snapshot = state; } }
