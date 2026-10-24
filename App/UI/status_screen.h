/**
 * @file status_screen.h
 * @brief 480x272状态页的RGB565渲染接口。
 * @details 角色、待测距离、同步、网络、温度和音频；支持有界行带，为以后分片刷新预留。
 * @note 只接受已取得独占绘制权的调用方画布，不启动LTDC/SDRAM，无IRQ。
 */
#ifndef SBCG_STATUS_SCREEN_H
#define SBCG_STATUS_SCREEN_H
#include "status_view.h"
#include "rgb565_canvas.h"
#ifdef __cplusplus
extern "C" {
#endif
#define UI_SCREEN_WIDTH 480U
#define UI_SCREEN_HEIGHT 272U
/**
 * @brief 仅更新指定行带；首次显示由调用方遍历完整272行。
 * @param canvas 非NULL已验证480x272画布，不修改其所有权或裁剪信息。
 * @param snapshot 非NULL状态副本，返回后无需保留。
 * @param now_ms 当前毫秒，交给文本层判断陈旧。
 * @param first_row 起始行，0..271。
 * @param row_count 更新行数，非零且不越过272。
 * @return true为已绘制，false参数错误且不写像素；不触碰外设。
 */
bool UI_StatusRenderRows(UI_Canvas *canvas,const UI_StatusSnapshot *snapshot,uint32_t now_ms,uint16_t first_row,uint16_t row_count);
#ifdef __cplusplus
}
#endif
#endif
