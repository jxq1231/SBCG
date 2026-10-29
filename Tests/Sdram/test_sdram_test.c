/** @file test_sdram_test.c @brief 真实内存测试核心的全范围、边界与故障注入；仅主机运行。 */
#include "BSP/sdram_test.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/** @brief 有限步推进到PASS。@param t 有效测试状态。@param tick 初始毫秒。@return 无返回值。 */
static void finish(SDRAM_Test *t,uint32_t tick)
{
    for(unsigned i=0;i<200000U && t->phase!=SDRAM_TEST_PASS && t->phase!=SDRAM_TEST_FAIL;i++) {
        SDRAM_TestStep(t,tick); tick+=101U;
    }
    assert(t->phase==SDRAM_TEST_PASS);
}
/** @brief 检查内存覆盖、分片、保持等待及错误停止。@return 0通过，否则断言失败。 */
int main(void)
{
    uint32_t buffer[SDRAM_TEST_SLICE_WORDS*2U+7U]; SDRAM_Test t;
    memset(buffer,0xCC,sizeof buffer);
    assert(SDRAM_TestInit(&t,&buffer[1],sizeof buffer-8U));
    assert(buffer[1]==UINT32_C(0xCCCCCCCC)); /* Init must not access RAM. */
    SDRAM_TestStep(&t,0U);assert(t.phase==SDRAM_TEST_FILL);
    SDRAM_TestStep(&t,1U);assert(t.cursor==SDRAM_TEST_SLICE_WORDS);
    assert(buffer[SDRAM_TEST_SLICE_WORDS+1U]==UINT32_C(0xCCCCCCCC));
    while(t.phase==SDRAM_TEST_FILL)SDRAM_TestStep(&t,2U);
    assert(t.phase==SDRAM_TEST_HOLD);SDRAM_TestStep(&t,101U);assert(t.phase==SDRAM_TEST_HOLD);
    SDRAM_TestStep(&t,102U);assert(t.phase==SDRAM_TEST_VERIFY);
    finish(&t,103U);
    assert(t.verified_words==t.words*SDRAM_TEST_PASSES);
    assert(buffer[0]==UINT32_C(0xCCCCCCCC));assert(buffer[sizeof buffer/4U-1U]==UINT32_C(0xCCCCCCCC));
    buffer[1]=0x123U; SDRAM_TestStep(&t,100000U);assert(buffer[1]==0x123U&&t.phase==SDRAM_TEST_PASS);
    assert(!SDRAM_TestInit(NULL,buffer,sizeof buffer));
    assert(!SDRAM_TestInit(&t,NULL,sizeof buffer));
    assert(!SDRAM_TestInit(&t,(char*)buffer+1,sizeof buffer-4U));
    assert(!SDRAM_TestInit(&t,buffer,7U));assert(!SDRAM_TestInit(&t,buffer,10U));
    assert(!SDRAM_TestInit(&t,buffer,UINT32_MAX));
    for(unsigned pass=0;pass<SDRAM_TEST_PASSES;pass++) {
        assert(SDRAM_TestInit(&t,buffer,sizeof buffer));
        unsigned guard=0;uint32_t tick=0;
        while(!(t.phase==SDRAM_TEST_VERIFY&&t.pass==pass)) {
            SDRAM_TestStep(&t,tick);tick+=101;assert(++guard<1000);
        }
        buffer[17]^=UINT32_C(0x01000100);
        SDRAM_TestStep(&t,tick);assert(t.phase==SDRAM_TEST_FAIL);
        assert(t.error_offset_bytes==17U*4U);assert(t.expected!=t.actual);
        SDRAM_Test save=t;uint32_t first=buffer[0];SDRAM_TestStep(&t,tick+1);
        assert(memcmp(&t,&save,sizeof t)==0&&buffer[0]==first);
    }
    assert(SDRAM_TestInit(&t,buffer,8));SDRAM_TestStep(&t,UINT32_MAX-10U);
    SDRAM_TestStep(&t,UINT32_MAX-10U);assert(t.phase==SDRAM_TEST_HOLD);
    SDRAM_TestStep(&t,50U);assert(t.phase==SDRAM_TEST_HOLD);
    SDRAM_TestStep(&t,90U);assert(t.phase==SDRAM_TEST_VERIFY);finish(&t,100U);
    /* A simulated high-address alias/corruption must not be hidden by constant patterns. */
    assert(SDRAM_TestInit(&t,buffer,sizeof buffer));
    uint32_t tick=0;while(!(t.phase==SDRAM_TEST_VERIFY&&t.pass==2U)){SDRAM_TestStep(&t,tick);tick+=101;}
    buffer[129]=buffer[1];while(t.phase==SDRAM_TEST_VERIFY)SDRAM_TestStep(&t,tick);
    assert(t.phase==SDRAM_TEST_FAIL&&t.error_offset_bytes==129U*4U);
    uint32_t *large=malloc(8388608U+8U);assert(large);large[0]=0x12345678;large[2097153]=0xAABBCCDD;
    assert(SDRAM_TestInit(&t,large+1,8388608U));finish(&t,0U);
    assert(t.verified_words==8388608U);assert(large[0]==0x12345678&&large[2097153]==0xAABBCCDD);free(large);
    puts("PASS SDRAM test: slices, 4 patterns/full 8MB, corruption, alias, retention, wrap, terminal, canaries");
    return 0;
}
