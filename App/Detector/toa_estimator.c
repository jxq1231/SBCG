/**
 * @file        toa_estimator.c
 * @brief       声事件到达时刻的子帧前沿过阈估算实现。
 * @details     先取窗内绝对幅值峰值，按配置计算实际阈值（绝对与相对峰值取大），
 *              再扫描首个自下而上的过阈样本对，线性插值出子帧偏移；无过阈则失败。
 * @note        幅值线性插值在强斜率上升沿上引入的偏差 << 1帧，足以支撑±100μs的
 *              重复播放抖动验收口径（48kHz下1帧≈20.8μs）；窗长与阈值由调用方给定。
 */
#include "toa_estimator.h"
#include <stddef.h>

#define TOA_MIN_FRAMES 8U
#define TOA_MAX_FRAMES 512U
#define TOA_MAX_PCM 32768U

const ToaEstimatorConfig ToaEstimator_DefaultConfig={32U,128U,4U};
const ToaBackwardConfig ToaBackward_DefaultConfig={256U,128U,8U,4U};

/**
 * @brief   估算候选窗前沿的过阈位置（相对samples[0]的子帧偏移）。
 * @param   config 非NULL有效配置，只读；window_frames 8..512。
 * @param   samples 非NULL连续只读PCM，至少window_frames个点，仅本调用借用。
 * @param   out_fraction 非NULL输出；成功为前沿插值位置，失败保持原值。
 * @return  true为找到前沿；false为参数非法、无过阈点或首个样本已过阈。
 * @note    实际阈值=max(threshold_pcm, peak_abs/relative_scale)，relative_scale为0时禁用相对项；
 *          过阈判定用绝对幅值，插值在|x[i-1]|<阈<=|x[i]|的相邻对上线性完成。
 */
bool ToaEstimator_Estimate(const ToaEstimatorConfig *config,
                           const int16_t *samples, double *out_fraction)
{
    if(config==NULL || out_fraction==NULL || samples==NULL ||
       config->window_frames<TOA_MIN_FRAMES || config->window_frames>TOA_MAX_FRAMES ||
       config->threshold_pcm>TOA_MAX_PCM){return false;}
    uint32_t peak=0U;
    for(uint16_t i=0U;i<config->window_frames;++i){
        const int32_t sample=samples[i];
        const uint32_t magnitude=(uint32_t)(sample<0 ? -sample:sample);
        if(magnitude>peak){peak=magnitude;}
    }
    if(peak==0U){return false;}
    uint32_t limit=config->threshold_pcm;
    if(config->relative_scale>0U && config->relative_scale<=16U){
        const uint32_t relative=peak/config->relative_scale;
        if(relative>limit){limit=relative;}
    }
    if(limit==0U){return false;}
    for(uint16_t i=1U;i<config->window_frames;++i){
        const uint32_t before=(uint32_t)(samples[i-1]<0 ? -(int32_t)samples[i-1]:samples[i-1]);
        if(before>=limit){continue;} /* 前沿必须自下而上跨阈，首个样本过阈视为窗外 */
        const uint32_t after=(uint32_t)(samples[i]<0 ? -(int32_t)samples[i]:samples[i]);
        if(after<limit){continue;}
        const double a=(double)before;
        const double b=(double)after;
        /* 线性插值，b>a由上述条件保证且limit在(a,b]内。 */
        const double fraction=(double)(i-1)+
            (b>a ? (double)(limit-a)/(b-a) : 0.0);
        *out_fraction=fraction;
        return true;
    }
    return false;
}
/** @brief 计算samples前frames点中i处的5点短窗平方能量，越界样本按0计，单位(PCM值)^2。 */
static uint64_t ToaEnergyAt(const int16_t *samples,uint16_t frames,int32_t i)
{
    uint64_t energy=0U;
    for(int32_t j=i-2;j<=i+2;++j){
        if(j<0 || j>=(int32_t)frames){continue;}
        const int32_t v=samples[j];
        energy+=(uint64_t)(v<0 ? -v:v)*(uint64_t)(v<0 ? -v:v);
    }
    return energy;
}
/**
 * @brief   能量包络起跳定位直达前沿，栅格无关。
 * @param   config 非NULL有效配置，只读；window_frames 32..512，run_samples 2..8。
 * @param   samples 非NULL连续只读PCM，至少window_frames个点，仅本调用借用。
 * @param   out_fraction 非NULL输出；成功为前沿插值位置，失败保持原值。
 * @return  true为找到连续前沿；false为参数非法、无峰值、整窗已过阈或连续段不足。
 * @note    幅值阈值=max(threshold_pcm, peak/relative_scale)，peak为窗内绝对幅值峰。
 *          定位分三步：1)按5点短窗平方能量求窗内能量最大值Emax；2)取窗内最左的
 *          >=Emax/8能量段起点作为突发粗起点（直达波优先，回声段整体在此阈值下与
 *          直达段连通或位于其后；旧实现沿能量峰向左只找包含段的起点，直达与更强
 *          回声间能量凹低于Emax/8时会把锚点落到回声段，跨轮回放产生±1.9ms跳动）；
 *          3)自粗起点向左找最后一个低于幅值阈值的样本，再向右取首个真实跨阈样本对
 *          线性插值出子帧前沿。输出相对samples[0]的位置，与门控候选窗的64帧栅格无关。
 */
