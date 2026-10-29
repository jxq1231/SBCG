/**
 * @file        bsp_lcd.c
 * @brief       在显示内存READY后管理RGB565双缓冲及生成LTDC的延后启动。
 * @details     首帧A完整提交才打开面板；后续影子地址经VBR换页并在前台确认所有权。
 * @note        仅前台串行调用，不绘图、不启用IRQ/DMA、不修改CubeMX时钟/引脚/时序。
 *              SDRAM为现有MPU非缓存区域且I/D Cache关闭，交接前仅用DSB完成CPU写入。
 */
#include "bsp_lcd.h"
#include "bsp_sdram.h"
#include "main.h"
#include <stddef.h>

#define LCD_PIXEL_CLOCK_HZ UINT32_C(9600000)
#define LCD_INTERRUPTS (LTDC_IT_LI | LTDC_IT_FU | LTDC_IT_TE | LTDC_IT_RR)
extern LTDC_HandleTypeDef hltdc;
static BSP_LcdSnapshot state;
static uint32_t pending_since_ms;

/**
 * @brief 对诊断计数饱和递增。
 * @param count 本模块持有的非NULL计数地址。
 * @return 无返回值。
 * @note 仅前台调用，防止长期运行计数回绕成零。
 */
static void Increment(uint32_t *count)
{ if (*count != UINT32_MAX) { ++*count; } }

/**
 * @brief 转入不可恢复的错误状态并关闭背光和显示使能。
 * @return false，便于发布/初始化失败路径返回。
 * @note 不释放扫描/待换入缓冲，不重置LTDC或SDRAM；只有复位可以重试。
 */
static bool Fail(void)
{
    state.status = BSP_LCD_ERROR;
    state.render_address = 0U;
    HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DISP_GPIO_Port, LCD_DISP_Pin, GPIO_PIN_RESET);
    return false;
}

/**
 * @brief 检查当前芯片内存所有权与缓存状态，不访问外部内存。
 * @return 显示READY且I/D Cache均关闭时为true。
 * @note SDRAM初始化已确认Region1/2；这里不重写MPU或增加缓存维护。
 */
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
 * @note 仅首帧生成初始化后调用，不手工补写不匹配的配置。
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
        layer->WindowX0 == 0U && layer->WindowX1 == BSP_LCD_WIDTH &&
        layer->WindowY0 == 0U && layer->WindowY1 == BSP_LCD_HEIGHT &&
        layer->PixelFormat == LTDC_PIXEL_FORMAT_RGB565 && layer->Alpha == 255U && layer->Alpha0 == 0U &&
        layer->BlendingFactor1 == LTDC_BLENDING_FACTOR1_CA &&
        layer->BlendingFactor2 == LTDC_BLENDING_FACTOR2_CA &&
        layer->ImageWidth == BSP_LCD_WIDTH && layer->ImageHeight == BSP_LCD_HEIGHT &&
        layer->FBStartAdress == BSP_LCD_FRAME_A &&
        layer->Backcolor.Red == 0U && layer->Backcolor.Green == 0U && layer->Backcolor.Blue == 0U &&
        (LTDC->GCR & LTDC_GCR_LTDCEN) != 0U && LTDC->IER == 0U &&
        (LTDC_Layer1->CR & LTDC_LxCR_LEN) != 0U && (LTDC_Layer2->CR & LTDC_LxCR_LEN) == 0U &&
        LTDC_Layer1->PFCR == LTDC_PIXEL_FORMAT_RGB565 && LTDC_Layer1->CFBAR == BSP_LCD_FRAME_A &&
        LTDC_Layer1->CFBLR == ((BSP_LCD_WIDTH * 2U << 16U) | (BSP_LCD_WIDTH * 2U + 3U)) &&
        LTDC_Layer1->CFBLNR == BSP_LCD_HEIGHT && (LTDC->SRCR & LTDC_SRCR_VBR) == 0U;
}

/**
 * @brief 检查并计数LTDC取数错误和HAL状态异常。
 * @return 无异常为true；异常时关闭输出并返回false。
 * @note 无IRQ清标志，不抹去致命错误证据；错误终态不再重复计数。
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

/** @brief 建立显示软件状态但不启动LTDC。@return 显示可用为true。@note 未上电时允许之后重试；既有错误不重置。 */
bool BSP_LCD_Init(void)
{
    if (state.status != BSP_LCD_OFF) { return state.status != BSP_LCD_ERROR; }
    if (!BSP_SDRAM_IsDisplayReady()) { return false; }
    if (!MemoryReady()) { Increment(&state.config_errors); return Fail(); }
    state.status = BSP_LCD_READY;
    return true;
}

