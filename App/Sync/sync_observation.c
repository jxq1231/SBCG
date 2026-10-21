#include "sync_observation.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define OBSERVATION_PERIOD_MS 1000U
#define OBSERVATION_LINE_SIZE 256U
#define OBSERVATION_QUEUE_EXPIRED 6U

/* CPU-only storage, used serially from the NO_SYS main loop. No DMA shares
   this queue, and no clock/PTP state is changed by observation. */
static struct
{
    SyncObservationInput queue[SYNC_OBSERVATION_CAPACITY];
    uint32_t head;
    uint32_t count;
    uint32_t last_result_ms;
    uint32_t last_stats_ms;
    uint32_t queued;
    uint32_t calc_ok;
    uint32_t calc_err;
    uint32_t expired;
    uint32_t queue_full;
    uint32_t format_err;
    SyncObservation_Log log;
    bool initialized;
} observation;

/* newlib-nano need not support 64-bit printf. Convert the magnitude using
   unsigned arithmetic: -(INT64_MIN) would overflow a signed integer. */
static void FormatSigned64(char text[21], int64_t value)
{
    char reversed[20];
    size_t digits = 0U;
    size_t length = 0U;
    uint64_t magnitude = (value < 0) ? UINT64_C(0) - (uint64_t)value : (uint64_t)value;
    do
    {
        reversed[digits++] = (char)('0' + magnitude % UINT64_C(10));
        magnitude /= UINT64_C(10);
    } while (magnitude != 0U);
    if (value < 0) { text[length++] = '-'; }
    while (digits != 0U) { text[length++] = reversed[--digits]; }
    text[length] = '\0';
}

static int FormatObservation(char *line, size_t capacity,
                             const SyncObservationInput *input,
                             unsigned int status, const SyncEstimate *result)
{
    char values[80] = "";
    if (status == SYNC_ESTIMATOR_OK)
    {
        char offset_text[21];
        char delay_text[21];
        FormatSigned64(offset_text, result->offset_x2_ns);
        FormatSigned64(delay_text, result->mean_path_delay_x2_ns);
        const int length = snprintf(values, sizeof(values),
            ",OFFSET_X2_NS=%s,DELAY_X2_NS=%s", offset_text, delay_text);
        if (length < 0 || (size_t)length >= sizeof(values)) { return -1; }
    }
    return snprintf(line, capacity,
        "$SYNC_OBS,SESSION=%08" PRIX32 "%08" PRIX32
        ",REQ_SEQ=%" PRIu32 ",CLIENT_TICK_MS=%" PRIu32
        ",UID_W0=%08" PRIX32 ",UID_W1=%08" PRIX32 ",UID_W2=%08" PRIX32
        ",STATUS=%u%s,SYNC=0,PC=SW,BOARD=HW\r\n",
        input->session_hi, input->session_lo, input->sequence,
        input->client_tick_ms, input->uid_words[0], input->uid_words[1],
        input->uid_words[2], status, values);
}

static void EmitLine(const char *line, int length)
{
    if (length <= 0 || (size_t)length >= OBSERVATION_LINE_SIZE)
    {
        ++observation.format_err;
        return;
    }
    observation.log(line, length);
}

void SyncObservation_Init(uint32_t now_ms, SyncObservation_Log log)
{
    memset(&observation, 0, sizeof(observation));
    observation.log = log;
    observation.initialized = true;
    observation.last_result_ms = now_ms - OBSERVATION_PERIOD_MS;
    observation.last_stats_ms = now_ms;
}

bool SyncObservation_Submit(const SyncObservationInput *input)
{
    if (!observation.initialized || observation.log == NULL || input == NULL)
    {
        return false;
    }
    if (observation.count == SYNC_OBSERVATION_CAPACITY)
    {
        ++observation.queue_full;
        return false;
    }
    const uint32_t tail = (observation.head + observation.count) % SYNC_OBSERVATION_CAPACITY;
    observation.queue[tail] = *input;
    ++observation.count;
    ++observation.queued;
    return true;
}

void SyncObservation_Process(uint32_t now_ms)
{
    if (!observation.initialized || observation.log == NULL) { return; }
    char line[OBSERVATION_LINE_SIZE];
    if (observation.count != 0U &&
        (uint32_t)(now_ms - observation.last_result_ms) >= OBSERVATION_PERIOD_MS)
    {
        const SyncObservationInput input = observation.queue[observation.head];
        observation.head = (observation.head + 1U) % SYNC_OBSERVATION_CAPACITY;
        --observation.count;
        observation.last_result_ms = now_ms;
        SyncEstimate result;
        unsigned int status;
        if ((uint32_t)(now_ms - input.created_ms) >= SYNC_OBSERVATION_TTL_MS)
        {
            status = OBSERVATION_QUEUE_EXPIRED;
            ++observation.expired;
        }
        else
        {
            status = (unsigned int)SyncEstimator_Calculate(&input.exchange,
                SYNC_OBSERVATION_WINDOW_NS, &result);
            if (status == SYNC_ESTIMATOR_OK) { ++observation.calc_ok; }
            else { ++observation.calc_err; }
        }
        EmitLine(line, FormatObservation(line, sizeof(line), &input, status,
                                        status == SYNC_ESTIMATOR_OK ? &result : NULL));
    }
    if ((uint32_t)(now_ms - observation.last_stats_ms) >= OBSERVATION_PERIOD_MS)
    {
        observation.last_stats_ms = now_ms;
        EmitLine(line, snprintf(line, sizeof(line),
            "$SYNC_OBS_STATS,QUEUED=%" PRIu32 ",CALC_OK=%" PRIu32
            ",CALC_ERR=%" PRIu32 ",EXPIRED=%" PRIu32 ",QUEUE_FULL=%" PRIu32
            ",FORMAT_ERR=%" PRIu32 "\r\n",
            observation.queued, observation.calc_ok, observation.calc_err,
            observation.expired, observation.queue_full, observation.format_err));
    }
}
