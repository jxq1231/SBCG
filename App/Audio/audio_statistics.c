/**
 * @file        audio_statistics.c
 * @brief       计算双声道交织PCM16幅值统计。
 * @details     分别计算最小值、最大值、绝对峰值和整数RMS，使用整数平方根，不访问外设。
 * @note        输入需含两倍帧数的样本且调用期间有效；BSP负责检验DMA覆盖竞争。
 */
#include "audio_statistics.h"
#include <limits.h>
#include <stddef.h>

/**
 * @brief   对64位无符号整数开平方并向下取整。
 * @param   value 待开方非负整数。
 * @return  向下取整的平方根，uint32_t可容纳。
 * @note    纯计算，只使用局部变量。
 */
static uint32_t SquareRoot(uint64_t value)
{
    uint64_t root = 0U;
    uint64_t bit = UINT64_C(1) << 62;
    while (bit > value) { bit >>= 2; }
    while (bit != 0U)
    {
        if (value >= root + bit)
        {
            value -= root + bit;
            root = (root >> 1) + bit;
        }
        else { root >>= 1; }
        bit >>= 2;
    }
    return (uint32_t)root;
}

/**
 * @brief   计算PCM16双声道块的最小值、最大值、峰值和整数RMS。
 * @param   samples 只读交织样本，至少2*frame_count个int16_t，不可为NULL，仅调用期间读取。
 * @param   frame_count 双声道帧数，范围1至UINT16_MAX。
 * @param   output 调用方可写统计对象，不可为NULL；失败保持原值。
 * @return  输入有效且计算完成返回true，否则false。
 * @note    不保留样本指针；RMS为floor(sqrt(mean(square)))，DMA所有权由调用方验证。
 */
bool AudioStatistics_Compute(const volatile int16_t *samples,
                             uint32_t frame_count, AudioStatistics *output)
{
    if (samples == NULL || output == NULL || frame_count == 0U ||
        frame_count > UINT16_MAX) { return false; }
    AudioStatistics result = {0};
    uint64_t energy[2] = {0};
    for (unsigned c = 0; c < 2U; ++c)
    {
        result.channel[c].minimum = INT16_MAX;
        result.channel[c].maximum = INT16_MIN;
    }
    for (uint32_t i = 0; i < frame_count; ++i)
    {
        for (unsigned c = 0; c < 2U; ++c)
        {
            const int32_t value = *samples++;
            AudioChannelStatistics *channel = &result.channel[c];
            const uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
            if (value < channel->minimum) { channel->minimum = (int16_t)value; }
            if (value > channel->maximum) { channel->maximum = (int16_t)value; }
            if (magnitude > channel->peak) { channel->peak = magnitude; }
            energy[c] += (uint64_t)magnitude * magnitude;
        }
    }
    for (unsigned c = 0; c < 2U; ++c)
    {
        result.channel[c].rms = SquareRoot(energy[c] / frame_count);
    }
    *output = result;
    return true;
}
