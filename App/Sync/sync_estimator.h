#ifndef SBCG_SYNC_ESTIMATOR_H
#define SBCG_SYNC_ESTIMATOR_H

#include <stdint.h>

/* Current PTP domain: uint32_t seconds plus decimal nanoseconds. */
#define SYNC_ESTIMATOR_MAX_TIMESTAMP_NS UINT64_C(4294967295999999999)

typedef enum
{
    SYNC_ESTIMATOR_OK,
    SYNC_ESTIMATOR_BAD_ARGUMENT,
    SYNC_ESTIMATOR_OUT_OF_RANGE,
    SYNC_ESTIMATOR_BAD_ORDER,
    SYNC_ESTIMATOR_WINDOW_EXCEEDED,
    SYNC_ESTIMATOR_NEGATIVE_DELAY
} SyncEstimatorStatus;

typedef struct
{
    uint64_t t1_a_tx_ns;
    uint64_t t2_b_rx_ns;
    uint64_t t3_b_tx_ns;
    uint64_t t4_a_rx_ns;
} SyncExchange;

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
SyncEstimatorStatus SyncEstimator_Calculate(const SyncExchange *exchange,
    uint64_t max_exchange_ns, SyncEstimate *result);

#endif
