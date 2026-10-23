/**
 * @file        ptp_rx.h
 * @brief       为已校验的v1 UDP探测请求关联MAC接收描述符时间。
 * @details     对外定义本模块的接口与回调约定。包装gnetif.input，在协议栈同步分发前保存匹配帧的RX快照，UDP回调仅可认领一次，记录进入4项CPU队列后前台输出。
 * @note        NO_SYS=1；需先初始化LwIP及PTP时钟；在UDPTimeExchange外层安装前初始化，快照窗口不能跨帧保存。
 */

#ifndef SBCG_PTP_RX_H
#define SBCG_PTP_RX_H

#include <stdint.h>

/**
 * @brief   向应用提交一行观测日志的回调类型。
 * @param   line 只读日志字符缓冲，不可为NULL；仅本次调用有效，需同步消费或复制。
 * @param   length 有效字符数，单位字节，不包含结尾零字符。
 * @return  无返回值。
 * @note    模块保存函数指针供前台使用；不得保留调用方局部缓冲指针。
 */
typedef void (*PtpRx_Log)(const char *line, int length);
/* NO_SYS main context, after MX_LWIP_Init and PtpClock_Init. */
/**
 * @brief   复位RX观测队列与统计，并安装输入包装。
 * @param   now_ms 初始化毫秒计数，用于日志节流。
 * @param   log 日志回调，可为NULL；后续调用期间须有效。
 * @return  无返回值；原输入为空时累计初始化错误且不安装。
 * @note    先完成MX_LWIP_Init及PtpClock_Init；如已装UDPTimeExchange，须先卸载该外层以免形成包装环。
 */
void PtpRx_Init(uint32_t now_ms, PtpRx_Log log);
/* Call once for a validated v1 request, before releasing its pbuf. */
/**
 * @brief   为一个已验证探测请求一次性认领当前RX快照并排队。
 * @param   request 至少28字节的已校验v1请求，可为NULL并按缺失计数；只复制序号、毫秒字段和快照。
 * @return  无返回值；未匹配、无效快照和队列满分别累计统计。
 * @note    由UDPProbe RAW回调在Input返回前调用；无论认领成功与否都会消耗本帧可用标志，不保存请求指针。
 */
void PtpRx_Observe(const uint8_t request[28]);
/**
 * @brief   按一秒节流输出至多一条RX队列记录及累计统计。
 * @param   now_ms 前台当前毫秒计数，按无符号差值处理回绕。
 * @return  无返回值。
 * @note    主循环调用；日志SYNC=0，仅表示本地DMA时间戳观测。
 */
void PtpRx_Process(uint32_t now_ms);

#endif
