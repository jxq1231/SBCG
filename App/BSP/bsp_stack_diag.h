/**
 * @file        bsp_stack_diag.h
 * @brief       提供诊断镜像的MSP涂色、采样及快照接口。
 * @details     分开记录写入水位与检查点读到的最低MSP。
 * @note        仅诊断构建启用，链接器划定栈范围。
 */
#ifndef SBCG_BSP_STACK_DIAG_H
#define SBCG_BSP_STACK_DIAG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t reserve_bytes;
    uint32_t painted_used_bytes;
    uint32_t min_msp;
    uint32_t limit;
    bool guard_ok;
} BSP_StackDiagSnapshot;

/** @brief 启动时涂写未使用的MSP区。@return 无返回值。@note 在main用户区且HAL初始化前调用一次。 */
void BSP_StackDiag_Paint(void);

/** @brief 在当前前台检查点记录MSP。@return 无返回值。 */
void BSP_StackDiag_Observe(void);

/**
 * @brief 扫描涂色水位并复制累计状态。
 * @param out 非NULL输出，仅成功时写入。
 * @return 涂色范围有效并完成读取为true。
 * @note 只在前台低频调用，允许中断继续使用MSP。
 */
bool BSP_StackDiag_Read(BSP_StackDiagSnapshot *out);

#endif
