/**
 * @file        waveform_tdoa.c
 * @brief       两板同一段PCM波形的互相关时差精化实现。
 * @details     对每个候选滞后在重叠区内去除各自均值后计算归一化相关系数，取最大峰并
 *              用相邻三点抛物线插值得到亚采样滞后；结合事件模型粗时差得到精化TDOA。
 * @note        纯计算；PCM乘积和使用int64精确累加，每个滞后仅在归一化时转double，
 *              避免Cortex-M7单精度FPU上的逐采样软件双精度运算；实际耗时须上板观察。
 */
#include "waveform_tdoa.h"
#include <math.h>
#include <stddef.h>

#define WAVEFORM_TDOA_MAX_FRAMES 1024U
#define WAVEFORM_TDOA_MAX_SEARCH 48U
#define WAVEFORM_TDOA_MIN_OVERLAP 64U

/**
 * @brief   计算指定滞后下的归一化互相关。
 * @param   a 非NULL A窗口。
 * @param   b 非NULL B窗口。
 * @param   frames 窗口长度。
 * @param   lag 滞后，正数表示用a的靠后部分对b的靠前部分（与事件时间约定一致）。
 * @return  相关系数，重叠不足或窗口无起伏时返回-1.0。
 * @note    int16乘积在int32范围内，最多1024点的累加绝对值不超过2^40；转double仍精确，
 *          后续去均值、归一化的运算顺序保持不变，不改变相关门限或峰选择规则。
 */
static double Correlation(const int16_t *a, const int16_t *b, uint16_t frames, int lag)
{
    const uint16_t shift = (uint16_t)(lag >= 0 ? lag : -lag);
    const uint16_t n = (uint16_t)(frames - shift);
    if (n < WAVEFORM_TDOA_MIN_OVERLAP) { return -1.0; }
    const uint16_t x0 = lag >= 0 ? shift : 0U;
    const uint16_t y0 = lag >= 0 ? 0U : shift;
    int64_t sum_ab_int = 0, sum_aa_int = 0, sum_bb_int = 0;
    int64_t sum_a_int = 0, sum_b_int = 0;
    for (uint16_t i = 0U; i < n; ++i)
    {
        const int32_t av = a[x0 + i];
        const int32_t bv = b[y0 + i];
        sum_ab_int += av * bv;
        sum_aa_int += av * av;
        sum_bb_int += bv * bv;
        sum_a_int += av;
        sum_b_int += bv;
    }
    const double sum_ab = (double)sum_ab_int, sum_aa = (double)sum_aa_int;
    const double sum_bb = (double)sum_bb_int, sum_a = (double)sum_a_int;
    const double sum_b = (double)sum_b_int;
    const double variance_a = sum_aa - sum_a * sum_a / n;
    const double variance_b = sum_bb - sum_b * sum_b / n;
    /* 每个采样至少1 LSB方差才可能有可分辨峰；否则直流/静音窗口的舍入残差会产生伪相关。 */
    if (!(variance_a >= (double)n) || !(variance_b >= (double)n)) { return -1.0; }
    return (sum_ab - sum_a * sum_b / n) / sqrt(variance_a * variance_b);
}

bool WaveformTdoa_Refine(const int16_t *a, const int16_t *b, uint16_t frames,
                         uint32_t sample_rate_hz, uint8_t search_frames,
                         int64_t delta0_ns, int64_t *tdoa_ns,
                         uint8_t *confidence)
{
    if (a == NULL || b == NULL || tdoa_ns == NULL || confidence == NULL ||
        frames < 2U || frames > WAVEFORM_TDOA_MAX_FRAMES ||
        sample_rate_hz == 0U || sample_rate_hz > 192000U ||
        search_frames == 0U || search_frames > WAVEFORM_TDOA_MAX_SEARCH)
    { return false; }
    const int16_t search = (int16_t)search_frames;
    int16_t best_lag = 0;
    double best = -1.0;
    for (int16_t lag = (int16_t)-search; lag <= search; ++lag)
    {
        const double value = Correlation(a, b, frames, lag);
        if (value > best) { best = value; best_lag = lag; }
    }
    if (!(best >= WAVEFORM_TDOA_MIN_CORRELATION) ||
        best_lag == -search || best_lag == search)
    { return false; }
    const double left = Correlation(a, b, frames, (int)best_lag - 1);
    const double right = Correlation(a, b, frames, (int)best_lag + 1);
    const double curvature = left - 2.0 * best + right;
    double fraction = 0.0;
    if (curvature < 0.0) { fraction = 0.5 * (left - right) / curvature; }
    if (fraction > 0.5) { fraction = 0.5; }
    if (fraction < -0.5) { fraction = -0.5; }
    const double lag_samples = (double)best_lag + fraction;
    const int64_t lag_ns = (int64_t)llround(lag_samples * 1e9 / (double)sample_rate_hz);
    *tdoa_ns = delta0_ns - lag_ns;
    double quality = best * 100.0;
    if (quality > 100.0) { quality = 100.0; }
    *confidence = (uint8_t)(quality + 0.5);
    return true;
}
