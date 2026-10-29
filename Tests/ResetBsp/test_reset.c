/** @file test_reset.c @brief 真实启动快照的原始标志、单次捕获与失败语义测试。 */
#include "BSP/bsp_reset.h"
#include "main.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
TestRcc test_rcc;
unsigned clear_calls;
int clear_fails;
/** @brief 执行独立启动样例。@param argc 3。@param argv 原CSR及清除失败标志。@return 0表示通过。 */
int main(int argc,char **argv)
{
    assert(argc==3);
    const uint32_t original=(uint32_t)strtoul(argv[1],NULL,16);
    clear_fails=atoi(argv[2]);
    BSP_ResetSnapshot result={.raw_csr=123U,.reset_flags=456U,.clear_ok=true};
    assert(!BSP_Reset_GetSnapshot(NULL));
    assert(!BSP_Reset_GetSnapshot(&result)&&result.raw_csr==123U&&result.reset_flags==456U);
    RCC->CSR=original;
    BSP_Reset_Capture();
    assert(BSP_Reset_GetSnapshot(&result));
    assert(result.raw_csr==original&&result.reset_flags==(original&UINT32_C(0xfe000000)));
    assert(result.clear_ok==!clear_fails&&clear_calls==1U);
    assert((RCC->CSR&UINT32_C(0x00ffffff))==(original&UINT32_C(0x00ffffff)));
    if(!clear_fails)assert((RCC->CSR&UINT32_C(0xfe000000))==0U);
    RCC->CSR=UINT32_C(0xffffffff);
    BSP_Reset_Capture();
    assert(clear_calls==1U&&BSP_Reset_GetSnapshot(&result)&&result.raw_csr==original);
    assert(!BSP_Reset_GetSnapshot(NULL));
    printf("PASS reset raw=%08lx clear_failure=%d\n",(unsigned long)original,clear_fails);
    return 0;
}
