/**
 * @file        bsp_stack_diag.c
 * @brief       测量MSP使用水位并检查栈底哨兵。
 * @details     启动用无栈指令涂写空闲区，前台检查点直接采样MSP。
 * @note        涂色识别实际写入，采样只覆盖显式检查点。状态只由前台更新。
 */
#include "bsp_stack_diag.h"
#include "main.h"
#include <stddef.h>

#define STACK_PATTERN 0xA55AA55A
#define STACK_GUARD_BYTES 16U
#define STACK_STRINGIFY_INNER(value) #value
#define STACK_STRINGIFY(value) STACK_STRINGIFY_INNER(value)

extern uint32_t _sstack;
extern uint32_t _estack;
static uint32_t painted_top __attribute__((used));
static uint32_t min_msp;
static uint32_t peak_painted_bytes;

/**
 * @brief  在不使用栈的情况下从_sstack涂写到调用者当前MSP。
 * @return 无返回值。
 * @note   暂时屏蔽普通中断，按进入时的PRIMASK恢复。不得加C语句、局部对象或子调用。
 */
__attribute__((naked, noinline))
void BSP_StackDiag_Paint(void)
{
    __asm(
        "mrs r12, primask\n"
        "cpsid i\n"
        "ldr r0, =_sstack\n"
        "mrs r1, msp\n"
        "cmp r0, r1\n"
        "bhs 2f\n"
        "ldr r2, =_estack\n"
        "cmp r1, r2\n"
        "bhi 2f\n"
        "ldr r2, =" STACK_STRINGIFY(STACK_PATTERN) "\n"
        "1: str r2, [r0], #4\n"
        "cmp r0, r1\n"
        "blo 1b\n"
        "ldr r3, =painted_top\n"
        "str r1, [r3]\n"
        "2: msr primask, r12\n"
        "bx lr\n");
}

/** @brief 记录检查点所见的最低MSP，单位为字节地址。@return 无返回值。 */
void BSP_StackDiag_Observe(void)
{
    const uint32_t current = __get_MSP();
    if (min_msp == 0U || current < min_msp)
    {
        min_msp = current;
    }
}

/**
 * @brief  从低地址扫描涂色，保留历次观察到的最大使用量。
 * @param  out 调用方输出，允许NULL并返回失败。
 * @return true表示有效快照。
 * @note   哨兵覆盖最低16字节，读取期间不禁止中断，下一次采样可见之后的栈写入。
 */
bool BSP_StackDiag_Read(BSP_StackDiagSnapshot *out)
{
    const uintptr_t bottom = (uintptr_t)&_sstack;
    const uintptr_t top = (uintptr_t)&_estack;
    if (out == NULL || painted_top < bottom + STACK_GUARD_BYTES || painted_top > top)
    {
        return false;
    }

    uintptr_t first_written = bottom;
    while (first_written < painted_top &&
           *(volatile const uint32_t *)first_written == STACK_PATTERN)
    {
        first_written += sizeof(uint32_t);
    }
    const uint32_t used_bytes = (uint32_t)(top - first_written);
    if (used_bytes > peak_painted_bytes)
    {
        peak_painted_bytes = used_bytes;
    }

    BSP_StackDiag_Observe();
    *out = (BSP_StackDiagSnapshot){
        .reserve_bytes = (uint32_t)(top - bottom),
        .painted_used_bytes = peak_painted_bytes,
        .min_msp = min_msp,
        .limit = (uint32_t)bottom,
        .guard_ok = peak_painted_bytes <= top - bottom - STACK_GUARD_BYTES &&
                    min_msp >= bottom + STACK_GUARD_BYTES,
    };
    return true;
}
