/**
 * @file        touchgfx_service.h
 * @brief       NoOS TouchGFX状态页前台服务与轻量帧通知接口。
 * @details     每秒准备共享业务文本；允许工作时每轮仅调用一次真正的框架入口。
 * @note        生成主循环的同名入口由链接包装成空操作；仅服务拥有真实入口调用权。
 */
#ifndef SBCG_TOUCHGFX_SERVICE_H
#define SBCG_TOUCHGFX_SERVICE_H
#include "status_view.h"
#ifdef __cplusplus
extern "C" {
#endif
/** @brief frames为endFrame次数，含无绘图帧、可接近VSYNC频率；data_generation为约1Hz数据版本，两者含义不同。 */
typedef struct {
    bool initialized, ready, failed, visible;
    uint32_t frames, visible_address, data_generation;
    UI_StatusSnapshot page;
} UI_TouchGFXSnapshot;
/**
 * @brief 在显示内存就绪后启动BSP，再初始化TouchGFX，重复调用无操作。
 * @param now_ms HAL毫秒，初始化数据及日志节流起点。
 * @param log 可空日志出口，返回前复制或消费文本，不保留指针。
 * @return 无返回值；初始化失败不重试硬件，不进入框架。
 */
void UI_TouchGFX_Init(uint32_t now_ms, void (*log)(const char *, int));
/**
 * @brief 前台检查显示，更新约1Hz文本，调用一次真实NoOS入口并处理可见通知。
 * @param now_ms HAL毫秒，无符号差处理回绕。
 * @param allow false时跳过全部UI工作和日志，供DHT低电平保护。
 * @return 无返回值；不消费PCM/候选队列，不自行等待VSYNC。
 */
void UI_TouchGFX_Process(uint32_t now_ms, bool allow);
/**
 * @brief 复制最近准备的格式化文本，供View前台setup/handleTick使用。
 * @param out 非NULL调用方文本副本，不保留地址。
 * @param generation 非NULL输出数据版本，用于避免重复更新View。
 * @return 有可用文本为true；无文本或参数错误为false且不改输出。
 */
bool UI_TouchGFX_GetText(UI_StatusText *out, uint32_t *generation);
/**
 * @brief 由TouchGFXHAL::endFrame父类返回后前台通知本轮完成与实际绘制地址。
 * @param rendered_address 父类调用之前捕获的绘制缓冲字节地址；A/B记为已绘制，0表示本轮未绘图。
 * @return 无返回值；所有endFrame均计数，只有合法A/B才增加缓冲绘制完成证据。
 * @note 与最近可见地址按A/B关联，不依赖通知先后，不屏蔽中断、不画图或输出日志。
 */
void UI_TouchGFX_FrameCompleted(uint32_t rendered_address);
/**
 * @brief 由setTFTFrameBuffer父类返回后通知可见缓冲，仅保存原子地址/标志。
 * @param address 可见帧地址，单位字节，只认可固定A/B缓冲。
 * @return 无返回值；IRQ或前台可调用，不操作GPIO、不输出日志。
 * @note 初始化或首次endFrame之前的合法通知也保留；Process仅在同地址已有实际绘制完成证据时开启背光。
 */
void UI_TouchGFX_BufferVisible(uint32_t address);
/** @brief 复制服务诊断。@param out 可空调用方输出，不保留地址。@return 无返回值，仅前台调用。 */
void UI_TouchGFX_GetSnapshot(UI_TouchGFXSnapshot *out);
#ifdef __cplusplus
}
#endif
#endif
