/**
 * @file        udp_time_exchange.h
 * @brief       关联一次UDP时间交换的板端接收、发送硬件时间与PC提交时刻。
 * @details     对外定义本模块的接口与回调约定。外包gnetif输入输出函数，记录type=4请求和type=5应答的t2/t3，前台发送type=6报告，再将type=7提交的t1/t4送入观测队列。
 * @note        仅支持NO_SYS同步链；必须在旧PtpRx/PtpTx包装之后安装，重新安装旧包装前先卸载本外层。仅观测，不校准时钟。
 */

#ifndef SBCG_UDP_TIME_EXCHANGE_H
#define SBCG_UDP_TIME_EXCHANGE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   读取当前板端毫秒计数的回调类型。
 * @return  uint32_t毫秒计数，允许自然回绕。
 * @note    服务保存回调并在NO_SYS前台输入/输出链调用；回调须持续有效，不传递上下文指针。
 */
typedef uint32_t (*UDPTimeExchange_Tick)(void);
/**
 * @brief   向应用提交一行观测日志的回调类型。
 * @param   text 只读日志字符缓冲，不可为NULL；仅本次调用有效，需同步消费或复制。
 * @param   length 有效字符数，单位字节，不包含结尾零字符。
 * @return  无返回值。
 * @note    模块保存函数指针供前台使用；不得保留调用方局部缓冲指针。
 */
typedef void (*UDPTimeExchange_Log)(const char *text, int length);

/* NO_SYS main context only. Failure leaves an unknown wrapper chain untouched. */
/**
 * @brief   确认外层所有权后卸载包装并清空服务状态。
 * @return  true表示已卸载或本来未安装；false表示回调活动或包装链所有权不符，链保持原样。
 * @note    仅在串行前台调用；成功恢复原input/linkoutput、移除PCB并清空记录，旧PtpRx/PtpTx包装保留。
 */
bool UDPTimeExchange_Deinit(void);
/**
 * @brief   安全卸载旧服务后校验依赖并安装时间交换外层包装。
 * @param   now_ms 初始化毫秒计数，允许首次Process立即尝试绑定。
 * @param   uid_words 至少3个UID字，不可为NULL且不能全零；内容复制到模块。
 * @param   tick 不可为NULL的毫秒读取回调，后续包装执行期间须有效。
 * @param   log 日志回调，可为NULL；非NULL时后续使用期间须有效。
 * @return  true表示包装已安装并配置；false表示卸载失败或参数/原输入输出无效。
 * @note    必须先安装PtpRx/PtpTx；实际UDP PCB由Process懒创建，初始化失败不表示已有包装仍在。
 */
bool UDPTimeExchange_Init(uint32_t now_ms, const uint32_t uid_words[3],
                          UDPTimeExchange_Tick tick, UDPTimeExchange_Log log);
/**
 * @brief   在前台重试绑定、回收过期记录并发送至多一个READY报告。
 * @param   now_ms 前台当前毫秒计数，重试、过期和日志均使用无符号差值。
 * @return  无返回值。
 * @note    回调活动时立即返回；每次至多尝试一条type=6报告且失败不自动重试，随后限速输出统计。
 */
void UDPTimeExchange_Process(uint32_t now_ms);

/**
 * @brief   在回调之外清除旧硬件配对，保留PCB、钩子与累计统计。
 * @return  true表示已清除；false表示回调活动，未修改状态。
 * @note    仅串行前台，校时或频率变化边界调用；不重启外设，不重置输出序号/累计计数。
 */
bool UDPTimeExchange_NotifyClockChange(void);

#endif
