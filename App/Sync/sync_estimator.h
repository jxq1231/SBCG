/**
 * @file        sync_estimator.h
 * @brief       依据一次配对的四时刻交换计算时差和平均路径延迟。
 * @details     对外定义本模块的接口与回调约定。t1/t4属于A时钟，t2/t3属于B时钟；先校验范围、同钟顺序和交换窗口，再输出以两倍纳秒表示的精确整数分子。
 * @note        纯算术，无硬件访问或时钟校正；正时差表示B领先A，路径非对称误差不会被检测或修正。
 */

#ifndef SBCG_SYNC_ESTIMATOR_H
#define SBCG_SYNC_ESTIMATOR_H

#include <stdint.h>

/* Current PTP domain: uint32_t seconds plus decimal nanoseconds. */
#define SYNC_ESTIMATOR_MAX_TIMESTAMP_NS UINT64_C(4294967295999999999)

/** @brief 四时刻纯算术估计的成功状态及校验失败原因。 */
typedef enum
{
    SYNC_ESTIMATOR_OK,
    SYNC_ESTIMATOR_BAD_ARGUMENT,
    SYNC_ESTIMATOR_OUT_OF_RANGE,
    SYNC_ESTIMATOR_BAD_ORDER,
    SYNC_ESTIMATOR_WINDOW_EXCEEDED,
    SYNC_ESTIMATOR_NEGATIVE_DELAY
} SyncEstimatorStatus;

/** @brief 一次匹配交换的四个时刻；各时钟在交换中须连续且不回绕。 */
typedef struct
{
    uint64_t t1_a_tx_ns; /**< A发送请求的时刻，单位纳秒。 */
    uint64_t t2_b_rx_ns; /**< B接收请求的时刻，单位纳秒。 */
    uint64_t t3_b_tx_ns; /**< B发送应答的时刻，单位纳秒。 */
    uint64_t t4_a_rx_ns; /**< A接收应答的时刻，单位纳秒。 */
} SyncExchange;

/** @brief 估计结果以两倍纳秒保存，保留半纳秒算术分子，不保证半纳秒测量精度。 */
typedef struct
{
    /* Twice nanoseconds: + means B leads A; -1 represents -0.5 ns. */
    int64_t offset_x2_ns;
    int64_t mean_path_delay_x2_ns;
} SyncEstimate;

/* Pure arithmetic, no clock correction. Caller supplies one matched exchange
   with continuous clocks (no reboot/step/rollover) and validated timestamps.
   Bounds apply to each clock's elapsed exchange, not cross-clock differences.
   All failures leave a non-NULL result unchanged. Path asymmetry is not detected
   or corrected; successful calculation does not establish synchronization. */
/**
 * @brief   校验一次四时刻交换并计算B相对A的时差及平均路径延迟。
 * @param   exchange 不可为NULL的只读匹配交换；t1/t4为A、t2/t3为B时钟的纳秒值，函数不保存指针。
 * @param   max_exchange_ns 每个时钟域内允许的最大交换历时，单位纳秒；须大于0且不超过最大时间戳。
 * @param   result 不可为NULL的输出；成功写入两倍纳秒值，任一失败保持原内容。
 * @return  SYNC_ESTIMATOR_OK表示已写入结果；其余枚举指出参数、范围、顺序、窗口或负延迟错误。
 * @note    调用方保证时钟连续且交换配对有效；offset_x2=(t2-t1)-(t4-t3)，delay_x2=(t2-t1)+(t4-t3)。不修正时钟或链路非对称。
 */
SyncEstimatorStatus SyncEstimator_Calculate(const SyncExchange *exchange,
    uint64_t max_exchange_ns, SyncEstimate *result);

#endif
