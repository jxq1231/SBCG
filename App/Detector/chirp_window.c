/**
 * @file        chirp_window.c
 * @brief       实现只读模板统计、单窗口去直流相关及独立失败诊断。
 * @details     每窗只做一趟点积；可选保留门限前相关，原匹配输出规则保持一致。
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
 * @brief 单趟统计窗口，分别提供原匹配输出和可选门限前诊断。
 * @param r 非NULL且已初始化的只读模板统计。
 * @param samples 非NULL已验证CPU副本，至少r.frames点，仅调用期间使用。
 * @param session 非零采集会话。
 * @param first 窗口绝对起点，末帧不得溢出。
 * @param config 非NULL只读门限配置；只使用RMS与相关度字段。
 * @param out 非NULL独占输出，失败清零；confirmed_frame固定0，不是已确认事件。
 * @param diag 可NULL独占输出，参数非法时独立清零并标记INVALID；不保存输入指针。
 * @return true为窗口过门限；false为参数无效或未匹配。
 * @note 两输出不与输入或彼此重叠；仅前台使用已验证CPU副本，不访问DMA。
 *       最多2400项点积，低RMS仅在需要诊断时继续开方，不增加第二趟点积。
 */
bool ChirpWindow_ScoreWithDiagnostic(const ChirpWindowTemplate *r,const int16_t *samples,
    uint32_t session,uint64_t first,const ChirpMatcherConfig *config,ChirpMatch *out,
    ChirpWindowDiagnostic *diag)
{
    if(diag!=NULL)*diag=(ChirpWindowDiagnostic){0};
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
    if(variance==0U){
        if(diag!=NULL){diag->status=CHIRP_WINDOW_ZERO_VARIANCE;diag->clipped=clipped;}
        return false;
    }
    const bool low_rms=variance<minimum*minimum*n*n;
    if(low_rms && diag==NULL)return false;
    const int64_t numerator=(int64_t)n*dot-sum*r->sum;
    double score=fabs((double)numerator)/sqrt((double)variance*(double)r->variance);
    if(score>1.0)score=1.0;
    if(diag!=NULL){
        *diag=(ChirpWindowDiagnostic){
            low_rms ? CHIRP_WINDOW_LOW_RMS:
                score<(double)config->min_correlation ? CHIRP_WINDOW_LOW_CORR:CHIRP_WINDOW_MATCH,
            score,true,numerator<0,clipped,(uint16_t)sqrt((double)variance/(double)(n*n))};
    }
    if(low_rms || score<(double)config->min_correlation)return false;
    *out=(ChirpMatch){session,first,first+n-1U,0U,(float)score,
        diag!=NULL ? diag->rms_pcm:(uint16_t)sqrt((double)variance/(double)(n*n)),numerator<0,clipped};
    return true;
}

/**
 * @brief 通过同一评分实现保留原接口的匹配与失败清零语义。
 * @param r 非NULL已初始化只读模板统计，样本使用期不变。
 * @param samples 非NULL完整CPU窗口，至少r.frames点，仅本次调用借用。
 * @param session 非零采集会话。
 * @param first 绝对窗口起点，末帧不得溢出uint64。
 * @param config 非NULL有效门限，仅本次调用借用。
 * @param out 非NULL独占输出，不与输入重叠；失败清零，confirmed_frame固定0。
 * @return true表示通过原门限；false表示无匹配或非法参数。
 * @note 不请求诊断，低RMS仍在开方前返回；不发布事件或访问硬件。
 */
bool ChirpWindow_Score(const ChirpWindowTemplate *r,const int16_t *samples,
    uint32_t session,uint64_t first,const ChirpMatcherConfig *config,ChirpMatch *out)
{
    return ChirpWindow_ScoreWithDiagnostic(r,samples,session,first,config,out,NULL);
}
