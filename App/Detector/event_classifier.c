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
/* 拍掌是短促、宽带、起始峰后快速衰减的瞬态；这些门限只使用当前窗口内
   可观测的包络/差分能量，不依赖标准音模板。持续纯音和截断扫频即使不是
   上扫 chirp，也不得因为没有其他类别就兜底成拍掌。 */
#define CLAP_MAX_ACTIVE_FRAMES 1000U
#define CLAP_MIN_HIGH_RATIO 120U
#define CLAP_MIN_DECAY_NUM 2U

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
    uint16_t peak_index = 0U;
    for (uint16_t i = 0U; i < count; ++i)
    {
        const uint32_t value = Magnitude(samples[(size_t)i * stride]);
        if (value > peak) { peak = value; peak_index = i; }
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
    uint64_t early_energy = 0U, late_energy = 0U;
    for (uint16_t i = (uint16_t)(first + 1U); i <= (uint16_t)last; ++i)
    {
        const int32_t previous = samples[(size_t)(i - 1U) * stride];
        const int32_t current = samples[(size_t)i * stride];
        const uint64_t diff = (uint64_t)(current - previous);
        diff_energy += diff * diff;
        total_energy += (uint64_t)((int32_t)current * (int32_t)current);
        if (i >= early_start && i < early_end) { early_energy += (uint64_t)((int32_t)current * (int32_t)current); }
        if (i >= late_start && i <= late_end) { late_energy += (uint64_t)((int32_t)current * (int32_t)current); }
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
    /* 选定的标准音是50ms上扫chirp，要求活动段足够长且过零率明显上升。 */
    const bool upward = result->zcr_late > result->zcr_early + 30U;
    /* 独立拍掌准入：短活动段、宽频差分能量、峰值靠近起始、包络明显衰减。
       不满足时保留UNKNOWN，绝不把“既不是chirp”直接当成拍掌。 */
    const bool short_span = active <= CLAP_MAX_ACTIVE_FRAMES;
    const bool broadband = result->high_ratio >= CLAP_MIN_HIGH_RATIO;
    const bool initial_peak = (uint16_t)peak_index <= (uint16_t)(first + quarter);
    const bool decays = late_energy == 0U ||
        early_energy >= (uint64_t)CLAP_MIN_DECAY_NUM * late_energy;
    if (short_span && broadband && initial_peak && decays)
    { result->type = EVENT_CLASS_CLAP; }
    else if (active >= 1200U && upward)
    { result->type = EVENT_CLASS_CHIRP; }
    else
    { result->type = EVENT_CLASS_UNKNOWN; }
    if (result->type == EVENT_CLASS_CLAP)
    {
        uint16_t quality = 55U;
        quality = (uint16_t)(quality + (result->high_ratio >= 1000U ? 25U :
            (result->high_ratio >= 500U ? 20U : 10U)));
        if (late_energy != 0U && early_energy >= UINT64_C(4) * late_energy) { quality += 15U; }
        result->quality = (uint8_t)(quality > 100U ? 100U : quality);
    }
    else
    {
        const uint16_t span_quality = active >= MAX_FRAMES ? 80U :
            (uint16_t)((uint32_t)active * 80U / MAX_FRAMES);
        const uint16_t trend_quality = upward ? 20U : 0U;
        result->quality = (uint8_t)(span_quality + trend_quality > 100U ? 100U :
            span_quality + trend_quality);
    }
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
