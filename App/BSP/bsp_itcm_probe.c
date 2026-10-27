/**
 * @file        bsp_itcm_probe.c
 * @brief       CPU-only ITCM数据访问探针实现。
 * @details     数组放在用户链接段.ItcmProbe（0x00000000，16KB）；写四种模式并逐字回读。
 * @note        诊断镜像专用；不依赖BSS清零，探针首次运行自行写入全部字。
 */
#include "BSP/bsp_itcm_probe.h"
#include <stddef.h>

#define ITCM_PROBE_WORDS 64U
#define ITCM_PROBE_BYTES (ITCM_PROBE_WORDS * 4U)

/** @brief 放在ITCM的探测数组；volatile防止编译器把回读优化掉。 */
static volatile uint32_t itcm_probe_words[ITCM_PROBE_WORDS]
    __attribute__((section(".ItcmProbe"), aligned(32)));

static BSP_ITCMProbeSnapshot probe_state;

/** @brief 简单32位CRC，仅用于日志指纹，不作安全校验。 */
static uint32_t Crc32Step(uint32_t crc,uint32_t value)
{
    crc ^= value;
    for(uint32_t bit=0U;bit<32U;++bit)
    { crc = (crc & 0x80000000U) ? ((crc << 1U) ^ 0x04C11DB7U) : (crc << 1U); }
    return crc;
}

/** @brief 按模式写满数组并逐字回读；返回错误数。 */
static uint32_t WriteRead(uint32_t seed)
{
    uint32_t errors=0U;
    for(uint32_t i=0U;i<ITCM_PROBE_WORDS;++i)
    { itcm_probe_words[i]=seed+(i*0x01010101U); }
    for(uint32_t i=0U;i<ITCM_PROBE_WORDS;++i)
    {
        const uint32_t expected=seed+(i*0x01010101U);
        if(itcm_probe_words[i]!=expected)++errors;
    }
    return errors;
}

bool BSP_ITCM_ProbeRun(void)
{
    probe_state=(BSP_ITCMProbeSnapshot){0};
    probe_state.address=(uint32_t)(uintptr_t)&itcm_probe_words[0];
    probe_state.bytes=ITCM_PROBE_BYTES;
    uint32_t crc=0xFFFFFFFFU;
    const uint32_t patterns[]={0x00000000U,0xFFFFFFFFU,0x55555555U,0xAAAAAAAAU};
    for(uint32_t p=0U;p<sizeof patterns/sizeof patterns[0];++p)
    {
        const uint32_t errors=WriteRead(patterns[p]);
        probe_state.errors+=errors;
        crc=Crc32Step(crc,patterns[p]);
    }
    for(uint32_t i=0U;i<ITCM_PROBE_WORDS;++i)
    { crc=Crc32Step(crc,itcm_probe_words[i]); }
    probe_state.crc=crc;
    probe_state.ran=true;
    probe_state.passed=probe_state.errors==0U;
    return probe_state.passed;
}

void BSP_ITCM_ProbeGetSnapshot(BSP_ITCMProbeSnapshot *out)
{
    if(out!=NULL)*out=probe_state;
}