bool ToaEstimator_EstimateBackward(const ToaBackwardConfig *config,
                                   const int16_t *samples, double *out_fraction)
{
    if(config==NULL || out_fraction==NULL || samples==NULL ||
       config->window_frames<32U || config->window_frames>TOA_MAX_FRAMES ||
       config->threshold_pcm>TOA_MAX_PCM ||
       config->run_samples<2U || config->run_samples>8U){return false;}
    uint32_t peak=0U;
    for(uint16_t i=0U;i<config->window_frames;++i){
        const uint32_t magnitude=(uint32_t)(samples[i]<0 ? -(int32_t)samples[i]:samples[i]);
        if(magnitude>peak){peak=magnitude;}
    }
    if(peak==0U){return false;}
    uint32_t limit=config->threshold_pcm;
    if(config->relative_scale>0U && config->relative_scale<=32U){
        const uint32_t relative=peak/config->relative_scale;
        if(relative>limit){limit=relative;}
    }
    if(limit==0U){return false;}
    const uint16_t frames=config->window_frames;
    uint64_t emax=0U;
    for(uint16_t i=0U;i<frames;++i){
        const uint64_t energy=ToaEnergyAt(samples,frames,(int32_t)i);
        if(energy>emax){emax=energy;}
    }
    if(emax==0U){return false;}
    const uint64_t elimit=emax>>3U;  /* Emax/8：低门槛让直达+多径回波连成整段 */
    uint16_t seg_start=0U;
    for(uint16_t i=0U;i<frames;++i){
        if(ToaEnergyAt(samples,frames,(int32_t)i)>=elimit){seg_start=i;break;}
    }
    /* 旧实现只沿能量最大位置向左找包含段的起点；当直达波与更强回声之间存在低于
       Emax/8 的能量凹时会把锚点落在回声段，跨轮回调相位使段起点±1.9ms 跳动。
       改为取窗内最左的 >=Emax/8 段起点：直接锚定首个能量突起（直达波），稳定。 */
    int32_t below=-1;
    for(int32_t i=(int32_t)seg_start;i>=0;--i){
        const uint32_t magnitude=(uint32_t)(samples[i]<0 ? -(int32_t)samples[i]:samples[i]);
        if(magnitude<limit){below=i;break;}
    }
    if(below<0){return false;}  /* 段起点到窗首整段已过阈，前沿在窗外 */
    int32_t cross=(int32_t)below+1;
    while((int32_t)cross<frames)
    {
        const uint32_t magnitude=(uint32_t)(samples[cross]<0 ? -(int32_t)samples[cross]:samples[cross]);
        if(magnitude>=limit){break;}
        ++cross;
    }
    if((int32_t)cross>=frames){return false;}  /* 起跳段内无可跨阈样本 */
    /* run_samples 校验：跨阈点之后必须存在连续 run_samples 个过阈样本。
       早期实现用 "idx_peak-start+1" 计数，最左段起跳后该跨度可从回波峰值跨到
       窗尾，两个相隔数十帧的孤立尖峰会被误判为满足连续过阈；改为向前数真正
       连续过阈样本，孤立瞬态在此处即失败。 */
    uint16_t run=0U;
    for(int32_t i=(int32_t)cross;i<(int32_t)frames;++i){
        const uint32_t magnitude=(uint32_t)(samples[i]<0 ? -(int32_t)samples[i]:samples[i]);
        if(magnitude<limit){break;}
        ++run;
        if(run>=config->run_samples){break;}
    }
    if(run<config->run_samples){return false;}
    const double hi=(double)(samples[cross]<0 ? -(int32_t)samples[cross]:samples[cross]);
    const double lo=(double)(samples[cross-1]<0 ? -(int32_t)samples[cross-1]:samples[cross-1]);
    if(hi<=lo){return false;}
    const double fraction=(double)(cross-1)+(double)((uint32_t)limit-lo)/(hi-lo);
    *out_fraction=fraction;
    return true;
}

