/**
 * @file        bsp_lcd.h
 * @brief       提供RGB565显示双缓冲的独占绘制、发布和只读诊断接口。
 * @details     首帧完成后才启动生成的LTDC，后续前台轮询垂直消隐换页。
 * @note        依赖SDRAM显示READY、现有非缓存MPU及关闭的I/D Cache；仅前台串行调用。
 */
#ifndef SBCG_BSP_LCD_H
#define SBCG_BSP_LCD_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_LCD_WIDTH 480U
#define BSP_LCD_HEIGHT 272U
#define BSP_LCD_FRAME_BYTES (BSP_LCD_WIDTH * BSP_LCD_HEIGHT * 2U)
#define BSP_LCD_FRAME_A UINT32_C(0xC0000000)
#define BSP_LCD_FRAME_B UINT32_C(0xC0040000)
#define BSP_LCD_RELOAD_TIMEOUT_MS 100U

typedef enum {
    BSP_LCD_OFF, BSP_LCD_READY, BSP_LCD_RUNNING, BSP_LCD_ERROR
} BSP_LcdStatus;

/** @brief 地址以字节为单位；累计计数仅供前台日志，无帧缓冲读取。 */
typedef struct {
    BSP_LcdStatus status;
    uint32_t front_address, pending_address, render_address;
    uint32_t presented_frames, underrun_errors, transfer_errors;
    uint32_t reload_timeouts, config_errors, hal_errors;
} BSP_LcdSnapshot;

/**
 * @brief 取得显示内存就绪状态，不启动LTDC、不访问帧缓冲。
 * @return 依赖匹配且可开始绘制为true；未READY或既有致命错误为false。
 * @note 在BSP_SDRAM_InitDisplay成功后调用；重复调用不重置所有权、计数或硬件。
 */
bool BSP_LCD_Init(void);
/**
 * @brief 租出唯一可写缓冲：首帧A，运行时为非前台缓冲。
 * @return 调用方借用的RGB565像素指针；未就绪、已有租约、待换页或错误时返回NULL。
 * @note 最多写480×272像素；指针仅在本次租约有效，Present/Cancel后禁止继续访问。
 */
uint16_t *BSP_LCD_BeginFrame(void);
/**
 * @brief 提交调用方已完整绘制的当前租约，首帧启动LTDC并打开显示/背光。
 * @param now_ms 当前HAL毫秒计数，后续VBR超时使用无符号差值处理回绕。
 * @return 成功发布或排队为true；无租约或错误为false。
 * @note 后续发布等待Process确认垂直消隐；提交后调用方立即失去写权限。
 */
bool BSP_LCD_Present(uint32_t now_ms);
/**
 * @brief 轮询LTDC错误和待换页完成，100ms超时转为致命错误并关闭背光。
 * @param now_ms 当前HAL毫秒计数，可回绕。
 * @return 无返回值；仅前台调用，不引入LTDC中断或DMA。
 */
void BSP_LCD_Process(uint32_t now_ms);
/**
 * @brief 归还尚未发布的绘制租约，允许下一次重新绘制。
 * @return 无返回值；不改变扫描/待换页所有权。
 * @note 前台使用，调用后原指针失效；错误状态不自动恢复。
 */
void BSP_LCD_CancelFrame(void);
/**
 * @brief 复制显示状态及累计错误计数。
 * @param snapshot 调用方输出，NULL安全无操作；不保存指针。
 * @return 无返回值；只读前台接口，不触碰帧缓冲。
 */
void BSP_LCD_GetSnapshot(BSP_LcdSnapshot *snapshot);
#endif
