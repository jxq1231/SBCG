/**
 * @file        bsp_ptp.h
 * @brief       本地 ETH PTP 时钟及最近接收帧硬件戳的访问。
 * @details     声明 PTP 状态和 uint32_t 秒/纳秒时间结构，以及初始化、本地读时、最近 RX 戳接口；成功时间的
 *          nanoseconds 小于十亿。
 * @note        仅前台，依赖 HAL_ETH_USE_PTP 一致编译、已有 ETH/LwIP 初始化和增强描述符。提供本地观测时钟，不
 *          代表跨板同步或声学时间验收。
 */

#ifndef SBCG_BSP_PTP_H
#define SBCG_BSP_PTP_H

#include <stdint.h>

/** @brief ETH PTP 的就绪、时钟、HAL、初始化超时、配置及快照校验结果。 */
typedef enum
{
    BSP_PTP_OK,
    BSP_PTP_NOT_READY,
    BSP_PTP_BAD_CLOCK,
    BSP_PTP_HAL_ERROR,
    BSP_PTP_INIT_TIMEOUT,
    BSP_PTP_BAD_CONFIG,
    BSP_PTP_BAD_SNAPSHOT
} BSP_PtpStatus;

/** @brief 本机硬件时间值：uint32_t 秒及十进制纳秒；成功读出的纳秒小于 1000000000。 */
typedef struct
{
    uint32_t seconds;
    uint32_t nanoseconds;
} BSP_PtpTime;

/* Main context after MX_LWIP_Init, with HAL tick running. Local time only. */
/**
 * @brief   按当前 200 MHz HCLK 初始化本地 ETH PTP 粗更新时钟。
 * @return  OK 表示 HAL 配置完成且请求位在双重预算内消退；其它区分 ETH、HCLK、HAL、超时和配置错误。
 * @note    仅主循环、MX_LWIP_Init 之后且 HAL tick 运行时调用；增量 5 ns，十进制纳秒回绕并对全部接收帧打戳。开
 *          启 PTP 时钟门是已有实现，不做跨节点校时/驯服，也不改 CubeMX 系统时钟。
 */
BSP_PtpStatus BSP_PTP_Init(void);
/**
 * @brief   读取并校验本地 ETH PTP 秒/纳秒快照。
 * @param   time 调用者拥有的输出；NULL 返回 BAD_SNAPSHOT，失败不改原值，成功纳秒范围为 0..999999999。
 * @return  OK 表示秒二次核对一致；其它为未就绪、配置/HAL 失败或三次读取仍跨秒等快照错误。
 * @note    仅前台，依赖成功初始化；HAL 读秒与纳秒后再次检查秒寄存器，避免跨秒混合读。输出只代表本机硬件时钟。
 */
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *time);
/* Last completed frame cache: use synchronously before reading another frame. */
/**
 * @brief   提取最近完成接收帧的 HAL 缓存硬件时间戳。
 * @param   time 调用者拥有的输出；NULL 返回 BAD_SNAPSHOT，失败保持原值，成功单位为秒与纳秒。
 * @return  OK 表示配置、完成帧状态和纳秒字段有效；其它为未就绪、配置错误、HAL 失败或快照不合格。
 * @note    仅前台，ETH 须 STARTED，使用增强描述符及全帧接收时间戳；必须在读取下一帧前同步消费此缓存。只校验当
 *          前完成帧，不自行关联 UDP 请求。
 */
BSP_PtpStatus BSP_PTP_ReadRx(BSP_PtpTime *time);

#endif
