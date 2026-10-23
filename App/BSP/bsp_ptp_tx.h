/**
 * @file        bsp_ptp_tx.h
 * @brief       同步单缓冲以太网发送的硬件 TX 时间戳交接。
 * @details     声明 TX 请求清理、布防、结束采样三接口，复用 bsp_ptp.h 的状态和秒/纳秒类型；调用方在 Begin 与
 *          End 之间完成同步发送。
 * @note        限 NO_SYS 单前台同步发送，要求增强描述符且环映射稳定；一次一组 Begin/End，不拥有传入帧，不在
 *          DMA OWN 期间改写描述符。
 */

#ifndef SBCG_BSP_PTP_TX_H
#define SBCG_BSP_PTP_TX_H
#include "bsp_ptp.h"

/* Main context, synchronous single-buffer TX only. One Begin/End at a time. */
/**
 * @brief   清理本模块已完成发送描述符上的时间戳请求位。
 * @return  OK 表示无待清理请求；NOT_READY 表示仍已布防或存在 DMA 所有描述符；其它为环配置错误。
 * @note    仅单一前台 NO_SYS 发送流程；只在 OWN 清零后清 TTSE，用屏障交接，不触碰 DMA 仍拥有的描述符。HAL 超
 *          时已清状态字的情况不重复改写。
 */
BSP_PtpStatus BSP_PTP_TxClean(void);
/**
 * @brief   为即将同步发送的单缓冲以太网帧布防一次硬件 TX 时间戳。
 * @param   buffer 非空只读帧首地址；调用方保持发送期间有效，不转移所有权，模块仅保存数值地址以便 End 校验。
 * @param   length 帧长度，单位字节；必须大于零且不超过单描述符 TBS1 容量。
 * @return  OK 表示当前空闲描述符已插入时间戳请求；其它表示参数、描述符、PTP 或 HAL 状态不符。
 * @note    仅主循环，须在一次同步单缓冲发送前调用；一次只允许一组 Begin/End。清旧 TTSS 并保存发送前 PTP 时刻
 *          ；Begin 本身不发送帧，不支持并发提交。
 */
BSP_PtpStatus BSP_PTP_TxBegin(const uint8_t *buffer, uint16_t length);
/**
 * @brief   在同步发送返回后验证所布防帧的 DMA TX 时间戳。
 * @param   time 调用者拥有的输出；NULL 会使已布防采样失败，成功才写入，失败保留原内容。
 * @return  OK 表示描述符、缓冲地址、长度及发送前后时间窗全部匹配；其它表示未布防或快照/配置/HAL 错误。
 * @note    仅与成功 Begin 配对的前台路径；即使失败也解除 armed 并尝试清理。要求 OWN 已归 CPU、单帧 FS/LS/TTSS
 *          且无 ES，再读 DESC6/7；屏障不等于 D-Cache 维护，当前启动代码未启用缓存。
 */
BSP_PtpStatus BSP_PTP_TxEnd(BSP_PtpTime *time);

#endif
