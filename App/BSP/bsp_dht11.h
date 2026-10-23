/**
 * @file        bsp_dht11.h
 * @brief       DHT11 开漏启动脉冲和基于 DWT 的五字节波形捕获。
 * @details     声明硬件/时序错误枚举，以及 Init、Start、Capture、Abort 前台接口；Capture 输出原始五字节，不解
 *          释校验和或温湿度数值。
 * @note        使用 DHT11_DATA（当前 PG7）与外部上拉；不改 GPIO 模式、TIM、NVIC 或 PTP。中断保持开启，长抢占
 *          可使捕获失败；校验和及数值语义由温度模块处理。
 */

#ifndef SBCG_BSP_DHT11_H
#define SBCG_BSP_DHT11_H

#include <stdint.h>

/** @brief 区分 DHT11 初始化、线路、启动窗口、边沿/整帧超时、脉宽、DWT 时钟及参数错误。 */
typedef enum
{
    BSP_DHT11_OK,
    BSP_DHT11_INIT_ERROR,
    BSP_DHT11_LINE_BUSY,
    BSP_DHT11_START_WINDOW,
    BSP_DHT11_TIMEOUT,
    BSP_DHT11_PULSE_ERROR,
    BSP_DHT11_CLOCK_ERROR,
    BSP_DHT11_ARGUMENT
} BSP_Dht11Status;

/* Main-loop only. Uses generated open-drain GPIO and the core DWT counter;
   never reconfigures GPIO, interrupts, clocks, TIM, DMA or the PTP clock. */
/**
 * @brief   验证已生成 GPIO 与内核频率并启用 DWT 周期计数。
 * @return  OK 表示引脚/时钟约束成立且 CYCCNT 在有限轮询内递增；其它为初始化或计数时钟错误。
 * @note    仅前台、GPIO 初始化后调用；释放开漏线，按整数 MHz 换算 cycles_per_us，不重置 CYCCNT，不重配
 *          GPIO/TIM/PTP。后续脉宽判定依赖核心频率保持一致。
 */
BSP_Dht11Status BSP_DHT11_Init(void);
/* Start only pulls low. Capture releases after a valid 18..30ms low interval,
   receives a bounded frame and leaves output unchanged on failure. */
/**
 * @brief   检查空闲线路并开始 DHT11 主机低电平启动脉冲。
 * @return  OK 表示已拉低；未初始化或线路忙时返回相应错误，线路忙会调用 Abort。
 * @note    仅前台；记录 GPIO 写入前后的 DWT 周期边界，函数不等待 18..30 ms，由温度服务稍后调用 Capture。
 */
BSP_Dht11Status BSP_DHT11_Start(void);
/**
 * @brief   在有效启动低电平窗口释放线路并同步捕获完整 DHT11 波形。
 * @param   frame 调用者拥有的五字节输出；NULL 返回 ARGUMENT 并 Abort。失败不改帧数据，成功输出原始五字节。
 * @return  OK 表示波形捕获完成；其它区分状态、18..30 ms 窗口、超时、计数停滞和脉宽错误。
 * @note    仅前台，须先成功 Start；中断保持开启，单边沿/整帧预算分别为 150 us/6000 us，长 IRQ 抢占仍会延长实
 *          际返回耗时。所有退出路径调用 Abort，已初始化或持有线路时释放数据线，不保证数据校验通过。
 */
BSP_Dht11Status BSP_DHT11_Capture(uint8_t frame[5]);
/**
 * @brief   释放 DHT11 开漏数据线并清除启动脉冲状态。
 * @return  无返回值。
 * @note    仅前台使用；ready 或 started 时将输出置高以释放线路，否则仅清 started。不中断已进入 Capture 的同步
 *          执行。
 */
void BSP_DHT11_Abort(void);

#endif
