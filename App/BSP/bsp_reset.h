/**
 * @file bsp_reset.h
 * @brief 提供每次启动的原始复位标志快照。
 * @details 在HAL/时钟初始化之前捕获RCC.CSR并清除累计复位标志，之后只读副本。
 * @note 多种标志可以同时置位，不将位图推断为唯一根因；不配置时钟或中断。
 */
#ifndef SBCG_BSP_RESET_H
#define SBCG_BSP_RESET_H
#include <stdbool.h>
#include <stdint.h>
/** @brief CSR原值、七类高位复位标志及清标志后的读回结果；不含跨掉电计数。 */
typedef struct {
    uint32_t raw_csr, reset_flags;
    bool clear_ok;
} BSP_ResetSnapshot;
/**
 * @brief 在本次main首次进入时捕获标志并清除旧累计标志，重复调用无操作。
 * @return 无返回值；清除失败保留原值，交由clear_ok报告。
 * @note 由main的USER CODE1在HAL初始化前调用；不依赖tick、UART或外部内存。
 */
void BSP_Reset_Capture(void);
/**
 * @brief 复制本次启动的只读诊断。
 * @param out 非NULL调用方输出，不保留地址；失败不修改输出。
 * @return 已捕获且参数有效为true；尚未捕获或NULL为false。
 * @note 仅前台使用；不读写硬件或重新清除标志。
 */
bool BSP_Reset_GetSnapshot(BSP_ResetSnapshot *out);
#endif
