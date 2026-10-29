/**
 * @file        status_service.h
 * @brief       状态页的前台快照、八行分片绘制与限速诊断接口。
 * @details     每帧固定一份业务副本；首帧显示后约每秒更新，超龄帧放弃。
 * @note        由App在归还音频块之后串行调度，不借用PCM，不增加IRQ或DMA。
 */
#ifndef SBCG_STATUS_SERVICE_H
#define SBCG_STATUS_SERVICE_H
#include "status_view.h"
#include <stdbool.h>
#include <stdint.h>
/** @brief 软件绘制状态；page是当前正在绘制/等待发布的副本，不代表物理屏幕观测。 */
typedef struct {
    bool initialized, rendering;
    uint16_t rendered_rows;
    uint32_t discarded_frames;
    UI_StatusSnapshot page;
} UI_StatusServiceSnapshot;
/**
 * @brief 依赖SDRAM显示READY取得LCD服务，不立即扫描未完成的画面。
 * @param now_ms 当前HAL毫秒，作为绘制与日志节流起点。
 * @param log 可空日志出口；回调须返回前复制或消费文本，不保留文本地址。
 * @return 无返回值；重复成功初始化不重置缓冲所有权，失败不重试硬件。
 */
void UI_StatusService_Init(uint32_t now_ms, void (*log)(const char *,int));
/**
 * @brief 轮询换页并绘制最多八行；完成且年龄小于2000ms才发布。
 * @param now_ms 当前HAL毫秒，无符号差支持回绕。
 * @param allow_work false时整次不绘制、不查询业务或输出，供DHT低电平门控。
 * @return 无返回值；每帧开始读取一次业务副本，不消费PCM、候选或同步队列。
 */
void UI_StatusService_Process(uint32_t now_ms, bool allow_work);
/**
 * @brief 复制服务软件状态，不读取外设。
 * @param out 可空输出，仅调用期间写入，调用方持有副本。
 * @return 无返回值；不改变绘制状态或缓冲租约。
 */
void UI_StatusService_GetSnapshot(UI_StatusServiceSnapshot *out);
#endif