/**
 * @brief   枚举同一窗内前4个有效上升沿候选。
 * @param   config 非NULL有效配置，window_frames 32..512、run_samples 2..8。
 * @param   samples 非NULL连续只读PCM，至少window_frames个点，仅本次借用。
 * @param   out 非NULL输出；成功时count为1..4，失败时count=0。
 * @return  true表示至少找到一个候选；false为参数非法或无有效前沿。
 * @note    复用ToaEstimator_EstimateBackward的峰值/阈值/能量段/连续性规则；
 *          第1个候选与单值接口完全一致，后续候选只追加不改变默认路径。
 */
bool ToaEstimator_EstimateCandidates(const ToaBackwardConfig *config,
                                     const int16_t *samples,
                                     ToaBackwardCandidates *out)
{
    if(out!=NULL){out->count=0U;}
    if(config==NULL || samples==NULL || out==NULL ||
       config->window_frames<32U || config->window_frames>TOA_MAX_FRAMES ||
       config->threshold_pcm>TOA_MAX_PCM ||
       config->run_samples<2U || config->run_samples>8U){return false;}
    uint32_t peak=0U;
    for(uint16_t i=0U;i<config->window_frames;++i){
        const uint32_t magnitude=(uint32_t)(samples[i]<0 ? -(int32_t)samples[i]:samples[i]);
        if(magnitude>peak){peak=magnitude;}
    }
    if(peak==0U){return false;}
    uint32_t limit=config->threshold_pcm;
    if(config->relative_scale>0U && config->relative_scale<=32U){
        const uint32_t relative=peak/config->relative_scale;
        if(relative>limit){limit=relative;}
    }
    if(limit==0U){return false;}
    const uint16_t frames=config->window_frames;
    uint64_t emax=0U;
    for(uint16_t i=0U;i<frames;++i){
        const uint64_t energy=ToaEnergyAt(samples,frames,(int32_t)i);
        if(energy>emax){emax=energy;}
    }
    if(emax==0U){return false;}
    const uint64_t elimit=emax>>3U;
    uint16_t seg_start=0U;
    for(uint16_t i=0U;i<frames;++i){
        if(ToaEnergyAt(samples,frames,(int32_t)i)>=elimit){seg_start=i;break;}
    }
    int32_t below=-1;
    for(int32_t i=(int32_t)seg_start;i>=0;--i){
        const uint32_t magnitude=(uint32_t)(samples[i]<0 ? -(int32_t)samples[i]:samples[i]);
        if(magnitude<limit){below=i;break;}
    }
    if(below<0){return false;}
    int32_t scan=below+1;
    while(scan<(int32_t)frames && out->count<TOA_BACKWARD_MAX_CANDIDATES){
        int32_t cross=-1;
        for(int32_t i=scan;i<(int32_t)frames;++i){
            const uint32_t before=(uint32_t)(samples[i-1]<0 ? -(int32_t)samples[i-1]:samples[i-1]);
            if(before>=limit){continue;}
            const uint32_t after=(uint32_t)(samples[i]<0 ? -(int32_t)samples[i]:samples[i]);
            if(after<limit){continue;}
            cross=i;
            break;
        }
        if(cross<0){break;}
        uint16_t run=0U;
        for(int32_t i=cross;i<(int32_t)frames;++i){
            const uint32_t magnitude=(uint32_t)(samples[i]<0 ? -(int32_t)samples[i]:samples[i]);
            if(magnitude<limit){break;}
            ++run;
            if(run>=config->run_samples){break;}
        }
        if(run<config->run_samples){scan=cross+1;continue;}
        const double hi=(double)(samples[cross]<0 ? -(int32_t)samples[cross]:samples[cross]);
        const double lo=(double)(samples[cross-1]<0 ? -(int32_t)samples[cross-1]:samples[cross-1]);
        if(hi<=lo){scan=cross+1;continue;}
        out->fractions[out->count]=(double)(cross-1)+
            (double)((uint32_t)limit-lo)/(hi-lo);
        ++out->count;
        scan=cross+1;
    }
    return out->count!=0U;
}