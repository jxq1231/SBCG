/**
 * @file        status_snapshot.h
 * @brief       为RGB565与TouchGFX提供同一份业务只读快照接口。
 * @details     复制身份、网络、同步、温度、音频和检测计数，不读取PCM或消费队列。
 * @note        前台串行调用；音频时效以复制音频副本之后的毫秒计数判断。
 */
#ifndef SBCG_STATUS_SNAPSHOT_H
#define SBCG_STATUS_SNAPSHOT_H
#include "status_view.h"
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 复制当前业务状态，尚无测距时保留无效标志。
 * @param now_ms HAL毫秒，作为除音频外各业务查询的时效参考，可回绕。
 * @param out 可空输出副本；不保存地址，不保留业务指针。
 * @return 无返回值；NULL不查询业务，A板参考不伪造零偏差。
 */
void UI_StatusSnapshot_Read(uint32_t now_ms, UI_StatusSnapshot *out);
#ifdef __cplusplus
}
#endif
#endif
