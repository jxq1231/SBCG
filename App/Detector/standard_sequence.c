/**
 * @file        standard_sequence.c
 * @brief       已选标准声周期事件串检测实现。
 * @details     相邻事件间隔匹配period的整数倍且误差不超过tolerance才继续；超gap/迭代失败/低质量重置。
 * @note        只做周期结构统计；重复击掌也可能满足间隔，不能单独作为标准声身份结论。
 */
#include "standard_sequence.h"
#include <stddef.h>
#include <string.h>

static uint64_t AbsDiff(uint64_t a, uint64_t b)
{
    return a >= b ? a - b : b - a;
}

static void ClearState(StandardSequence *sequence)
{
    sequence->state = (StandardSequenceSnapshot){0};
    sequence->have_last = false;
    sequence->last_time_ns = 0U;
}

bool StandardSequence_Init(StandardSequence *sequence, const StandardSequenceConfig *config)
{
    if (sequence == NULL) { return false; }
    memset(sequence, 0, sizeof *sequence);
    if (config == NULL || config->period_ns == 0U || config->tolerance_ns == 0U ||
        config->tolerance_ns > config->period_ns / 2U ||
        config->reset_gap_ns <= config->period_ns ||
        config->min_events == 0U || config->min_events > STANDARD_SEQUENCE_MAX_EVENTS ||
        config->quality_min > 100U) { return false; }
    sequence->config = *config;
    return true;
}

void StandardSequence_Reset(StandardSequence *sequence)
{
    if (sequence != NULL) { ClearState(sequence); }
}

bool StandardSequence_Push(StandardSequence *sequence, uint64_t time_ns, uint8_t quality)
{
    if (sequence == NULL || sequence->config.period_ns == 0U) { return false; }
    if (quality < sequence->config.quality_min)
    {
        ClearState(sequence);
        return false;
    }
    if (!sequence->have_last)
    {
        sequence->have_last = true;
        sequence->last_time_ns = time_ns;
        sequence->state = (StandardSequenceSnapshot){
            false, 1U, quality, time_ns, 0U};
        return false;
    }
    const uint64_t delta = time_ns >= sequence->last_time_ns ?
        time_ns - sequence->last_time_ns : UINT64_MAX;
    if (delta > sequence->config.reset_gap_ns)
    {
        ClearState(sequence);
        sequence->have_last = true;
        sequence->last_time_ns = time_ns;
        sequence->state = (StandardSequenceSnapshot){false, 1U, quality, time_ns, 0U};
        return false;
    }
    const uint64_t period = sequence->config.period_ns;
    const uint64_t multiple = (delta + period / 2U) / period;
    bool matches = false;
    uint64_t expected = 0U;
    if (multiple >= 1U && multiple <= 4U)
    {
        expected = multiple * period;
        matches = AbsDiff(delta, expected) <= sequence->config.tolerance_ns;
    }
    if (!matches)
    {
        ClearState(sequence);
        sequence->have_last = true;
        sequence->last_time_ns = time_ns;
        sequence->state = (StandardSequenceSnapshot){false, 1U, quality, time_ns, 0U};
        return false;
    }
    const uint64_t error = AbsDiff(delta, expected);
    if (sequence->state.events < STANDARD_SEQUENCE_MAX_EVENTS)
    { ++sequence->state.events; }
    if (quality < sequence->state.quality) { sequence->state.quality = quality; }
    if (error > sequence->state.max_period_error_ns)
    { sequence->state.max_period_error_ns = error; }
    sequence->state.last_time_ns = time_ns;
    sequence->last_time_ns = time_ns;
    sequence->state.detected = sequence->state.events >= sequence->config.min_events;
    return sequence->state.detected;
}

void StandardSequence_Get(const StandardSequence *sequence, StandardSequenceSnapshot *out)
{
    if (out == NULL) { return; }
    if (sequence == NULL) { *out = (StandardSequenceSnapshot){0}; return; }
    *out = sequence->state;
}
