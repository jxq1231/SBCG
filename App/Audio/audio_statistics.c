#include "audio_statistics.h"
#include <limits.h>
#include <stddef.h>

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
