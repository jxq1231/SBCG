#ifndef SBCG_SYNC_OBSERVATION_H
#define SBCG_SYNC_OBSERVATION_H

#include "sync_estimator.h"
#include <stdbool.h>
#include <stdint.h>

#define SYNC_OBSERVATION_CAPACITY 4U
#define SYNC_OBSERVATION_TTL_MS 2000U
#define SYNC_OBSERVATION_WINDOW_NS UINT64_C(800000000)

typedef struct
{
    uint32_t sequence;
    uint32_t client_tick_ms;
    uint32_t uid_words[3];
    uint32_t session_hi;
    uint32_t session_lo;
    uint32_t created_ms;
    SyncExchange exchange;
} SyncObservationInput;

typedef void (*SyncObservation_Log)(const char *line, int length);

/* All calls belong to the serialized main-loop context, never an ISR.
   Init clears the queue, counters and cooldowns; NULL log disables admission. */
void SyncObservation_Init(uint32_t now_ms, SyncObservation_Log log);
/* Copies the complete input without calculation, formatting or logging.
   False means NULL input, no initialized logger, or full queue. Only full
   queue rejection increments QUEUE_FULL. Upstream validates exchange keys. */
bool SyncObservation_Submit(const SyncObservationInput *input);
/* At most one dequeue per call and one result per 1000 ms. Age >= TTL is
   STATUS=6 (queue expired), not an estimator result. First result may be
   immediate. Stats have an independent 1000 ms period. No clock correction. */
void SyncObservation_Process(uint32_t now_ms);

#endif
