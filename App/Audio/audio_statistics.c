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
    /* 双声道展开为局部标量，去掉内层循环和结构体索引；整数和与字段语义不变。 */
    int16_t minimum0 = INT16_MAX, maximum0 = INT16_MIN;
    int16_t minimum1 = INT16_MAX, maximum1 = INT16_MIN;
    uint32_t peak0 = 0U, peak1 = 0U;
    uint64_t energy0 = UINT64_C(0), energy1 = UINT64_C(0);
    const volatile int16_t *cursor = samples;
    for (uint32_t i = 0U; i < frame_count; ++i)
    {
        const int32_t value0 = *cursor++;
        const int32_t value1 = *cursor++;
        const uint32_t magnitude0 = (uint32_t)(value0 < 0 ? -value0 : value0);
        const uint32_t magnitude1 = (uint32_t)(value1 < 0 ? -value1 : value1);
        if (value0 < minimum0) { minimum0 = (int16_t)value0; }
        if (value0 > maximum0) { maximum0 = (int16_t)value0; }
        if (value1 < minimum1) { minimum1 = (int16_t)value1; }
        if (value1 > maximum1) { maximum1 = (int16_t)value1; }
        if (magnitude0 > peak0) { peak0 = magnitude0; }
        if (magnitude1 > peak1) { peak1 = magnitude1; }
        energy0 += (uint64_t)magnitude0 * magnitude0;
        energy1 += (uint64_t)magnitude1 * magnitude1;
    }
    AudioStatistics result;
    result.channel[0] = (AudioChannelStatistics){
        minimum0, maximum0, peak0, SquareRoot(energy0 / frame_count)};
    result.channel[1] = (AudioChannelStatistics){
        minimum1, maximum1, peak1, SquareRoot(energy1 / frame_count)};
    *output = result;
    return true;
}
