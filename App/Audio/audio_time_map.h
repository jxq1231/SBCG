/**
 * @file        audio_time_map.h
 * @brief       由两锚点估计目标帧进度的本地PTP模型时间。
 * @details     声明本模块接口、数据类型或配置常量。校验会话、顺序、时间窗口和速率区间，在所有端点相位均夹住目标时作线性插值，输出纳秒估计及边界。
 * @note        纯计算；调用方隔离板卡与启动域，输入输出存储不重叠；边界不覆盖变速、编解码器延迟或同步误差。
 */
#ifndef SBCG_AUDIO_TIME_MAP_H
#define SBCG_AUDIO_TIME_MAP_H

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_TIME_MAP_MAX_WIDTH_FRAMES 4U
#define AUDIO_TIME_MAP_MIN_WINDOW_NS UINT64_C(100000000)
#define AUDIO_TIME_MAP_MAX_WINDOW_NS UINT64_C(10000000000)
#define AUDIO_TIME_MAP_MAX_SPAN_FRAMES UINT64_C(500000)
#define AUDIO_TIME_MAP_Q16_ONE 65536U
/* Outward-rounded millihertz limits around derived 47991.071 Hz +/-1%.
   Sanity limits only: neither instantaneous rate nor acoustic calibration. */
#define AUDIO_TIME_MAP_MIN_RATE_MILLI_HZ UINT64_C(47511160)
#define AUDIO_TIME_MAP_MAX_RATE_MILLI_HZ UINT64_C(48470982)

/**
 * @brief   单个本地PTP时刻对应的NDTR帧进度夹读区间。
 * @note    session仅在当前板卡本次启动内有意义；sec/nsec为PTP秒/纳秒，端点相位按[frame_lo,frame_hi+1]保守闭合。
 */
typedef struct
{
    bool valid;
    uint32_t session;
    uint64_t frame_lo;
    uint64_t frame_hi;
    uint32_t sec;
    uint32_t nsec;
} AudioTimeMapAnchor;

/**
 * @brief   本地纳秒模型估计及相位量化边界。
 * @note    valid只表示输入满足模型条件，estimate_ns为上下界中点，不证明物理采样时刻或跨板同步。
 */
typedef struct
{
    bool valid;
    uint64_t estimate_ns;
    uint64_t model_lower_ns;
    uint64_t model_upper_ns;
} AudioTimeMapResult;

/** @brief 插值拒绝原因；OK为成功，其余状态均使非NULL结果清零。 */
typedef enum
{
    AUDIO_TIME_MAP_OK = 0,
    AUDIO_TIME_MAP_BAD_ARGUMENT,
    AUDIO_TIME_MAP_INVALID_ANCHOR,
    AUDIO_TIME_MAP_SESSION,
    AUDIO_TIME_MAP_ORDER,
    AUDIO_TIME_MAP_WINDOW,
    AUDIO_TIME_MAP_RATE,
    AUDIO_TIME_MAP_OUTSIDE,
    AUDIO_TIME_MAP_RANGE,
    AUDIO_TIME_MAP_BAD_POSITION,
    AUDIO_TIME_MAP_Q16_RANGE
} AudioTimeMapStatus;

/** @brief NDTR模型进度坐标的Q16子帧表示；frame为整数进度，fraction_q16为1/65536帧。 */
typedef struct
{
    uint64_t frame;
    uint16_t fraction_q16;
} AudioTimeMapPosition;

/* Pure function; no HAL, allocation, peripheral access or global state.
   Caller isolates a single BOOT/board before invoking this function: SESSION
   alone is not a globally unique epoch. Input/output storage must not overlap.

   frame_position is an integer coordinate of continuous NDTR frame progress.
   It is NOT a PCM sample's physical/acoustic timestamp. Each input closes its
   phase bin to [frame_lo, frame_hi+1]. Output bounds assume linear progress
   between anchors and cover only phase quantization/integer rounding; unknown
   in-window rate changes, codec/SAI/FIFO delays and synchronization are excluded.
   No extrapolation: target must be bracketed for every possible endpoint phase.
   On failure, a non-NULL result is cleared; valid means model-input validity. */
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
                                        AudioTimeMapResult *result);
/**
 * @brief   以整数帧+Q16子帧坐标估计被夹住的NDTR进度对应模型时间。
 * @param   left/right 同板同启动域、同会话的只读锚点，不可为NULL。
 * @param   target_session 非零目标会话，必须匹配两端。
 * @param   position 非NULL只读Q16坐标；frame为整数进度，fraction_q16须小于65536。
 * @param   result 非NULL可写输出；失败清零，成功给出纳秒中值及相位边界。
 * @return  OK或既有/新增拒绝原因；Q16=0时逐项等价于AudioTimeMap_Estimate。
 * @note    仅映射量化后的Q16坐标；一LSB的Q16分辨率不并入LO/HI，也不代表物理采样或声学精度。
 */
AudioTimeMapStatus AudioTimeMap_EstimateQ16(const AudioTimeMapAnchor *left,
                                           const AudioTimeMapAnchor *right,
                                           uint32_t target_session,
                                           const AudioTimeMapPosition *position,
                                           AudioTimeMapResult *result);

#endif
