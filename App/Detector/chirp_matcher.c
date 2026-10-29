/**
 * @file        chirp_matcher.c
 * @brief       实现定长PCM16环形窗口的去直流模板相关及有界峰观察。
 * @details     逐帧复制输入、更新64位能量和，门限通过后直接点积，输出最高评分窗口元数据。
 * @note        无硬件/堆内存/日志依赖；double和逐点相关仅为本轮正确性基线，未接入实时采集。
 */
#include "chirp_matcher.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

const ChirpMatcherConfig ChirpMatcher_DefaultConfig={64U,0.80f,48U,24000U};

/**
 * @brief 清流状态但保留已经校验的模板与配置。
 * @param m 非NULL独占状态，模板/配置须有效。
 * @return 无返回值。
 * @note 仅串行前台/主机使用，不发布待定峰，不解引用输入PCM。
 */
static void ClearStream(ChirpMatcher *m)
{
    memset(m->ring,0,sizeof m->ring);
    m->used=m->next=m->clipped_samples=m->remaining=0U;
    m->sample_sum=0; m->sample_squares=0U; m->best_score=0.0;
    m->best=(ChirpMatch){0}; m->previous_frame=m->previous_event_first=0U;
    m->session=0U; m->have_input=m->pending=m->have_event=false;
}

/**
 * @brief 返回N乘去直流平方和，避免逐点浮点去均值。
 * @param sum 至多2400个PCM16的有符号和。
 * @param squares 对应平方和，单位PCM16幅值平方。
 * @param count 样本数，16..2400。
 * @return count*squares-sum*sum，范围不超过2400²*32768²，小于2^53。
 * @note 所有累加/乘法均在64位范围内；只供有效状态使用。
 */
static uint64_t Variance(int64_t sum,uint64_t squares,uint16_t count)
{
    return (uint64_t)count*squares-(uint64_t)(sum*sum);
}

/**
 * @brief 检查初始化参数并计算只读模板的统计量。
 * @param m 非NULL输出状态，失败清零。
 * @param template_pcm 非NULL且不与状态重叠的只读模板，借用到下次Init结束。
 * @param template_frames 模板单声道点数，16..2400。
 * @param config 非NULL配置，可来自旧m.config，先复制。
 * @return true为配置完成；false为非法配置或零方差模板。
 * @note 不生成模板，不访问外设，模板必须由调用方保持有效且不变。
 */
bool ChirpMatcher_Init(ChirpMatcher *m,const int16_t *template_pcm,
                       uint16_t template_frames,const ChirpMatcherConfig *config)
{
    if(m==NULL)return false;
    const ChirpMatcherConfig copy=config!=NULL ? *config:(ChirpMatcherConfig){0};
    memset(m,0,sizeof *m);
    if(template_pcm==NULL || config==NULL || template_frames<16U ||
       template_frames>CHIRP_MATCHER_MAX_FRAMES || copy.min_rms_pcm>32768U ||
       !isfinite(copy.min_correlation) || copy.min_correlation<=0.0f ||
       copy.min_correlation>1.0f || copy.peak_search_frames==0U ||
       copy.peak_search_frames>CHIRP_MATCHER_MAX_FRAMES)return false;
    int64_t sum=0; uint64_t squares=0U;
    for(uint16_t i=0U;i<template_frames;++i) {
        const int64_t x=template_pcm[i];sum+=x;squares+=(uint64_t)(x*x);
    }
    const uint64_t variance=Variance(sum,squares,template_frames);
    if(variance==0U)return false;
    m->template_pcm=template_pcm;m->template_frames=template_frames;m->config=copy;
    m->template_sum=sum;m->template_variance=variance;m->configured=true;
    return true;
}

/**
 * @brief 显式清空流，保留配置与模板。
 * @param m 非NULL且已配置的独占状态。
 * @return true为成功清流，false为NULL/未配置且不写状态。
 * @note 清掉未完成的峰观察，不把残留候选作为事件发布。
 */
bool ChirpMatcher_Reset(ChirpMatcher *m)
{
    if(m==NULL || !m->configured)return false;
    ClearStream(m);return true;
}

/**
 * @brief 将一个样本复制到环形缓冲，同时维护和、平方和与截幅计数。
 * @param m 已配置的非NULL独占状态。
 * @param sample PCM16单声道值。
 * @return 无返回值。
 * @note 环形缓冲满后next指向时间上最早的样本；不引用DMA内存。
 */
