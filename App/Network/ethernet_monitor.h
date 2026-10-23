/**
 * @file        ethernet_monitor.h
 * @brief       轮询以太网PHY状态并输出初始化与链路日志。
 * @details     对外定义本模块的接口与回调约定。经BSP_Ethernet读取PHY信息、速率与双工状态；初始化失败按2秒重试，链路按250毫秒检查，变化或5秒时输出。
 * @note        前台串行调用；不配置MAC、DMA、NVIC或CubeMX引脚。
 */

#ifndef SBCG_ETHERNET_MONITOR_H
#define SBCG_ETHERNET_MONITOR_H

#include <stdint.h>

/**
 * @brief   向应用提交一行观测日志的回调类型。
 * @param   line 只读日志字符缓冲，不可为NULL；仅本次调用有效，需同步消费或复制。
 * @param   length 有效字符数，单位字节，不包含结尾零字符。
 * @return  无返回值。
 * @note    模块保存函数指针供前台使用；不得保留调用方局部缓冲指针。
 */
typedef void (*EthernetMonitor_Log)(const char *line, int length);

/**
 * @brief   保存日志接口并复位PHY轮询与重试状态。
 * @param   now_ms 前台毫秒计数；初始化差值允许无符号回绕。
 * @param   log_line 日志回调，可为NULL以停用处理；后续调用期间须有效。
 * @return  无返回值。
 * @note    不访问PHY；首次Process即可尝试初始化。
 */
void EthernetMonitor_Init(uint32_t now_ms, EthernetMonitor_Log log_line);
/**
 * @brief   按实际经过的毫秒数重试初始化、读取并报告PHY链路。
 * @param   now_ms 前台当前毫秒计数，差值以uint32_t处理回绕。
 * @return  无返回值；读取失败会回到初始化重试状态。
 * @note    主循环调用，非精确周期任务；同步调用BSP与日志接口。
 */
void EthernetMonitor_Process(uint32_t now_ms);

#endif
