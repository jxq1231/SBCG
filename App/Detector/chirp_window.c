/**
 * @file        chirp_window.c
 * @brief       实现只读模板的统计预计算和单窗口去直流归一化相关。
 * @details     每次调用只计算一个窗口，工作量由上层候选搜索显式限制。
 * @note        CPU只读调用方已验证副本，不维护DMA所有权或访问硬件。
 */
#include "chirp_window.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief 校验并借用模板，缓存整数统计量。
 * @param out 非NULL独占输出，失败清零，不与模板重叠。
 * @param samples 非NULL只读模板，调用方保证整个使用期不变。
 * @param frames 单声道点数16..2400。
 * @return true表示非零方差有效模板；false表示拒绝。
 * @note 统计最大不超过2400²*32768²，64位计算不溢出。
 */
bool ChirpWindow_Init(ChirpWindowTemplate *out,const int16_t *samples,uint16_t frames)
{
    if(out==NULL)return false;
    *out=(ChirpWindowTemplate){0};
    if(samples==NULL || frames<16U || frames>CHIRP_MATCHER_MAX_FRAMES)return false;
    int64_t sum=0;uint64_t squares=0U;
    for(uint16_t i=0U;i<frames;++i){const int64_t x=samples[i];sum+=x;squares+=(uint64_t)(x*x);}
    const uint64_t variance=(uint64_t)frames*squares-(uint64_t)(sum*sum);
    if(variance==0U)return false;
    *out=(ChirpWindowTemplate){samples,sum,variance,frames};return true;
}

/**
 * @brief 对单个连续窗口计算相关，过门限时返回窗口元数据。
 * @param r 非NULL且已初始化的只读模板统计。
 * @param samples 非NULL已验证CPU副本，至少r.frames点，仅调用期间使用。
 * @param session 非零采集会话。
 * @param first 窗口绝对起点，末帧不得溢出。
 * @param config 非NULL只读门限配置；只使用RMS与相关度字段。
 * @param out 非NULL不重叠输出，失败清零；confirmed_frame为0，不是已确认事件。
 * @return true为窗口过门限；false为参数无效或未匹配。
 * @note 最多2400项直接点积；调用方控制每轮调用次数，不能在IRQ或未经验证DMA上调用。
 */
bool ChirpWindow_Score(const ChirpWindowTemplate *r,const int16_t *samples,
    uint32_t session,uint64_t first,const ChirpMatcherConfig *config,ChirpMatch *out)
{
    if(out==NULL)return false;
    *out=(ChirpMatch){0};
    if(r==NULL || r->samples==NULL || r->frames<16U || r->frames>CHIRP_MATCHER_MAX_FRAMES ||
       r->variance==0U || samples==NULL || session==0U || config==NULL ||
       config->min_rms_pcm>32768U || !isfinite(config->min_correlation) ||
       config->min_correlation<=0.0f || config->min_correlation>1.0f ||
       first>UINT64_MAX-(r->frames-1U))return false;
    int64_t sum=0,dot=0;uint64_t squares=0U;bool clipped=false;
    for(uint16_t i=0U;i<r->frames;++i){
        /* 两个PCM16的乘积和平方均<=2^30，先做32位乘法再扩展到64位累计。 */
        const int32_t x=samples[i];sum+=x;squares+=(uint32_t)(x*x);
        dot+=(int64_t)(x*(int32_t)r->samples[i]);clipped=clipped || x==INT16_MIN || x==INT16_MAX;
    }
    const uint64_t n=r->frames,minimum=config->min_rms_pcm;
    const uint64_t variance=n*squares-(uint64_t)(sum*sum);
    if(variance==0U || variance<minimum*minimum*n*n)return false;
    const int64_t numerator=(int64_t)n*dot-sum*r->sum;
    double score=fabs((double)numerator)/sqrt((double)variance*(double)r->variance);
    if(score>1.0)score=1.0;
    if(score<(double)config->min_correlation)return false;
    *out=(ChirpMatch){session,first,first+n-1U,0U,(float)score,
        (uint16_t)sqrt((double)variance/(double)(n*n)),numerator<0,clipped};
    return true;
}
