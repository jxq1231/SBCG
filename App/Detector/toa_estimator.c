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