/**
 * @file        waveform_tdoa.h
 * @brief       两板同一段PCM波形的互相关时差精化（纯计算，无状态）。
 * @details     两板各自在检测到的起跳后固定偏移处截取同长度波形；先按事件时间差给出粗
 *              TDOA，再在有限滞后范围内做归一化互相关并用抛物线插值得到亚采样修正，
 *              消除单板阈值起跳±1齿（约65mm）的混叠。
 * @note        仅前台串行调用；不读PCM/HAL/网络，不保存指针；输入窗口必须已按各自事件
 *              时间对齐到相近的声学位置（偏差不超过约半个窗口）。
 */
#ifndef SBCG_WAVEFORM_TDOA_H
#define SBCG_WAVEFORM_TDOA_H
#include <stdbool.h>
#include <stdint.h>

/** @brief 建议窗口长度，单位采样；48kHz下512点为10.7ms，落在50ms chirp内部。 */
#define WAVEFORM_TDOA_WINDOW_FRAMES 512U
/** @brief 建议搜索半径，单位采样；覆盖单板起跳±2齿（约±18点）及少量模型误差。 */
#define WAVEFORM_TDOA_SEARCH_FRAMES 24U
/** @brief 接受结果所需的最小归一化相关系数（0..1），低于此值回退旧单值路径。 */
#define WAVEFORM_TDOA_MIN_CORRELATION 0.60

/**
 * @brief   用两段同长度波形精化 b-a 时差。
 * @param   a 非NULL A板波形窗口，长度不少于frames，仅本调用借用。
 * @param   b 非NULL B板波形窗口，长度不少于frames，仅本调用借用。
 * @param   frames 窗口采样数，须为2..1024。
 * @param   sample_rate_hz 采样率，须为1..192000。
 * @param   search_frames 滞后搜索半径，单位采样，1..48。
 * @param   delta0_ns 事件模型给出的粗时差 b-a，纳秒；作为精修的起点。
 * @param   tdoa_ns 非NULL输出；成功写入精化后的 b-a 时差，纳秒。
 * @param   confidence 非NULL输出；成功写入0..100的相关置信度（相关系数×100）。
 * @return  true表示找到有效互相关峰并写入输出；false为参数非法、窗口无起伏、相关系数
 *          低于门限或峰落在搜索边界，此时不写入输出。
 * @note    归一化在重叠区内按各自均值去除后计算，避免窗口直流偏置影响；插值只在内部峰
 *          上进行。结果仍以LOCAL=1/SYNC=0边界使用，不代表声学标定。
 */
bool WaveformTdoa_Refine(const int16_t *a, const int16_t *b, uint16_t frames,
                         uint32_t sample_rate_hz, uint8_t search_frames,
                         int64_t delta0_ns, int64_t *tdoa_ns,
                         uint8_t *confidence);

#endif
