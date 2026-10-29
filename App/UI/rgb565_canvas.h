/**
 * @file rgb565_canvas.h
 * @brief 调用方提供RGB565内存的有界矩形与5x7 ASCII文本绘制。
 * @details 裁剪矩形/行窗口，不分配内存，不访问固定SDRAM地址或DMA2D。
 * @note framebuffer只有在硬件初始化完成后才能由板级模块传入；当前仅主机验证。
 */
#ifndef SBCG_RGB565_CANVAS_H
#define SBCG_RGB565_CANVAS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** @brief 将0..255的RGB分量截取为565；每个实参求值一次，不执行范围饱和或硬件写入。 */
#define UI_RGB565(r,g,b) ((uint16_t)((((uint16_t)(r)>>3)<<11)|(((uint16_t)(g)>>2)<<5)|((uint16_t)(b)>>3)))
/** @brief pixels由调用方拥有；stride单位uint16像素，clip_end不包含末行。 */
typedef struct { uint16_t *pixels;uint16_t width,height;uint32_t stride;uint16_t clip_first,clip_end; } UI_Canvas;
/**
 * @brief 验证内存布局并建立画布，不清屏。
 * @param out 非NULL输出，失败清零。
 * @param pixels 非NULL调用方可写像素存储，需在后续绘制期间有效。
 * @param pixel_count 可用uint16元素数，包含行跨度空间。
 * @param width 可见宽度像素，非零。
 * @param height 可见高度像素，非零。
 * @param stride 每行uint16元素数，>=width。
 * @return true为布局可用，false为尺寸/容量/溢出错误；无外设访问。
 */
bool UI_CanvasInit(UI_Canvas *out,uint16_t *pixels,size_t pixel_count,uint16_t width,uint16_t height,uint32_t stride);
/** @brief 在非NULL有效画布canvas裁剪绘制矩形，x/y为有符号像素坐标，w/h为尺寸，color为RGB565，无返回值。 */
void UI_FillRect(UI_Canvas *canvas,int32_t x,int32_t y,uint32_t w,uint32_t h,uint16_t color);
/** @brief 在canvas的x/y画最多80个ASCII字符，text只读NUL结尾，scale范围1..4，color为RGB565；NULL/非法参数不绘制，无返回值。 */
void UI_DrawText(UI_Canvas *canvas,int32_t x,int32_t y,const char *text,uint8_t scale,uint16_t color);
#ifdef __cplusplus
}
#endif
#endif
