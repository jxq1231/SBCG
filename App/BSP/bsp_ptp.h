/**
 * @file        bsp_ptp.h
 * @brief       ETH PTP本地读时、最近RX硬件戳及B板有界校正接口。
 * @details     声明秒/十进制纳秒结构、A板粗更新及B板精细更新初始化、绝对频率修正、相位步进和不回绕代际。
 * @note        仅NO_SYS前台，依赖200MHz HCLK、HAL_ETH_USE_PTP和ETH初始化。调用者负责角色、TX空闲与
 *              网络/音频旧数据清理；接口成功仅表示本地硬件命令完成，不是跨板同步或声学精度验收。
 */

#ifndef SBCG_BSP_PTP_H
#define SBCG_BSP_PTP_H

#include <stdint.h>

/** @brief ETH PTP的就绪、时钟、HAL、初始化/更新超时、配置、快照、忙、范围及代际结果；保留旧状态编号。 */
typedef enum
{
    BSP_PTP_OK,
    BSP_PTP_NOT_READY,
    BSP_PTP_BAD_CLOCK,
    BSP_PTP_HAL_ERROR,
    BSP_PTP_INIT_TIMEOUT,
    BSP_PTP_BAD_CONFIG,
    BSP_PTP_BAD_SNAPSHOT,
    BSP_PTP_BUSY,
    BSP_PTP_OUT_OF_RANGE,
    BSP_PTP_UPDATE_TIMEOUT,
    BSP_PTP_GENERATION_EXHAUSTED
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
 * @brief   启动供B板驯服的200MHz、10ns精细更新本地PTP时钟。
 * @return  OK表示有界初始化与寄存器校验完成，其它状态表示未就绪、时钟、超时或代际耗尽。
 * @note    仅启动前台、ETH初始化后调用；不可用于在线切换已就绪时钟。名义addend为0x80000000。
 */
BSP_PtpStatus BSP_PTP_InitFine(void);
/**
 * @brief   设置相对于名义addend的绝对频率修正。
 * @param   correction_ppb 有符号十亿分比，范围-100000..100000；正值加快本地时钟。
 * @return  OK表示更新完成；范围、模式、忙、超时或代际耗尽返回对应错误。
 * @note    仅B板fine模式前台；调用者须先保证BSP_PTP_TxClean()==OK并离开网络回调。
 *          更新改变速率代际；超时使时钟失效且改变epoch，不表示硬件已回滚。
 */
BSP_PtpStatus BSP_PTP_SetFrequencyPpb(int32_t correction_ppb);
/**
 * @brief   通过新鲜读时与绝对TSSTI覆盖实施一次相位校正。
 * @param   correction_ns 有符号纳秒修正，正值推进本地时钟；目标须可由uint32秒和十进制纳秒表示。
 * @return  OK表示命令完成；读时、范围、模式、忙、超时或代际耗尽返回对应错误。
 * @note    仅B板fine模式前台，调用者须保证TxClean为OK并清理旧交换/锚点。
 *          读写延迟由后续伺服收敛；非零步进提升epoch与速率代际，失败不承诺回滚。
 */
BSP_PtpStatus BSP_PTP_Step(int64_t correction_ns);
/**
 * @brief   读取时钟初始化、相位或失效的代际。
 * @return  当前uint32代际；0表示尚未成功开始初始化，达到UINT32_MAX后禁止复用。
 * @note    仅前台；代际不证明时钟就绪或跨板同步，队列和锚点须自行比较。
 */
uint32_t BSP_PTP_GetEpoch(void);
/**
 * @brief   读取初始化、频率更新或相位变化的模型代际。
 * @return  当前uint32速率代际；耗尽后不回绕。
 * @note    仅前台，用于拒绝跨速率边界的音频插值，不代表同步质量。
 */
uint32_t BSP_PTP_GetRateRevision(void);
/**
 * @brief   读取最近成功应用的频率修正。
 * @return  有符号ppb；初始化名义频率或coarse模式为0。
 * @note    仅前台；失效后数值仅为历史记录，不能代替就绪判断。
 */
int32_t BSP_PTP_GetFrequencyPpb(void);
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
