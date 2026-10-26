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
#include <string.h>

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
    /* 栅格无关回溯估计：静默16帧后斜坡，峰在窗内，前沿在样本28(3900)与29(4200)之间。 */
    {
        int16_t back[256];
        for(unsigned i=0U;i<16U;++i)back[i]=0;
        for(unsigned i=16U;i<256U;++i){
            const long value=(long)(i-15)*300L;
            back[i]=(int16_t)(value>32767L?32767L:value);
        }
        fraction=0.0;
        CHECK(ToaEstimator_EstimateBackward(&ToaBackward_DefaultConfig,back,&fraction));
        CHECK(fabs(fraction-28.65)<0.1);
    }
    /* 能量包络起跳：三节递增斜坡（斜率100/200/600，尾部截顶32767），幅值峰在尾部，
       但能量段起点回溯到首节，跨阈插值固定在样本44(4000)与45(4200)之间。 */
    {
        int16_t back[256];memset(back,0,sizeof back);
        for(unsigned i=10U;i<40U;++i)back[i]=(int16_t)((long)(i-9)*100L);         /* 100..3000 */
        for(unsigned i=40U;i<80U;++i)back[i]=(int16_t)((long)(i-39)*200L+3000L);  /* 3200..11200 */
        for(unsigned i=80U;i<256U;++i){const long v=(long)(i-79)*600L+11200L;
            back[i]=(int16_t)(v>32767L?32767L:v);}                                /* 11800..截顶 */
        fraction=0.0;
        CHECK(ToaEstimator_EstimateBackward(&ToaBackward_DefaultConfig,back,&fraction));
        CHECK(fabs(fraction-44.475)<0.01);
    }
    /* 能量包络对软起振正弦突发：起跳点定位到首段包络，插值落在样本15与16之间。 */
    {
        const double pi=3.141592653589793;
        int16_t osc[256];memset(osc,0,sizeof osc);
        for(unsigned i=16U;i<256U;++i){
            const double phase=2.0*pi*2000.0*(double)i/48000.0;
            osc[i]=(int16_t)(14000.0*fabs(sin(phase))+0.5);
        }
        fraction=-1.0;
        CHECK(ToaEstimator_EstimateBackward(&ToaBackward_DefaultConfig,osc,&fraction));
        CHECK(fraction>10.0 && fraction<30.0);
    }
    /* 单点尖峰不足连续过阈段 -> 拒绝 */
    {
        int16_t back[256];memset(back,0,sizeof back);back[50]=30000;
        fraction=99.0;
        CHECK(!ToaEstimator_EstimateBackward(&ToaBackward_DefaultConfig,back,&fraction));
        CHECK(fraction==99.0);
    }
    /* 两处相隔若干帧的孤立尖峰：左段起跳后不得把它们算作连续过阈段 -> 拒绝 */
    {
        int16_t back[256];memset(back,0,sizeof back);
        back[50]=20000;back[100]=30000;
        fraction=77.0;
        CHECK(!ToaEstimator_EstimateBackward(&ToaBackward_DefaultConfig,back,&fraction));
        CHECK(fraction==77.0);
    }
    /* 整窗已过阈（前沿在窗外）-> 拒绝 */
    int16_t full[256];
    for(unsigned i=0U;i<256U;++i)full[i]=10000;
    CHECK(!ToaEstimator_EstimateBackward(&ToaBackward_DefaultConfig,full,&fraction));
    /* 参数非法全部拒绝 */
    CHECK(!ToaEstimator_EstimateBackward(NULL,full,&fraction));
    CHECK(!ToaEstimator_EstimateBackward(&(ToaBackwardConfig){16U,128U,8U,4U},full,&fraction));
    CHECK(!ToaEstimator_EstimateBackward(&(ToaBackwardConfig){256U,128U,8U,9U},full,&fraction));
    printf("PASS toa estimator: integer/fractional leading edges, adaptive threshold, boundaries, grid-free backward\n");
    return 0;
}