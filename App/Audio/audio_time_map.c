/**
 * @file        audio_time_map.c
 * @brief       由两锚点估计目标帧进度的本地PTP模型时间。
 * @details     校验会话、顺序、时间窗口和速率区间，在所有端点相位均夹住目标时作线性插值，输出纳秒估计及边界。
 * @note        纯计算；调用方隔离板卡与启动域，输入输出存储不重叠；边界不覆盖变速、编解码器延迟或同步误差。
 */
#include "audio_time_map.h"
#include <stddef.h>
#include <string.h>

#define NS_PER_SEC UINT64_C(1000000000)
#define NS_PER_SEC_X_MILLI UINT64_C(1000000000000)

/**
 * @brief   检查锚点标志、纳秒范围及帧区间宽度。
 * @param   anchor 只读锚点，不可为NULL，调用方先检查指针。
 * @return  满足核心局部约束返回true，否则false。
 * @note    会话归属与两点顺序由估计函数继续校验。
 */
static bool ValidAnchor(const AudioTimeMapAnchor *anchor)
{
    return anchor->valid && anchor->nsec < NS_PER_SEC &&
           anchor->frame_hi >= anchor->frame_lo &&
           anchor->frame_hi - anchor->frame_lo <= AUDIO_TIME_MAP_MAX_WIDTH_FRAMES;
}

/**
 * @brief   由两锚点估计被夹住的帧进度所对应的模型时间。
 * @param   left 左端只读锚点，不可为NULL。
 * @param   right 右端只读锚点，不可为NULL，须与左端同板同启动域。
 * @param   target_session 非零目标采集会话，必须匹配两端。
 * @param   frame_position NDTR传输进度的整数帧坐标，并非PCM声学时刻。
 * @param   result 调用方可写结果，不可为NULL且不与输入重叠；失败清零非NULL输出。
 * @return  OK表示模型输入有效；其余枚举说明拒绝原因。
 * @note    纯函数，不外推；纳秒边界仅含线性进度假设下相位量化和取整，不含变速、编解码器/SAI/FIFO延迟或同步误差。
 */
AudioTimeMapStatus AudioTimeMap_Estimate(const AudioTimeMapAnchor *left,
                                        const AudioTimeMapAnchor *right,
                                        uint32_t target_session,
                                        uint64_t frame_position,
                                        AudioTimeMapResult *result)
{
    uint64_t time_left_ns, time_right_ns, elapsed_ns;
    uint64_t upper_left, upper_right, min_delta, max_delta;
    uint64_t early_product, late_product, early_ns, late_ns, late_denominator;

    if (result == NULL)
    {
        return AUDIO_TIME_MAP_BAD_ARGUMENT;
    }
    memset(result, 0, sizeof *result);
    if (left == NULL || right == NULL)
    {
        return AUDIO_TIME_MAP_BAD_ARGUMENT;
    }
    if (!ValidAnchor(left) || !ValidAnchor(right))
    {
        return AUDIO_TIME_MAP_INVALID_ANCHOR;
    }
    if (target_session == 0U || left->session != target_session ||
        right->session != target_session)
    {
        return AUDIO_TIME_MAP_SESSION;
    }
    if (left->frame_hi == UINT64_MAX || right->frame_hi == UINT64_MAX)
    {
        return AUDIO_TIME_MAP_RANGE;
    }
    upper_left = left->frame_hi + 1U;
    upper_right = right->frame_hi + 1U;
    /* uint32 seconds and <1e9 nanoseconds always fit in uint64 nanoseconds. */
    time_left_ns = (uint64_t)left->sec * NS_PER_SEC + left->nsec;
    time_right_ns = (uint64_t)right->sec * NS_PER_SEC + right->nsec;
    if (time_right_ns <= time_left_ns || right->frame_lo <= upper_left)
    {
        return AUDIO_TIME_MAP_ORDER;
    }
    elapsed_ns = time_right_ns - time_left_ns;
    if (elapsed_ns < AUDIO_TIME_MAP_MIN_WINDOW_NS ||
        elapsed_ns > AUDIO_TIME_MAP_MAX_WINDOW_NS)
    {
        return AUDIO_TIME_MAP_WINDOW;
    }
    min_delta = right->frame_lo - upper_left;
    max_delta = upper_right - left->frame_lo;
    if (max_delta > AUDIO_TIME_MAP_MAX_SPAN_FRAMES)
    {
        return AUDIO_TIME_MAP_RANGE;
    }
    /* Each product <=5e17 after window/span checks; no absolute frame index
       is multiplied. The entire endpoint-rate interval must pass the band. */
    if (min_delta * NS_PER_SEC_X_MILLI <
            elapsed_ns * AUDIO_TIME_MAP_MIN_RATE_MILLI_HZ ||
        max_delta * NS_PER_SEC_X_MILLI >
            elapsed_ns * AUDIO_TIME_MAP_MAX_RATE_MILLI_HZ)
    {
        return AUDIO_TIME_MAP_RATE;
    }
    if (frame_position < upper_left || frame_position > right->frame_lo)
    {
        return AUDIO_TIME_MAP_OUTSIDE;
    }
    /* The interpolation fraction decreases as either endpoint phase increases.
       Therefore both upper phases give the earliest time, both lower phases
       the latest time. Close half-open bins conservatively before rounding. */
    early_product = elapsed_ns * (frame_position - upper_left);
    late_product = elapsed_ns * (frame_position - left->frame_lo);
    late_denominator = right->frame_lo - left->frame_lo;
    early_ns = early_product / (upper_right - upper_left);
    late_ns = late_product / late_denominator;
    if (late_product % late_denominator != 0U)
    {
        ++late_ns;
    }
    /* Both offsets are within elapsed_ns. Adding to time_left cannot overflow
       because the valid right PTP timestamp is the absolute upper limit. */
    result->model_lower_ns = time_left_ns + early_ns;
    result->model_upper_ns = time_left_ns + late_ns;
    result->estimate_ns = result->model_lower_ns +
                         (result->model_upper_ns - result->model_lower_ns) / 2U;
    result->valid = true;
    return AUDIO_TIME_MAP_OK;
}
