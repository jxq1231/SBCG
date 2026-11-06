/**
 * @file        test_flash_wait_budget.c
 * @brief       校验bsp_flash_ram的忙等待预算覆盖128 KiB扇区擦除的最坏情况。
 * @details     轮询循环在ITCM内执行，主机只能校验预算数值：按216 MHz主频与实测循环成本
 *              （-Os约8周期、-O0约25周期）换算成时间，要求擦除≥1.5 s、编程仍在毫秒级。
 * @note        纯常量/算术测试，不访问硬件，也不覆盖真实擦除时序（需上板验证）。
 */
#include "BSP/bsp_flash_ram.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_CORE_CLOCK_HZ UINT64_C(216000000)
#define TEST_ERASE_CYCLES_PER_POLL 8U   /**< -Os下每次轮询约8周期，取最快成本作下界。 */
#define TEST_PROGRAM_CYCLES_PER_POLL 25U /**< -O0下每次轮询约25周期，取最慢成本作上界。 */

/**
 * @brief   把轮询次数按给定单次周期成本换算成毫秒。
 * @param   iterations 轮询上限，单位次。
 * @param   cycles_per_poll 单次轮询占用的内核周期数，单位周期。
 * @return  216 MHz下的等待时间，单位ms。
 * @note    整数运算且只用于预算校验，不参与固件运行。
 */
static uint64_t WaitMs(uint32_t iterations, uint32_t cycles_per_poll)
{
    return (uint64_t)iterations * cycles_per_poll * UINT64_C(1000) / TEST_CORE_CLOCK_HZ;
}

/**
 * @brief   校验擦除预算覆盖1.5 s且编程预算仍为毫秒级。
 * @return  全部断言通过返回0；断言失败由assert中止。
 */
int main(void)
{
    /* 数据手册128 KiB扇区擦除典型约1 s：即使按最快的循环成本，预算也须覆盖1.5 s。 */
    assert(WaitMs(BSP_FLASH_RAM_ERASE_WAIT_LIMIT, TEST_ERASE_CYCLES_PER_POLL) >= UINT64_C(1500));
    /* 修复前的2,000,000次在最快成本下不足0.1 s，必须被排除。 */
    assert(BSP_FLASH_RAM_ERASE_WAIT_LIMIT > UINT32_C(40000000));
    /* 单字编程只需微秒级：预算应覆盖毫秒级尾部，又不应拖到秒级。 */
    assert(WaitMs(BSP_FLASH_RAM_PROGRAM_WAIT_LIMIT, TEST_ERASE_CYCLES_PER_POLL) >= UINT64_C(50));
    assert(WaitMs(BSP_FLASH_RAM_PROGRAM_WAIT_LIMIT, TEST_PROGRAM_CYCLES_PER_POLL) <= UINT64_C(300));
    /* 擦除与编程必须使用不同预算，不能退回同一个常量。 */
    assert(BSP_FLASH_RAM_ERASE_WAIT_LIMIT > BSP_FLASH_RAM_PROGRAM_WAIT_LIMIT * UINT32_C(10));
    printf("PASS BootCounter: erase wait %u iterations, program wait %u iterations\n",
           (unsigned)BSP_FLASH_RAM_ERASE_WAIT_LIMIT,
           (unsigned)BSP_FLASH_RAM_PROGRAM_WAIT_LIMIT);
    return 0;
}
