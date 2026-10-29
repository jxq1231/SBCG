/**
 * @file        standard_sequence.h
 * @brief       已选标准声的周期性事件串检测（纯C，无音频/HAL/网络）。
 * @details     标准声定义为10个约2s间隔的50ms chirp事件；本模块只验证事件到达时间的
 *              周期性/连续性，输出候选串、事件数、周期误差和质量最小值。
 * @note        周期性不等于标准声身份：单次击掌不会触发，但重复击掌在同样间隔下也可能
 *              被标为候选；最终标准声/击掌分类仍需波形/质量门控。不得把本结果当识别通过。
 */
#ifndef SBCG_STANDARD_SEQUENCE_H
#define SBCG_STANDARD_SEQUENCE_H

#include <stdbool.h>
#include <stdint.h>

#define STANDARD_SEQUENCE_PERIOD_NS UINT64_C(2000000000)
#define STANDARD_SEQUENCE_MAX_EVENTS 10U

/** @brief 配置；参数非法时Init返回false。 */
typedef struct
{
    uint64_t period_ns;
    uint64_t tolerance_ns;
    uint64_t reset_gap_ns;
    uint8_t min_events;
    uint8_t quality_min;
} StandardSequenceConfig;

/** @brief 只读序列状态；detected仅在事件数达到min_events时true。 */
typedef struct
{
    bool detected;
    uint8_t events;
    uint8_t quality;
    uint64_t last_time_ns;
    uint64_t max_period_error_ns;
} StandardSequenceSnapshot;

/** @brief 前台单消费者状态；无动态内存。 */
typedef struct
{
    StandardSequenceConfig config;
    StandardSequenceSnapshot state;
    bool have_last;
    uint64_t last_time_ns;
} StandardSequence;

/** @brief 初始化并复制配置；非法参数返回false并清空。 */
bool StandardSequence_Init(StandardSequence *sequence, const StandardSequenceConfig *config);

/** @brief 提交一个事件时间/质量；返回本次是否已形成达到门槛的周期串。 */
bool StandardSequence_Push(StandardSequence *sequence, uint64_t time_ns, uint8_t quality);

/** @brief 清空当前串，保留配置；无返回值。 */
void StandardSequence_Reset(StandardSequence *sequence);

/** @brief 复制当前状态；NULL不操作。 */
void StandardSequence_Get(const StandardSequence *sequence, StandardSequenceSnapshot *out);

#endif
