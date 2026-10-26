/**
 * @file        event_classifier.c
 * @brief       标准chirp/单次击掌波形与包络代理分类。
 * @details     只使用候选后的PCM标量特征；不依赖模板、FFT或网络。
 * @note        实验性分类，可能受房间/设备影响；不能替代标准声身份或物理精度验收。
 */
#include "event_classifier.h"
#include <stddef.h>
#include <string.h>

#define MIN_FRAMES 480U
#define MAX_FRAMES 2400U
#define MIN_PEAK 512U

static uint32_t Magnitude(int16_t value)
{
    return value < 0 ? (uint32_t)(-(int32_t)value) : (uint32_t)value;
}

static bool ClassifyStrided(const int16_t *samples, uint16_t stride,
                             uint16_t frames, EventClassResult *result)
{
    if (result == NULL) { return false; }
    memset(result, 0, sizeof *result);
    if (samples == NULL || stride == 0U || frames < MIN_FRAMES) { return false; }
    const uint16_t count = frames > MAX_FRAMES ? MAX_FRAMES : frames;
    uint32_t peak = 0U;
    for (uint16_t i = 0U; i < count; ++i)
    {
        const uint32_t value = Magnitude(samples[(size_t)i * stride]);
        if (value > peak) { peak = value; }
    }
    if (peak < MIN_PEAK) { return false; }
    const uint32_t threshold = peak / 8U > 256U ? peak / 8U : 256U;
    int first = -1, last = -1;
    for (uint16_t i = 0U; i < count; ++i)
    {
        if (Magnitude(samples[(size_t)i * stride]) >= threshold)
        { if (first < 0) { first = (int)i; } last = (int)i; }
    }
    if (first < 0 || last <= first) { return false; }
    const uint16_t active = (uint16_t)(last - first + 1);
    result->active_frames = active;
    if (active < 120U) { result->valid = true; result->type = EVENT_CLASS_UNKNOWN; return true; }
    const uint16_t quarter = active / 4U;
    if (quarter == 0U) { result->valid = true; result->type = EVENT_CLASS_UNKNOWN; return true; }
    const uint16_t early_start = (uint16_t)first;
    const uint16_t early_end = (uint16_t)(first + quarter);
    const uint16_t late_start = (uint16_t)(last - quarter + 1U);
    const uint16_t late_end = (uint16_t)last;
    uint32_t early_cross = 0U, late_cross = 0U;
    uint64_t total_energy = 0U, diff_energy = 0U;
    for (uint16_t i = (uint16_t)(first + 1U); i <= (uint16_t)last; ++i)
    {
        const int32_t previous = samples[(size_t)(i - 1U) * stride];
        const int32_t current = samples[(size_t)i * stride];
        const uint64_t diff = (uint64_t)(current - previous);
        diff_energy += diff * diff;
        total_energy += (uint64_t)((int32_t)current * (int32_t)current);
        if ((previous < 0 && current >= 0) || (previous >= 0 && current < 0))
        {
            if (i >= early_start && i < early_end) { ++early_cross; }
            if (i >= late_start && i <= late_end) { ++late_cross; }
        }
    }
    if (total_energy == 0U) { return false; }
    result->zcr_early = (uint16_t)((early_cross * 1000U) / quarter);
    result->zcr_late = (uint16_t)((late_cross * 1000U) / quarter);
    result->high_ratio = (uint16_t)((diff_energy * 1000U) / total_energy);
    /* The selected standard chirp is 50 ms and sweeps upward: require a long
       active span and a clear rise in zero-crossing rate. A short/hard clap
       fails the span/trend test even though it is broadband. */
    const bool upward = result->zcr_late > result->zcr_early + 30U;
    if (active >= 1200U && upward) { result->type = EVENT_CLASS_CHIRP; }
    else if (active < 1200U || !upward) { result->type = EVENT_CLASS_CLAP; }
    else { result->type = EVENT_CLASS_UNKNOWN; }
    const uint16_t span_quality = active >= MAX_FRAMES ? 80U :
        (uint16_t)((uint32_t)active * 80U / MAX_FRAMES);
    const uint16_t trend_quality = upward ? 20U : 0U;
    result->quality = (uint8_t)(span_quality + trend_quality > 100U ? 100U :
        span_quality + trend_quality);
    result->valid = true;
    return true;
}

bool EventClassifier_Classify(const int16_t *samples, uint16_t frames,
                               EventClassResult *result)
{
    return ClassifyStrided(samples, 1U, frames, result);
}

bool EventClassifier_ClassifyStrided(const int16_t *samples, uint16_t stride,
                                      uint16_t frames, EventClassResult *result)
{
    return ClassifyStrided(samples, stride, frames, result);
}
