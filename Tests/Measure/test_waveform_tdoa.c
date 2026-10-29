/**
 * @file        test_waveform_tdoa.c
 * @brief       波形时差纯核心的主机回归测试。
 * @details     检查正负滞后、拒绝条件，以及1024点满幅PCM和大直流偏置下的累加行为。
 * @note        确定性输入只验证计算契约，不代表板上耗时、声学距离或实物精度通过。
 */
#include "Measure/waveform_tdoa.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#undef assert
#define assert(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} } while(0)

/** @brief 生成确定性测试波形。 @param index 名义48kHz采样序号。 @return PCM16样本。 */
static int16_t Sample(uint32_t index)
{
    const double t = (double)index / 48000.0;
    const double value = 6000.0 * sin(2.0 * 3.14159265358979 * (2100.0 * t + 0.5 * 41.0 * t * t)) +
                         3000.0 * sin(2.0 * 3.14159265358979 * 3311.0 * t + 0.7);
    return (int16_t)(value > 0.0 ? value + 0.5 : value - 0.5);
}

/** @brief 原有带限波形用的长源序列；由main先填充，仅本测试访问。 */
static int16_t source[WAVEFORM_TDOA_WINDOW_FRAMES + 64U];
/**
 * @brief   从同一长源序列填充窗口，保证负滞后也有真实波形。
 * @param   out 非NULL输出，调用方提供至少frames个样本，返回前完成复制。
 * @param   frames 复制样本数。
 * @param   offset 源数组内起点；调用方保证offset+frames不越界。
 * @return  无返回值。
 */
static void FillFrom(int16_t *out, uint16_t frames, uint16_t offset)
{
    for (uint16_t i = 0U; i < frames; ++i) { out[i] = source[offset + i]; }
}

/**
 * @brief   更新确定性伪随机状态，供幅度和滞后测试使用。
 * @param   state 非NULL输入输出状态，初值须非零；仅调用期间借用。
 * @return  更新后的32位字，不用于设备随机数。
 */
static uint32_t RandomWord(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

/**
 * @brief   验证同源整数位移的时间符号、幅值和相关置信度。
 * @param   a 非NULL只读A窗口，至少frames个样本，仅调用期间借用。
 * @param   b 非NULL只读B窗口，至少frames个样本，仅调用期间借用。
 * @param   frames 每窗样本数。
 * @param   lag 预设整数滞后；正数表示b[j]对应a[j+lag]。
 * @param   search 滞后搜索半径，单位样本；本函数测试内部峰。
 * @param   delta0_ns 粗时差，单位纳秒。
 * @return  无返回值；偏离契约时终止测试。
 * @note    有限重叠会使三点插值略偏离整数，容许1000ns，不依赖板距真值。
 */
static void ExpectShift(const int16_t *a, const int16_t *b, uint16_t frames,
                        int lag, uint8_t search, int64_t delta0_ns)
{
    int64_t tdoa_ns = 0;
    uint8_t confidence = 0U;
    assert(WaveformTdoa_Refine(a, b, frames, 48000U, search, delta0_ns,
                              &tdoa_ns, &confidence));
    const int64_t expected_ns = delta0_ns -
        (int64_t)llround((double)lag * 1e9 / 48000.0);
    const int64_t error_ns = tdoa_ns > expected_ns ? tdoa_ns - expected_ns :
                                                        expected_ns - tdoa_ns;
    assert(error_ns <= 1000);
    assert(confidence == 100U);
}

/**
 * @brief   验证被拒绝的输入保持两个输出值不变。
 * @param   a 非NULL只读A窗口，提供至少frames个样本。
 * @param   b 非NULL只读B窗口，提供至少frames个样本。
 * @param   frames 每窗样本数；本测试只传入合法缓冲长度。
 * @param   search 搜索半径，单位样本；本测试保证小于frames。
 * @return  无返回值；接受了拒绝用例或改写输出时终止测试。
 */
static void ExpectRejected(const int16_t *a, const int16_t *b,
                           uint16_t frames, uint8_t search)
{
    int64_t tdoa_ns = INT64_C(123456789);
    uint8_t confidence = 73U;
    assert(!WaveformTdoa_Refine(a, b, frames, 48000U, search, 0,
                               &tdoa_ns, &confidence));
    assert(tdoa_ns == INT64_C(123456789));
    assert(confidence == 73U);
}

/**
 * @brief   检查最长窗口的满幅乘积累加、大直流偏置和搜索边界。
 * @return  无返回值；通过本地断言验证，不访问硬件。
 * @note    1024个INT16_MIN平方之和为2^40，可暴露错误的32位累加；输入确定性生成。
 */
static void TestAccumulatorRange(void)
{
    int16_t extended[1120];
    int16_t a[1024], b[1024];
    uint32_t state = UINT32_C(0x3f7952a1);
    for (uint16_t i = 0U; i < 1120U; ++i)
    {
        extended[i] = (RandomWord(&state) & 1U) != 0U ? INT16_MIN : INT16_MAX;
    }
    for (int lag = -31; lag <= 31; lag += 62)
    {
        for (uint16_t i = 0U; i < 1024U; ++i)
        {
            a[i] = extended[48U + i];
            b[i] = extended[(uint16_t)(48 + lag) + i];
        }
        ExpectShift(a, b, 1024U, lag, 48U, INT64_C(-1000000));
    }
    ExpectShift(a, a, 1024U, 0, 48U, INT64_C(987654321));

    /* 两路直流符号相反，相关信息仅为小幅起伏，检查去均值所用五个和。 */
    for (uint16_t i = 0U; i < 1120U; ++i)
    { extended[i] = (int16_t)((int32_t)(RandomWord(&state) % 9U) - 4); }
    for (uint16_t i = 0U; i < 1024U; ++i)
    {
        a[i] = (int16_t)(32763 + extended[48U + i]);
        b[i] = (int16_t)(-32763 + extended[55U + i]);
    }
    ExpectShift(a, b, 1024U, 7, 24U, INT64_C(1000000));

    /* 满幅直流仍必须拒绝，且不能改写调用方保留的结果。 */
    for (uint16_t i = 0U; i < 1024U; ++i) { a[i] = INT16_MIN; b[i] = INT16_MAX; }
    ExpectRejected(a, b, 1024U, 48U);

    /* 构造真正落在左右搜索边界的宽带峰，检查两种符号均拒绝。 */
    for (uint16_t i = 0U; i < 1120U; ++i)
    { extended[i] = (int16_t)((int32_t)(RandomWord(&state) & 65535U) - 32768); }
    for (int lag = -24; lag <= 24; lag += 48)
    {
        for (uint16_t i = 0U; i < 1024U; ++i)
        {
            a[i] = extended[48U + i];
            b[i] = extended[(uint16_t)(48 + lag) + i];
        }
        ExpectRejected(a, b, 1024U, 24U);
    }
    /* 少于64点时没有足够重叠；64点同源窗口的零滞后则满足最小重叠。 */
    ExpectRejected(a, a, 63U, 24U);
    ExpectShift(a, a, 64U, 0, 24U, INT64_C(1000000));
}

/** @brief 依次运行计算与拒绝用例。 @return 0表示所有断言通过，否则测试立即失败。 */
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
    TestAccumulatorRange();
    puts("PASS waveform TDOA: sub-sample lag, sign, rejection gates, full-scale 1024, DC offsets");
    return 0;
}
