/**
 * @file        sync_estimator.c
 * @brief       依据一次配对的四时刻交换计算时差和平均路径延迟。
 * @details     t1/t4属于A时钟，t2/t3属于B时钟；先校验范围、同钟顺序和交换窗口，再输出以两倍纳秒表示的精确整数分子。
 * @note        纯算术，无硬件访问或时钟校正；正时差表示B领先A，路径非对称误差不会被检测或修正。
 */

#include "sync_estimator.h"
#include <stddef.h>

/* Each signed first-order difference is within +/-MAX_TIMESTAMP_NS; both
   second-order sums/differences therefore fit int64_t, without saturation. */
_Static_assert(SYNC_ESTIMATOR_MAX_TIMESTAMP_NS <= (uint64_t)INT64_MAX / 2U,
               "Second-order timestamp arithmetic must fit int64_t");

/**
 * @brief   校验一次四时刻交换并计算B相对A的时差及平均路径延迟。
 * @param   exchange 不可为NULL的只读匹配交换；t1/t4为A、t2/t3为B时钟的纳秒值，函数不保存指针。
 * @param   max_exchange_ns 每个时钟域内允许的最大交换历时，单位纳秒；须大于0且不超过最大时间戳。
 * @param   result 不可为NULL的输出；成功写入两倍纳秒值，任一失败保持原内容。
 * @return  SYNC_ESTIMATOR_OK表示已写入结果；其余枚举指出参数、范围、顺序、窗口或负延迟错误。
 * @note    调用方保证时钟连续且交换配对有效；offset_x2=(t2-t1)-(t4-t3)，delay_x2=(t2-t1)+(t4-t3)。不修正时钟或链路非对称。
 */
SyncEstimatorStatus SyncEstimator_Calculate(const SyncExchange *exchange,
    uint64_t max_exchange_ns, SyncEstimate *result)
{
    if (exchange == NULL || result == NULL) { return SYNC_ESTIMATOR_BAD_ARGUMENT; }
    if (max_exchange_ns == 0U || max_exchange_ns > SYNC_ESTIMATOR_MAX_TIMESTAMP_NS ||
        exchange->t1_a_tx_ns > SYNC_ESTIMATOR_MAX_TIMESTAMP_NS ||
        exchange->t2_b_rx_ns > SYNC_ESTIMATOR_MAX_TIMESTAMP_NS ||
        exchange->t3_b_tx_ns > SYNC_ESTIMATOR_MAX_TIMESTAMP_NS ||
        exchange->t4_a_rx_ns > SYNC_ESTIMATOR_MAX_TIMESTAMP_NS)
    { return SYNC_ESTIMATOR_OUT_OF_RANGE; }
    if (exchange->t4_a_rx_ns < exchange->t1_a_tx_ns ||
        exchange->t3_b_tx_ns < exchange->t2_b_rx_ns)
    { return SYNC_ESTIMATOR_BAD_ORDER; }
    if (exchange->t4_a_rx_ns - exchange->t1_a_tx_ns > max_exchange_ns ||
        exchange->t3_b_tx_ns - exchange->t2_b_rx_ns > max_exchange_ns)
    { return SYNC_ESTIMATOR_WINDOW_EXCEEDED; }

    /* Cast only after range checks; cross-clock differences may be negative. */
    const int64_t forward_ns = (int64_t)exchange->t2_b_rx_ns - (int64_t)exchange->t1_a_tx_ns;
    const int64_t reverse_ns = (int64_t)exchange->t4_a_rx_ns - (int64_t)exchange->t3_b_tx_ns;
    const int64_t delay_x2_ns = forward_ns + reverse_ns;
    if (delay_x2_ns < 0) { return SYNC_ESTIMATOR_NEGATIVE_DELAY; }

    /* Positive offset means B leads A. Keep the exact numerators, including
       half-nanosecond results; this does not imply sub-nanosecond accuracy. */
    *result = (SyncEstimate){forward_ns - reverse_ns, delay_x2_ns};
    return SYNC_ESTIMATOR_OK;
}
