#include "sync_estimator.h"
#include <stddef.h>

/* Each signed first-order difference is within +/-MAX_TIMESTAMP_NS; both
   second-order sums/differences therefore fit int64_t, without saturation. */
_Static_assert(SYNC_ESTIMATOR_MAX_TIMESTAMP_NS <= (uint64_t)INT64_MAX / 2U,
               "Second-order timestamp arithmetic must fit int64_t");

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
