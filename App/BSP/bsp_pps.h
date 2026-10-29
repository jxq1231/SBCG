/**
 * @file        bsp_pps.h
 * @brief       声明PTP目标比较驱动TIM2整秒脉冲的前台接口。
 * @details     提供生成配置校验、安全启停、有时效预算的整秒安排及软件观察快照。
 * @note        仅串行前台；依赖CubeMX生成的PA15/TIM2_CH1、下拉、Reset/ITR1和ETH PTP时钟。
 *              软件计数不是引脚边沿实测；CPU完全停止超过约71分钟时无法阻止计数自然回绕。
 */
#ifndef SBCG_BSP_PPS_H
#define SBCG_BSP_PPS_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 前台PPS配置、启动、时钟和目标比较的处理结果。 */
typedef enum
{
    BSP_PPS_OK = 0,
    BSP_PPS_NOT_READY,
    BSP_PPS_BAD_CONFIG,
    BSP_PPS_START_TIMEOUT,
    BSP_PPS_CLOCK_ERROR,
    BSP_PPS_TARGET_ERROR
} BSP_PpsStatus;

/** @brief 软件观察状态；emitted只统计时间/代际及硬件触发状态相符的事件，不代表示波器验收。 */
typedef struct
{
    bool configured;
    bool armed;
    uint32_t target_seconds;
    uint32_t emitted;
    uint32_t missed;
    uint32_t errors;
    uint32_t epoch;
} BSP_PpsSnapshot;

/**
 * @brief   校验CubeMX资源并使输出处于安全停用状态。
 * @return  OK表示配置匹配且已停用；其它表示资源未就绪或配置/停用错误。
 * @note    仅应用初始化前台；不重写GPIO、时钟、通道模式、IRQ或DMA配置，不要求PTP已初始化。
 */
BSP_PpsStatus BSP_PPS_Init(void);

/**
 * @brief   先关闭TIM输出再停止计数，取消目标比较并清除待决状态。
 * @return  OK表示已确认输出/计数和目标关闭；其它表示无法确认安全停用。
 * @note    仅串行前台；允许在Init之前调用，不读取PTP时间，供PTP初始化、失效和相位覆盖前保护。
 */
BSP_PpsStatus BSP_PPS_Stop(void);

/**
 * @brief   消费硬件触发观察并在资格预算内提前安排一个未来整秒。
 * @param   valid_for_ms 当前调用时剩余资格时间，单位毫秒；0立即取消输出。
 * @param   schedule_allowed 是否允许启动或安排新目标；false仍执行安全检查和已触发状态消费。
 * @return  OK表示安全处理完成；其它表示配置、计数、时钟或目标异常并撤销输出。
 * @note    主循环反复调度，不是精确周期；目标至少提前100ms，目标之后100ms也必须在资格预算内。
 *          不补发过去目标、不输出日志，不操作IRQ/DMA；长期CPU停止的硬件边界见文件说明。
 */
BSP_PpsStatus BSP_PPS_Process(uint32_t valid_for_ms, bool schedule_allowed);

/**
 * @brief   复制当前软件观察状态。
 * @param   out 调用者拥有的输出，非NULL时复制；NULL时不写内存。
 * @return  非NULL返回true，NULL返回false；configured字段单独表示是否已完成初始化校验。
 * @note    仅串行前台；快照不读取或清除硬件状态，也不证明引脚边沿或跨板精度。
 */
bool BSP_PPS_GetSnapshot(BSP_PpsSnapshot *out);

#endif
