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
 * @brief 栅格无关的峰值回溯前沿配置。
 * @note  搜索窗64..512点；从窗内峰值逐样本向左找最后一段连续阈值以上段的起点，
 *        再用 lo/start 样本对线性插值出子帧前沿，输出相对samples[0]的位置。
 *        run_samples为连续过阈样本数2..8，抑制单点尖峰误触；该位置与调用方
 *        候选窗起点(如64帧门控栅格)无依赖，适合作为绝对前沿锚点。
 */
typedef struct {
    uint16_t window_frames;
    uint16_t threshold_pcm;
    uint16_t relative_scale;
    uint8_t run_samples;
} ToaBackwardConfig;

/** @brief 试验默认：256点窗、阈值128、相对峰值1/8、连续4样本过阈。 */
extern const ToaBackwardConfig ToaBackward_DefaultConfig;

/**
 * @brief   从峰值向左回溯定位连续过阈前沿，返回相对samples[0]的子帧位置。
 * @param   config 非NULL有效配置，只读。
 * @param   samples 非NULL连续只读PCM，至少window_frames个点，仅本调用借用。
 * @param   out_fraction 非NULL输出；成功为前沿插值位置，失败保持原值。
 * @return  true为找到连续前沿；false为参数非法、无峰值、整窗已过阈或连续段不足。
 * @note    与旧Estimate的正向线性插值不同，本接口基于峰回溯，不依赖候选窗栅格；
 *          峰可出现在窗内任意位置，前沿为最后一段连续过阈的起点到其前一样本插值。
 */
bool ToaEstimator_EstimateBackward(const ToaBackwardConfig *config,
                                   const int16_t *samples, double *out_fraction);

#endif