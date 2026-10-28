/**
 * @file        toa_estimator.h
 * @brief       声明声事件到达时刻的子帧前沿过阈估算接口。
 * @details     对候选窗内波形按绝对幅值找首个超过阈值的上升沿，用前后两点线性插值
 *              得到小于一个采样周期的子帧偏移；纯计算，不读时钟、不访问PCM外的硬件。
 * @note        幅值阈值可配置为主阈值或相对窗峰；输出为样本栅格内的偏移，达秒级换算
 *              由调用方按采样率完成，本模块不承诺PTP绝对时基。
 */
#ifndef SBCG_TOA_ESTIMATOR_H
#define SBCG_TOA_ESTIMATOR_H
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 前沿阈值配置。
 * @note  threshold_pcm为绝对幅值下限（PCM16单位，0禁用）；relative_scale在1..16时
 *        实际阈值为threshold_pcm与peak_abs/relative_scale的较大者，用于适应幅度波动；
 *        window_frames为候选窗点数8..512，峰值取窗内最大值。
 */
typedef struct {
    uint16_t window_frames;
    uint16_t threshold_pcm;
    uint16_t relative_scale;
} ToaEstimatorConfig;

/** @brief 试验默认：32点窗、阈值128、相对峰值1/4取样。 */
extern const ToaEstimatorConfig ToaEstimator_DefaultConfig;

/**
 * @brief   估算候选窗前沿的过阈位置（相对samples[0]的子帧偏移）。
 * @param   config 非NULL有效配置，只读。
 * @param   samples 非NULL连续只读PCM，至少window_frames个点，仅本调用借用。
 * @param   out_fraction 非NULL输出；成功为前沿插值位置，失败保持原值。
 * @return  true为找到前沿；false为参数非法、无过阈点或首个样本已过阈（前沿在窗外）。
 * @note    无硬件访问、无状态、可重入；纯前台，线性插值使用绝对幅值。
 */
bool ToaEstimator_Estimate(const ToaEstimatorConfig *config,
                           const int16_t *samples, double *out_fraction);

/**
 * @brief 能量包络起跳的栅格无关前沿配置。
 * @note  搜索窗32..512点；按5点短窗平方能量求Emax，取窗内最左的>=Emax/8能量段
 *        起点作为突发粗起点（直达波优先；直达与更强回声之间能量凹低于Emax/8时，
 *        旧实现会把锚点落到回声段，跨轮回放跳动可达±1.9ms），再从粗起点向左找最后
 *        一个低于幅值阈值的样本，向右取首个真实跨阈样本对线性插值出子帧前沿。
 *        run_samples为连续过阈样本数2..8，抑制单点尖峰误触；该位置与调用方候选窗
 *        起点(如64帧门控栅格)无依赖，适合作为绝对前沿锚点。
 */
typedef struct {
    uint16_t window_frames;
    uint16_t threshold_pcm;
    uint16_t relative_scale;
    uint8_t run_samples;
} ToaBackwardConfig;

/** @brief 试验默认：256点窗、阈值128、相对峰值1/8、连续4样本过阈。 */
extern const ToaBackwardConfig ToaBackward_DefaultConfig;

/** @brief 一次估算最多保留的早/晚过阈候选数。 */
#define TOA_BACKWARD_MAX_CANDIDATES 4U

/**
 * @brief 同一候选窗内的多个有效上升沿候选。
 * @note  fractions按时间递增；count=0表示无有效前沿；调用方只读。
 */
typedef struct {
    uint8_t count;
    double fractions[TOA_BACKWARD_MAX_CANDIDATES];
} ToaBackwardCandidates;

/**
 * @brief   枚举同一候选窗内的前4个有效上升沿候选。
 * @param   config 非NULL有效配置，规则与ToaEstimator_EstimateBackward相同。
 * @param   samples 非NULL连续只读PCM，至少window_frames个点，仅本调用借用。
 * @param   out 非NULL输出；成功时count为1..4，失败时清为0。
 * @return  true表示至少找到一个候选；false表示参数非法、无峰值、
 *          整窗已过阈或首个候选后无连续过阈段。
 * @note    第1个候选与ToaEstimator_EstimateBackward的第一前沿完全一致；
 *          其余候选按时间递增追加，供上层做早/晚齿选择。纯计算，无状态。
 */
bool ToaEstimator_EstimateCandidates(const ToaBackwardConfig *config,
                                     const int16_t *samples,
                                     ToaBackwardCandidates *out);

/**
 * @brief   基于能量包络起跳定位直达前沿，返回相对samples[0]的子帧位置。
 * @param   config 非NULL有效配置，只读。
 * @param   samples 非NULL连续只读PCM，至少window_frames个点，仅本调用借用。
 * @param   out_fraction 非NULL输出；成功为前沿插值位置，失败保持原值。
 * @return  true为找到连续前沿；false为参数非法、无峰值、整窗已过阈或连续段不足。
 * @note    与旧Estimate的正向线性插值不同，本接口先用能量包络粗定位窗内最左能量段
 *          起点再做样本级过阈插值，直达波优先，避免幅度大于直达波的多径回声峰被直接
 *          选作锚点；峰可出现在窗内任意位置，输出不依赖调用方候选窗栅格。
 */
bool ToaEstimator_EstimateBackward(const ToaBackwardConfig *config,
                                   const int16_t *samples, double *out_fraction);

#endif