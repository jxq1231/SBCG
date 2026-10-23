/**
 * @file        audio_statistics.h
 * @brief       计算双声道交织PCM16幅值统计。
 * @details     声明本模块接口、数据类型或配置常量。分别计算最小值、最大值、绝对峰值和整数RMS，使用整数平方根，不访问外设。
 * @note        输入需含两倍帧数的样本且调用期间有效；BSP负责检验DMA覆盖竞争。
 */
#ifndef SBCG_AUDIO_STATISTICS_H
#define SBCG_AUDIO_STATISTICS_H
#include <stdbool.h>
#include <stdint.h>

/** @brief 单声道PCM16幅值统计；最小/最大保留符号，peak/rms为非负样本幅值。 */
typedef struct
{
    int16_t minimum, maximum;
    uint32_t peak, rms;
} AudioChannelStatistics;
/** @brief 与输入交织顺序一致的两个声道统计，不指定左右声学空间含义。 */
typedef struct { AudioChannelStatistics channel[2]; } AudioStatistics;

/* Interleaved signed PCM16, two channels. Bounded to UINT16_MAX frames.
   RMS is floor(sqrt(mean(square))); invalid input leaves output unchanged. */
/**
 * @brief   计算PCM16双声道块的最小值、最大值、峰值和整数RMS。
 * @param   samples 只读交织样本，至少2*frame_count个int16_t，不可为NULL，仅调用期间读取。
 * @param   frame_count 双声道帧数，范围1至UINT16_MAX。
 * @param   output 调用方可写统计对象，不可为NULL；失败保持原值。
 * @return  输入有效且计算完成返回true，否则false。
 * @note    不保留样本指针；RMS为floor(sqrt(mean(square)))，DMA所有权由调用方验证。
 */
bool AudioStatistics_Compute(const volatile int16_t *samples,
                             uint32_t frame_count, AudioStatistics *output);
#endif
