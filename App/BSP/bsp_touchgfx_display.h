/**
 * @file        bsp_touchgfx_display.h
 * @brief       TouchGFX显示启动、前台错误检查和延后背光接口。
 * @details     以固定RGB565双缓冲启动生成LTDC，保留框架对LTDC中断与换页的所有权。
 * @note        依赖显示SDRAM READY且I/D Cache关闭；不设置DMA2D、MPU、RCC或NVIC。
 */
#ifndef SBCG_BSP_TOUCHGFX_DISPLAY_H
#define SBCG_BSP_TOUCHGFX_DISPLAY_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BSP_TOUCHGFX_WIDTH 480U
#define BSP_TOUCHGFX_HEIGHT 272U
#define BSP_TOUCHGFX_FRAME_BYTES (BSP_TOUCHGFX_WIDTH * BSP_TOUCHGFX_HEIGHT * 2U)
#define BSP_TOUCHGFX_FRAME_A UINT32_C(0xC0000000)
#define BSP_TOUCHGFX_FRAME_B UINT32_C(0xC0040000)
typedef enum {
    BSP_TOUCHGFX_OFF, BSP_TOUCHGFX_READY, BSP_TOUCHGFX_RUNNING, BSP_TOUCHGFX_ERROR
} BSP_TouchGFXDisplayStatus;
/** @brief 前台只读副本；front_address为框架已报告可见的缓冲地址，错误计数饱和。 */
typedef struct {
    BSP_TouchGFXDisplayStatus status;
    uint32_t front_address, underrun_errors, transfer_errors, hal_errors, config_errors;
} BSP_TouchGFXDisplaySnapshot;
/**
 * @brief 关背光，清零双缓冲，然后调用生成MX_LTDC_Init并检查实际配置。
 * @return 内存与硬件匹配为true；错误保持终态，未READY不访问外存或LTDC。
 * @note 音频启动之前前台调用，清屏最多两次261120字节，不进行SDRAM自检。
 */
bool BSP_TouchGFXDisplay_Init(void);
/** @brief 前台检查原始FU/TE标志及HAL错误锁存，失败关显示。@return 无返回值，不清IRQ标志。 */
void BSP_TouchGFXDisplay_Process(void);
/**
 * @brief 同一缓冲的实际绘制完成与可见证据均已具备后，由前台打开显示和背光。
 * @param address 框架报告的可见缓冲，只接受固定A/B地址，单位字节。
 * @return 接受为true，错误或非法地址为false并保持关闭。
 * @note 调用方按地址关联完成/可见证据，不依赖两通知先后；本接口不得从IRQ调用。
 */
bool BSP_TouchGFXDisplay_Reveal(uint32_t address);
/** @brief 复制软件诊断，不读帧缓冲。@param out 可空调用方副本，不保留地址。@return 无返回值。 */
void BSP_TouchGFXDisplay_GetSnapshot(BSP_TouchGFXDisplaySnapshot *out);
#ifdef __cplusplus
}
#endif
#endif
