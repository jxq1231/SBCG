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
 * @param   out_fraction 非NULL输出；失败保留原值，成功为[0,window_frames-1)的double，
 *          i表示前沿位于样本i-1与i之间且插值系数∈[0,1)，越界/恒阈值时夹紧。
 * @return  true为找到前沿；false为参数非法、无过阈点或首个样本已过阈（前沿在窗外）。
 * @note    无硬件访问、无状态、可重入；纯前台，线性插值使用绝对幅值。
 */
bool ToaEstimator_Estimate(const ToaEstimatorConfig *config,
                           const int16_t *samples, double *out_fraction);

#endif