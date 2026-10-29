/**
 * @file        bsp_touchgfx_display.c
 * @brief       管理TouchGFX使用的LCD安全启动、前台错误检查和延后背光。
 * @details     显示SDRAM READY后在背光关闭时清零A/B，再启动生成LTDC；换页/IRQ属于TouchGFX。
 * @note        必须在音频DMA启动之前初始化；不设置RCC/MPU/NVIC/DMA2D，不改变生成硬件配置。
 *              I/D Cache保持关闭，CPU绘制/清屏通过DSB交给LTDC读取，不增加缓存维护。
 */
#include "bsp_touchgfx_display.h"
#include "bsp_sdram.h"
#include "main.h"
#include <stddef.h>
#include <string.h>

#define LCD_PIXEL_CLOCK_HZ UINT32_C(9600000)
extern LTDC_HandleTypeDef hltdc;
static BSP_TouchGFXDisplaySnapshot state;

/** @brief 饱和累加错误计数。@param count 非NULL模块计数指针。@return 无返回值，前台调用。 */
static void Increment(uint32_t *count)
{ if (*count != UINT32_MAX) { ++*count; } }

/** @brief 锁存错误并关闭显示/背光。@return 固定false；只允许复位恢复。@note 仅前台，不关框架IRQ或改换页地址。 */
static bool Fail(void)
{
    state.status = BSP_TOUCHGFX_ERROR;
    HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DISP_GPIO_Port, LCD_DISP_Pin, GPIO_PIN_RESET);
    return false;
}

/** @brief 检查显示SDRAM所有权和I/D Cache关闭条件。@return 依赖就绪为true，不访问外存或改MPU。 */
static bool MemoryReady(void)
{
    return BSP_SDRAM_IsDisplayReady() &&
        (SCB->CCR & (SCB_CCR_IC_Msk | SCB_CCR_DC_Msk)) == 0U;
}

/**
 * @brief 从RCC现值只读核对HSE经PLLSAI-R分支得到的LCD像素时钟。
 * @return HSE/主PLL/PLLSAI已就绪且精确得到9.6MHz时为true。
 * @note 本工程HAL_RCCEx_GetPeriphCLKFreq不支持LTDC，不能用其返回的0Hz拒绝配置。
 *       64位先乘后除避免中间截断，除前验证M/N/R范围；不写RCC或改变时钟来源。
 */
static bool PixelClockMatches(void)
{
    const uint32_t clock_ready = RCC_CR_HSERDY | RCC_CR_PLLRDY | RCC_CR_PLLSAIRDY;
    const uint32_t cr = RCC->CR;
    const uint32_t pll = RCC->PLLCFGR;
    const uint32_t pll_sai = RCC->PLLSAICFGR;
    const uint32_t divider = RCC->DCKCFGR1;
    if ((cr & clock_ready) != clock_ready ||
        (pll & RCC_PLLCFGR_PLLSRC) != RCC_PLLCFGR_PLLSRC_HSE) { return false; }
    const uint32_t pll_m = pll & RCC_PLLCFGR_PLLM;
    const uint32_t pll_sai_n = (pll_sai & RCC_PLLSAICFGR_PLLSAIN) >> RCC_PLLSAICFGR_PLLSAIN_Pos;
    const uint32_t pll_sai_r = (pll_sai & RCC_PLLSAICFGR_PLLSAIR) >> RCC_PLLSAICFGR_PLLSAIR_Pos;
    const uint32_t div_r_encoding = (divider & RCC_DCKCFGR1_PLLSAIDIVR) >> RCC_DCKCFGR1_PLLSAIDIVR_Pos;
    if (pll_m < 2U || pll_sai_n < 50U || pll_sai_n > 432U || pll_sai_r < 2U) { return false; }
    /* DIVR编码0/1/2/3分别表示2/4/8/16，像素时钟=HSE*N/(M*R*DIVR)。 */
    const uint64_t numerator_hz = (uint64_t)HSE_VALUE * pll_sai_n;
    const uint64_t denominator = (uint64_t)pll_m * pll_sai_r * (2U << div_r_encoding);
    return numerator_hz % denominator == 0U && numerator_hz / denominator == LCD_PIXEL_CLOCK_HZ;
}

/**
 * @brief 只读核对生成的LTDC句柄、像素时钟和关键扫描寄存器。
 * @return 与480×272 RGB565单图层批准配置一致时为true。
 * @note 仅延后生成初始化完成后调用，不手工补写不匹配的配置。
 */