static void Append(ChirpMatcher *m,int16_t sample)
{
    if(m->used==m->template_frames) {
        const int64_t old=m->ring[m->next];
        m->sample_sum-=old;m->sample_squares-=(uint64_t)(old*old);
        if(old==INT16_MIN || old==INT16_MAX)--m->clipped_samples;
    } else ++m->used;
    const int64_t x=sample;
    m->ring[m->next]=sample;m->sample_sum+=x;m->sample_squares+=(uint64_t)(x*x);
    if(sample==INT16_MIN || sample==INT16_MAX)++m->clipped_samples;
    if(++m->next==m->template_frames)m->next=0U;
}

/**
 * @brief 对完整且过能量/去重门限的窗口计算评分，必要时更新待定峰。
 * @param m 非NULL独占状态，已追加当前样本。
 * @param frame 当前窗口最后一个样本的绝对帧号。
 * @return 无返回值；可能建立待定峰，但不直接发布事件。
 * @note 点积最坏O(N)；整数分子不溢出int64，方差乘积先转double以避免64位乘积溢出。
 */
static void Evaluate(ChirpMatcher *m,uint64_t frame)
{
    if(m->used!=m->template_frames)return;
    const uint64_t first=frame-(m->template_frames-1U);
    if(m->have_event && first-m->previous_event_first<m->config.refractory_frames)return;
    const uint64_t variance=Variance(m->sample_sum,m->sample_squares,m->template_frames);
    const uint64_t n=m->template_frames,rms=m->config.min_rms_pcm;
    if(variance==0U || variance<rms*rms*n*n)return;
    int64_t dot=0;uint16_t j=0U;
    for(uint16_t i=m->next;i<m->template_frames;++i)
        dot+=(int64_t)m->ring[i]*m->template_pcm[j++];
    for(uint16_t i=0U;i<m->next;++i)
        dot+=(int64_t)m->ring[i]*m->template_pcm[j++];
    const int64_t numerator=(int64_t)n*dot-m->sample_sum*m->template_sum;
    double score=fabs((double)numerator)/sqrt((double)variance*(double)m->template_variance);
    if(score>1.0)score=1.0;
    if(score<(double)m->config.min_correlation || (m->pending && score<=m->best_score))return;
    if(!m->pending) { m->pending=true;m->remaining=m->config.peak_search_frames; }
    m->best_score=score;
    m->best=(ChirpMatch){m->session,first,frame,0U,(float)score,
        (uint16_t)sqrt((double)variance/(double)(n*n)),numerator<0,m->clipped_samples!=0U};
}

/**
 * @brief 消费单帧并在固定观察帧数后发布最佳匹配窗口。
 * @param m 非NULL已初始化状态；调用方独占。
 * @param session 非零流会话，0清流但不消费。
 * @param frame_index 绝对帧号，重复/缺口/倒序清流后消费，UINT64_MAX后同会话拒绝。
 * @param sample 当前单声道PCM16，按值复制。
 * @param out 非NULL不重叠输出，无事件时清零；NULL拒绝且不消费。
 * @return NONE、EVENT、DISCONTINUITY或INVALID；EVENT只表示数字模板匹配。
 * @note 待定峰不跨流边界；观察尾部不足不发布；无真实时间/物理距离换算。
 */
ChirpMatcherResult ChirpMatcher_Push(ChirpMatcher *m,uint32_t session,
                                   uint64_t frame_index,int16_t sample,ChirpMatch *out)
{
    if(out==NULL)return CHIRP_MATCH_INVALID;
    *out=(ChirpMatch){0};
    if(m==NULL || !m->configured)return CHIRP_MATCH_INVALID;
    if(session==0U) { ClearStream(m);return CHIRP_MATCH_INVALID; }
    bool boundary=false;
    if(m->have_input) {
        if(session==m->session && m->previous_frame==UINT64_MAX)return CHIRP_MATCH_INVALID;
        boundary=session!=m->session || frame_index!=m->previous_frame+1U;
        if(boundary)ClearStream(m);
    }
    m->session=session;m->previous_frame=frame_index;m->have_input=true;
    const bool was_pending=m->pending;
    Append(m,sample);Evaluate(m,frame_index);
    if(was_pending && --m->remaining==0U) {
        *out=m->best;out->confirmed_frame=frame_index;
        m->pending=false;m->have_event=true;m->previous_event_first=out->first_frame;
        return CHIRP_MATCH_EVENT;
    }
    return boundary ? CHIRP_MATCH_DISCONTINUITY:CHIRP_MATCH_NONE;
}
