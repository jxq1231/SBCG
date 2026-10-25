/**
 * @file bsp_reset.c
 * @brief 捕获本次启动的RCC复位状态并建立下一次复位的干净观察边界。
 * @details 保留完整CSR与七类标志；使用HAL的RMVF宏清状态后读回验证，不写其它RCC配置。
 * @note main的USER CODE1只调用一次，不依赖HAL时基、IRQ、DMA、缓存或外部内存。
 */
#include "bsp_reset.h"
#include "main.h"
#include <stddef.h>

#define RESET_FLAGS_MASK (RCC_CSR_BORRSTF | RCC_CSR_PINRSTF | RCC_CSR_PORRSTF | \
                         RCC_CSR_SFTRSTF | RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF | RCC_CSR_LPWRRSTF)
_Static_assert(RESET_FLAGS_MASK == UINT32_C(0xfe000000), "STM32F746 reset flag layout required");
static BSP_ResetSnapshot snapshot;
static bool captured;

/** @brief 按接口约定先保存原始标志，再清除并验证；重复调用不破坏既有证据。@return 无返回值。 */
void BSP_Reset_Capture(void)
{
    if (captured) { return; }
    snapshot.raw_csr = RCC->CSR;
    snapshot.reset_flags = snapshot.raw_csr & RESET_FLAGS_MASK;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    snapshot.clear_ok = (RCC->CSR & RESET_FLAGS_MASK) == 0U;
    captured = true;
}

/**
 * @brief 将启动副本交给前台日志，不再次读取或清除硬件状态。
 * @param out 非NULL输出，调用方持有；失败保持原内容。
 * @return 已捕获且输出有效时true，否则false。
 */
bool BSP_Reset_GetSnapshot(BSP_ResetSnapshot *out)
{
    if (!captured || out == NULL) { return false; }
    *out = snapshot;
    return true;
}
