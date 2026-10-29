/**
 * @file        ptp_tx.h
 * @brief       观测v1 UDP探测应答的MAC发送描述符时间。
 * @details     对外定义本模块的接口与回调约定。包装gnetif.linkoutput，只对UID匹配的type=3单段帧申请TX时间戳，调用原输出后取回快照，4项CPU队列交前台日志。
 * @note        NO_SYS=1且同步发送；在PTP时钟与RX观察器之后、UDPTimeExchange外层之前初始化；不修改DMA配置。
 */

#ifndef SBCG_PTP_TX_H
#define SBCG_PTP_TX_H
#include <stdint.h>
/**
 * @brief   向应用提交一行观测日志的回调类型。
 * @param   line 只读日志字符缓冲，不可为NULL；仅本次调用有效，需同步消费或复制。
 * @param   length 有效字符数，单位字节，不包含结尾零字符。
 * @return  无返回值。
 * @note    模块保存函数指针供前台使用；不得保留调用方局部缓冲指针。
 */
typedef void (*PtpTx_Log)(const char *line, int length);
/* NO_SYS main context, after the PTP clock and RX observer initialize. */
/**
 * @brief   复制UID、复位TX队列统计并安装链路输出包装。
 * @param   now_ms 初始化毫秒计数，作为日志节流起点。
 * @param   uid_words 至少3个UID字，允许NULL以禁用匹配并记录错误；内容复制，不保存指针。
 * @param   log 日志回调，可为NULL；后续调用期间须有效。
 * @return  无返回值；缺失UID或原输出时增加初始化错误。
 * @note    PTP时钟及RX观察器之后前台初始化；重新初始化前须先卸载UDPTimeExchange外层。
 */
void PtpTx_Init(uint32_t now_ms, const uint32_t uid_words[3], PtpTx_Log log);
/**
 * @brief   回收已完成TX请求，并限速输出队列记录和统计。
 * @param   now_ms 前台当前毫秒计数，日志按无符号差值每秒节流。
 * @return  无返回值。
 * @note    每次调用先尝试清理；DMA仍持有OWN时不强行回收，日志固定SYNC=0。
 */
void PtpTx_Process(uint32_t now_ms);
#endif