/** @brief 租出尚未扫描的唯一帧缓冲。@return 借用像素指针，忙/错误为NULL。@note 不解引用外部内存，调用方负责整帧绘制。 */
uint16_t *BSP_LCD_BeginFrame(void)
{
    if ((state.status != BSP_LCD_READY && state.status != BSP_LCD_RUNNING) ||
        state.render_address != 0U || state.pending_address != 0U) { return NULL; }
    if (!MemoryReady()) { Increment(&state.config_errors); (void)Fail(); return NULL; }
    state.render_address = state.front_address == BSP_LCD_FRAME_A ? BSP_LCD_FRAME_B : BSP_LCD_FRAME_A;
    return (uint16_t *)(uintptr_t)state.render_address;
}

/**
 * @brief 发布完整帧，首帧调用生成初始化，后续请求垂直消隐换页。
 * @param now_ms 当前HAL毫秒，可回绕，用于100ms换页时限。
 * @return 发布成功或换页已排队为true，其它为false。
 * @note DSB将CPU绘制写入交给LTDC主设备；不调用会启用RR IRQ的HAL_LTDC_Reload。
 */
bool BSP_LCD_Present(uint32_t now_ms)
{
    if ((state.status != BSP_LCD_READY && state.status != BSP_LCD_RUNNING) ||
        state.render_address == 0U || state.pending_address != 0U) { return false; }
    if (!MemoryReady()) { Increment(&state.config_errors); return Fail(); }
    if (state.status == BSP_LCD_READY) {
        __DSB();
        MX_LTDC_Init();
        /* HAL初始化主动打开FU/TE中断源；本BSP只轮询，不改变生成NVIC配置。 */
        if (hltdc.Instance != LTDC) { Increment(&state.config_errors); return Fail(); }
        __HAL_LTDC_DISABLE_IT(&hltdc, LCD_INTERRUPTS);
        if (!ConfigMatches()) { Increment(&state.config_errors); return Fail(); }
        if (!CheckErrors()) { return false; }
        state.front_address = state.render_address;
        state.render_address = 0U;
        state.status = BSP_LCD_RUNNING;
        Increment(&state.presented_frames);
        HAL_GPIO_WritePin(LCD_DISP_GPIO_Port, LCD_DISP_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
        return true;
    }
    if (!CheckErrors()) { return false; }
    if ((LTDC->SRCR & LTDC_SRCR_VBR) != 0U) { Increment(&state.config_errors); return Fail(); }
    if (HAL_LTDC_SetAddress_NoReload(&hltdc, state.render_address, 0U) != HAL_OK) {
        Increment(&state.hal_errors); return Fail();
    }
    __DSB();
    state.pending_address = state.render_address;
    state.render_address = 0U;
    pending_since_ms = now_ms;
    LTDC->SRCR = LTDC_SRCR_VBR;
    return true;
}

/** @brief 前台轮询错误和VBR完成后归还旧前台。@param now_ms HAL毫秒，可回绕。@return 无返回值。@note 完成证据优先于超时，同轮发现错误则保留所有权不归还。 */
void BSP_LCD_Process(uint32_t now_ms)
{
    if (state.status != BSP_LCD_RUNNING) { return; }
    if (!MemoryReady()) { Increment(&state.config_errors); (void)Fail(); return; }
    if (!CheckErrors()) { return; }
    if (state.pending_address == 0U) { return; }
    if ((LTDC->SRCR & LTDC_SRCR_VBR) == 0U) {
        state.front_address = state.pending_address;
        state.pending_address = 0U;
        Increment(&state.presented_frames);
    } else if ((uint32_t)(now_ms - pending_since_ms) >= BSP_LCD_RELOAD_TIMEOUT_MS) {
        Increment(&state.reload_timeouts); (void)Fail();
    }
}

/** @brief 撤销未发布绘制租约。@return 无返回值。@note 不释放扫描或待换页缓冲，不访问外部内存。 */
void BSP_LCD_CancelFrame(void)
{ state.render_address = 0U; }

/** @brief 复制前台诊断副本。@param snapshot 输出指针，NULL无操作。@return 无返回值。@note 不读帧缓冲/不清硬件状态。 */
void BSP_LCD_GetSnapshot(BSP_LcdSnapshot *snapshot)
{ if (snapshot != NULL) { *snapshot = state; } }
