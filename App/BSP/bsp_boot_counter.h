/**
 * @file        bsp_boot_counter.h
 * @brief       从保留Flash扇区读取并推进本板跨掉电启动代次。
 * @details     在两个保留扇区中追加带CRC和提交标记的记录，按双扇区轮转擦除；
 *              只有整条记录回读一致后新代次才对上层可见，因此掉电不会把未登记的值交给模式握手。
 * @note        仅在启动阶段、网络/音频/显示服务之前调用一次；不配置引脚、时钟、中断或DMA。
 */
#ifndef SBCG_BSP_BOOT_COUNTER_H
#define SBCG_BSP_BOOT_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 启动代次读取结果码，用于串口稳定输出。 */
typedef enum
{
    BSP_BOOT_COUNTER_OK = 0,
    BSP_BOOT_COUNTER_ERROR_ARGUMENT,
    BSP_BOOT_COUNTER_ERROR_LAYOUT,
    BSP_BOOT_COUNTER_ERROR_EXHAUSTED,
    BSP_BOOT_COUNTER_ERROR_ERASE,
    BSP_BOOT_COUNTER_ERROR_PROGRAM,
    BSP_BOOT_COUNTER_ERROR_VERIFY
} BSP_BootCounterStatus;

/** @brief 本次成功登记后的代次与写入位置，供诊断日志。 */
typedef struct
{
    uint64_t value;  /* 已回读验证并登记的非零代次。 */
    uint8_t sector;  /* 使用的保留扇区下标0或1。 */
    uint16_t slot;   /* 扇区内记录槽下标，范围0..4095。 */
    bool erased;     /* 本次是否先擦除了目标扇区。 */
} BSP_BootCounterResult;

/**
 * @brief   读取历史最大代次并追加一条更大的新记录。
 * @param   out 非NULL输出；成功时写入已回读验证的代次和位置，失败保持清零。
 * @return  OK表示新代次已持久化；其它值表示未登记，调用者不得把它用于模式握手。
 * @note    失败时只在保留扇区留下残写记录或错误；不会擦除保存当前最大值的扇区。
 */
BSP_BootCounterStatus BSP_BootCounter_Next(BSP_BootCounterResult *out);

/**
 * @brief   将状态码转换为稳定短字符串。
 * @param   status 待转换状态。
 * @return  非NULL只读常量字符串。
 */
const char *BSP_BootCounter_StatusName(BSP_BootCounterStatus status);

#endif
