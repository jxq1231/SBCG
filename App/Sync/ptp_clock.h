/**
 * @file        ptp_clock.h
 * @brief       初始化并观测本地以太网MAC PTP计时器。
 * @details     对外定义本模块的接口与回调约定。经BSP_PTP读取秒和纳秒，每秒最多输出一次状态，检测停滞或倒退；LOCAL及SYNC=0明确表示未建立同步。
 * @note        在LwIP/ETH初始化完成后由前台初始化和轮询；不执行跨节点时钟校正。
 */

#ifndef SBCG_PTP_CLOCK_H
#define SBCG_PTP_CLOCK_H

#include <stdint.h>

/**
 * @brief   向应用提交一行观测日志的回调类型。
 * @param   line 只读日志字符缓冲，不可为NULL；仅本次调用有效，需同步消费或复制。
 * @param   length 有效字符数，单位字节，不包含结尾零字符。
 * @return  无返回值。
 * @note    模块保存函数指针供前台使用；不得保留调用方局部缓冲指针。
 */
typedef void (*PtpClock_Log)(const char *line, int length);

/* Local MAC clock observation, never a declaration of synchronization. */
/**
 * @brief   复位本地时钟观测状态，并调用BSP初始化MAC PTP计数器。
 * @param   now_ms 前台初始化毫秒计数，作为日志节流起点。
 * @param   log 日志回调，可为NULL；后续处理期间须有效。
 * @return  无返回值；BSP初始化状态保存至后续日志。
 * @note    在MX_LWIP_Init之后前台执行；初始化本地计数不代表与PC同步。
 */
void PtpClock_Init(uint32_t now_ms, PtpClock_Log log);
/**
 * @brief   每秒最多读取一次本地PTP时间并报告运行或错误状态。
 * @param   now_ms 前台当前毫秒计数，按无符号差值节流并允许回绕。
 * @return  无返回值。
 * @note    LOCAL日志固定SYNC=0；读失败、停滞或倒退累计错误，不执行校时。
 */
void PtpClock_Process(uint32_t now_ms);

/**
 * @brief   清除上次读时快照，使受控校时后的首个样本不与旧时间域比较。
 * @return  无返回值。
 * @note    仅串行前台，校时或频率变化边界调用；不重启外设，不重置输出序号/累计计数。
 */
void PtpClock_NotifyClockChange(void);

#endif
