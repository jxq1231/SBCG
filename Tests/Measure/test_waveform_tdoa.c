#include "Measure/waveform_tdoa.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#undef assert
#define assert(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} } while(0)

/** @brief 生成一段确定性的宽带测试波形。 */
static int16_t Sample(uint32_t index)
{
    const double t = (double)index / 48000.0;
    const double value = 6000.0 * sin(2.0 * 3.14159265358979 * (2100.0 * t + 0.5 * 41.0 * t * t)) +
                         3000.0 * sin(2.0 * 3.14159265358979 * 3311.0 * t + 0.7);
    return (int16_t)(value > 0.0 ? value + 0.5 : value - 0.5);
}

/** @brief 从同一长源序列的指定起点填充窗口，保证负偏移也有真实波形。 */
static int16_t source[WAVEFORM_TDOA_WINDOW_FRAMES + 64U];
static void FillFrom(int16_t *out, uint16_t frames, uint16_t offset)
{
    for (uint16_t i = 0U; i < frames; ++i) { out[i] = source[offset + i]; }
}

int main(void)
{
    int16_t a[WAVEFORM_TDOA_WINDOW_FRAMES];
    int16_t b[WAVEFORM_TDOA_WINDOW_FRAMES];
    int16_t flat[WAVEFORM_TDOA_WINDOW_FRAMES];
    int16_t noise[WAVEFORM_TDOA_WINDOW_FRAMES];
    int64_t tdoa = 0;
    uint8_t confidence = 0U;
    const int64_t delta0 = 1000000;

    /* b[j]=a[j+7]：互相关峰在+7采样；tdoa=delta0-7*T。 */
    for (uint16_t i = 0U; i < (uint16_t)sizeof source / sizeof source[0]; ++i)
    { source[i] = Sample(i); }
    FillFrom(a, WAVEFORM_TDOA_WINDOW_FRAMES, 0U);
    FillFrom(b, WAVEFORM_TDOA_WINDOW_FRAMES, 7U);
    assert(WaveformTdoa_Refine(a, b, WAVEFORM_TDOA_WINDOW_FRAMES, 48000U,
                               WAVEFORM_TDOA_SEARCH_FRAMES, delta0,
                               &tdoa, &confidence));
    {
        const int64_t expected = delta0 - (int64_t)llround(7.0 * 1e9 / 48000.0);
        const int64_t error = tdoa > expected ? tdoa - expected : expected - tdoa;
        assert(error <= 1000);
    }
    assert(confidence >= 90U);

    /* 负滞后与零粗差：b[j]=a[j-5]，tdoa为+5采样的时间。 */
    FillFrom(a, WAVEFORM_TDOA_WINDOW_FRAMES, 5U);
    FillFrom(b, WAVEFORM_TDOA_WINDOW_FRAMES, 0U);
    assert(WaveformTdoa_Refine(a, b, WAVEFORM_TDOA_WINDOW_FRAMES, 48000U,
                               WAVEFORM_TDOA_SEARCH_FRAMES, 0, &tdoa, &confidence));
    {
        const int64_t expected = (int64_t)llround(5.0 * 1e9 / 48000.0);
        const int64_t error = tdoa > expected ? tdoa - expected : expected - tdoa;
        assert(error <= 1000);
    }

    /* 无起伏窗口不得给出结果。 */
    for (uint16_t i = 0U; i < WAVEFORM_TDOA_WINDOW_FRAMES; ++i) { flat[i] = 123; }
    assert(!WaveformTdoa_Refine(flat, flat, WAVEFORM_TDOA_WINDOW_FRAMES, 48000U,
                                WAVEFORM_TDOA_SEARCH_FRAMES, 0, &tdoa, &confidence));

    /* 与A不相关的噪声必须被门限拒绝。 */
    for (uint16_t i = 0U; i < WAVEFORM_TDOA_WINDOW_FRAMES; ++i)
    { noise[i] = (int16_t)(((uint32_t)i * 2654435761U) % 20001U - 10000U); }
    assert(!WaveformTdoa_Refine(a, noise, WAVEFORM_TDOA_WINDOW_FRAMES, 48000U,
                                WAVEFORM_TDOA_SEARCH_FRAMES, 0, &tdoa, &confidence));

    /* 参数边界。 */
    assert(!WaveformTdoa_Refine(NULL, b, WAVEFORM_TDOA_WINDOW_FRAMES, 48000U,
                                WAVEFORM_TDOA_SEARCH_FRAMES, 0, &tdoa, &confidence));
    assert(!WaveformTdoa_Refine(a, b, 1U, 48000U, WAVEFORM_TDOA_SEARCH_FRAMES,
                               0, &tdoa, &confidence));
    assert(!WaveformTdoa_Refine(a, b, WAVEFORM_TDOA_WINDOW_FRAMES, 0U,
                                WAVEFORM_TDOA_SEARCH_FRAMES, 0, &tdoa, &confidence));
    assert(!WaveformTdoa_Refine(a, b, WAVEFORM_TDOA_WINDOW_FRAMES, 48000U, 0U,
                                0, &tdoa, &confidence));
    assert(!WaveformTdoa_Refine(a, b, WAVEFORM_TDOA_WINDOW_FRAMES, 48000U,
                                WAVEFORM_TDOA_SEARCH_FRAMES, 0, NULL, &confidence));
    puts("PASS waveform TDOA: sub-sample lag, sign, band-limited signal, rejection gates");
    return 0;
}