static bool ConfigMatches(void)
{
    const LTDC_InitTypeDef *init = &hltdc.Init;
    const LTDC_LayerCfgTypeDef *layer = &hltdc.LayerCfg[0];
    return hltdc.Instance == LTDC && hltdc.State == HAL_LTDC_STATE_READY &&
        hltdc.ErrorCode == HAL_LTDC_ERROR_NONE &&
        PixelClockMatches() &&
        init->HSPolarity == LTDC_HSPOLARITY_AL && init->VSPolarity == LTDC_VSPOLARITY_AL &&
        init->DEPolarity == LTDC_DEPOLARITY_AL && init->PCPolarity == LTDC_PCPOLARITY_IPC &&
        init->HorizontalSync == 40U && init->VerticalSync == 9U &&
        init->AccumulatedHBP == 53U && init->AccumulatedVBP == 11U &&
        init->AccumulatedActiveW == 533U && init->AccumulatedActiveH == 283U &&
        init->TotalWidth == 565U && init->TotalHeigh == 285U &&
        init->Backcolor.Red == 0U && init->Backcolor.Green == 0U && init->Backcolor.Blue == 0U &&
        layer->WindowX0 == 0U && layer->WindowX1 == BSP_TOUCHGFX_WIDTH &&
        layer->WindowY0 == 0U && layer->WindowY1 == BSP_TOUCHGFX_HEIGHT &&
        layer->PixelFormat == LTDC_PIXEL_FORMAT_RGB565 && layer->Alpha == 255U && layer->Alpha0 == 0U &&
        layer->BlendingFactor1 == LTDC_BLENDING_FACTOR1_CA &&
        layer->BlendingFactor2 == LTDC_BLENDING_FACTOR2_CA &&
        layer->ImageWidth == BSP_TOUCHGFX_WIDTH && layer->ImageHeight == BSP_TOUCHGFX_HEIGHT &&
        layer->FBStartAdress == BSP_TOUCHGFX_FRAME_A &&
        layer->Backcolor.Red == 0U && layer->Backcolor.Green == 0U && layer->Backcolor.Blue == 0U &&
        (LTDC->GCR & LTDC_GCR_LTDCEN) != 0U &&
        (LTDC_Layer1->CR & LTDC_LxCR_LEN) != 0U && (LTDC_Layer2->CR & LTDC_LxCR_LEN) == 0U &&
        LTDC_Layer1->PFCR == LTDC_PIXEL_FORMAT_RGB565 && LTDC_Layer1->CFBAR == BSP_TOUCHGFX_FRAME_A &&
        LTDC_Layer1->CFBLR == ((BSP_TOUCHGFX_WIDTH * 2U << 16U) | (BSP_TOUCHGFX_WIDTH * 2U + 3U)) &&
        LTDC_Layer1->CFBLNR == BSP_TOUCHGFX_HEIGHT && (LTDC->SRCR & LTDC_SRCR_VBR) == 0U;
}

/**
 * @brief 合并原始FU/TE与HAL锁存错误，避免IRQ清标志后漏报。
 * @return 无错误为true；否则饱和累计并关闭显示。
 * @note 不清ISR/ICR，不禁用LIE/FU/TE，不抢占TouchGFX的中断与换页所有权。
 */
static bool CheckErrors(void)
{
    const uint32_t flags = LTDC->ISR;
    bool failed = false;
    if ((flags & LTDC_ISR_FUIF) != 0U) { Increment(&state.underrun_errors); failed = true; }
    if ((flags & LTDC_ISR_TERRIF) != 0U) { Increment(&state.transfer_errors); failed = true; }
    if (hltdc.State != HAL_LTDC_STATE_READY || hltdc.ErrorCode != HAL_LTDC_ERROR_NONE) {
        Increment(&state.hal_errors); failed = true;
    }
    return failed ? Fail() : true;
}

/** @brief 按头文件约定在音频启动前清屏并启动生成LTDC。@return READY/RUNNING为true；未就绪或错误为false。 */
bool BSP_TouchGFXDisplay_Init(void)
{
    if (state.status != BSP_TOUCHGFX_OFF) { return state.status != BSP_TOUCHGFX_ERROR; }
    if (!BSP_SDRAM_IsDisplayReady()) { return false; }
    if (!MemoryReady()) { Increment(&state.config_errors); return Fail(); }
    HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DISP_GPIO_Port, LCD_DISP_Pin, GPIO_PIN_RESET);
    /* 清屏只覆盖两个261120字节缓冲，保留中间1024字节间隔与其余外部内存。 */
    memset((void *)(uintptr_t)BSP_TOUCHGFX_FRAME_A, 0, BSP_TOUCHGFX_FRAME_BYTES);
    memset((void *)(uintptr_t)BSP_TOUCHGFX_FRAME_B, 0, BSP_TOUCHGFX_FRAME_BYTES);
    __DSB();
    MX_LTDC_Init();
    if (!ConfigMatches()) { Increment(&state.config_errors); return Fail(); }
    if (!CheckErrors()) { return false; }
    state.status = BSP_TOUCHGFX_READY;
    return true;
}

/** @brief 前台轮询内存依赖与显示错误。@return 无返回值；错误终态不继续检查或重复计数。 */
void BSP_TouchGFXDisplay_Process(void)
{
    if (state.status != BSP_TOUCHGFX_READY && state.status != BSP_TOUCHGFX_RUNNING) { return; }
    if (!MemoryReady()) { Increment(&state.config_errors); (void)Fail(); return; }
    (void)CheckErrors();
}

/** @brief 接受框架可见地址，在前台开启面板。@param address 字节地址，仅A/B有效。@return 接受为true，错误为false并关显示。 */
bool BSP_TouchGFXDisplay_Reveal(uint32_t address)
{
    if (state.status != BSP_TOUCHGFX_READY && state.status != BSP_TOUCHGFX_RUNNING) { return false; }
    if (address != BSP_TOUCHGFX_FRAME_A && address != BSP_TOUCHGFX_FRAME_B) {
        Increment(&state.config_errors); return Fail();
    }
    BSP_TouchGFXDisplay_Process();
    if (state.status == BSP_TOUCHGFX_ERROR) { return false; }
    state.front_address = address;
    if (state.status == BSP_TOUCHGFX_READY) {
        __DSB();
        HAL_GPIO_WritePin(LCD_DISP_GPIO_Port, LCD_DISP_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
        state.status = BSP_TOUCHGFX_RUNNING;
    }
    return true;
}

/** @brief 复制前台显示状态。@param out 可空调用方副本，不保留地址。@return 无返回值，不访问硬件。 */
void BSP_TouchGFXDisplay_GetSnapshot(BSP_TouchGFXDisplaySnapshot *out)
{ if (out != NULL) { *out = state; } }
