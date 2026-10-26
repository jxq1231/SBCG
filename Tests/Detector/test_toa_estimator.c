/**
 * @file        test_toa_estimator.c
 * @brief       验证到达时刻前沿子帧估算的数学与边界。
 * @details     用已知整数/分数前沿的合成波形核对插值位置，覆盖参数非法、恒阈值、
 *              首样本过阈、无过阈与相对阈值自适应。
 * @note        纯主机C11，无硬件访问；不依赖HAL/日志。
 */
#include "Detector/toa_estimator.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** @brief 条件求值一次，失败打印行号并结束；测试专用。 */
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)

/** @brief 构造从0开始以每样本step线性上升（带底噪0）的窗，peak为窗内最大值。 */
static void Fill(int16_t *samples,uint16_t frames,int32_t step)
{
    for(uint16_t i=0U;i<frames;++i){
        const int64_t value=(int64_t)(i+1)*step;
        samples[i]=(int16_t)(value>32767L ? 32767:value);
    }
}

int main(void)
{
    int16_t samples[64];
    double fraction=123.0;
    /* 强斜率上升沿：前沿过阈位置接近线性的解析值。 */
    Fill(samples,32,2000); /* 样本i幅值≈2000*(i+1)，窗口峰值32768会截顶，但前沿在i=1附近 */
    CHECK(!ToaEstimator_Estimate(NULL,&samples[0],&fraction));
    CHECK(ToaEstimator_Estimate(&ToaEstimator_DefaultConfig,samples,&fraction));
    CHECK(fraction >= 0.0 && fraction < 8.0);
    /* 精确分数前沿：阈值128、斜率128/样本时，前沿恰好在样本1与2之间0.0处附近。 */
    {
        ToaEstimatorConfig cfg=ToaEstimator_DefaultConfig;cfg.threshold_pcm=128U;cfg.relative_scale=0U;
        for(uint16_t i=0U;i<32U;++i)samples[i]=(int16_t)((int32_t)(i+1)*64); /* 64,128,192,... */
        CHECK(ToaEstimator_Estimate(&cfg,samples,&fraction));
        /* 前沿在样本1(64)与2(128)之间：(128-64)/(128-64)=1.0 -> 位置≈1.0 */
        CHECK(fabs(fraction-1.0)<0.05);
    }
    /* 非整数前沿：斜率80/样本、阈值128，跨越点样本0(80)->1(160)，系数(128-80)/80=0.6 -> 0.6 */
    {
        ToaEstimatorConfig cfg=ToaEstimator_DefaultConfig;cfg.threshold_pcm=128U;cfg.relative_scale=0U;
        for(uint16_t i=0U;i<32U;++i)samples[i]=(int16_t)((int32_t)(i+1)*80);
        CHECK(ToaEstimator_Estimate(&cfg,samples,&fraction));
        CHECK(fabs(fraction-0.6)<0.05);
    }
    /* 相对阈值自适应：主阈值过大时仍可检出（峰值/4=4000>128） */
    {
        ToaEstimatorConfig cfg=ToaEstimator_DefaultConfig;cfg.threshold_pcm=30000U;cfg.relative_scale=4U;
        Fill(samples,32,2000);
        CHECK(ToaEstimator_Estimate(&cfg,samples,&fraction));
        CHECK(fraction>=0.0 && fraction<20.0);
    }
    /* 首样本已过阈且窗内无下穿再上穿 -> 前沿在窗外，拒绝 */
    for(uint16_t i=0U;i<32U;++i)samples[i]=10000;
    samples[0]=32767;
    CHECK(!ToaEstimator_Estimate(&ToaEstimator_DefaultConfig,samples,&fraction));
    /* 全零 -> 无峰值，拒绝 */
    for(uint16_t i=0U;i<32U;++i)samples[i]=0;
    CHECK(!ToaEstimator_Estimate(&ToaEstimator_DefaultConfig,samples,&fraction));
    /* 恒阈值（不动）且无过阈 -> 拒绝 */
    for(uint16_t i=0U;i<32U;++i)samples[i]=100;
    CHECK(!ToaEstimator_Estimate(&ToaEstimator_DefaultConfig,samples,&fraction));
    /* 参数非法一律拒绝且不改输出 */
    fraction=55.0;
    CHECK(!ToaEstimator_Estimate(NULL,samples,&fraction));
    CHECK(!ToaEstimator_Estimate(&(ToaEstimatorConfig){7U,128U,4U},samples,&fraction));
    CHECK(!ToaEstimator_Estimate(&(ToaEstimatorConfig){32U,0U,0U},samples,&fraction));
    CHECK(fraction==55.0);
    printf("PASS toa estimator: integer/fractional leading edges, adaptive threshold, boundaries\n");
    return 0;
}