/* Include the real implementation so private formatting can be checked at
   int64 extrema without extending the firmware's public interface. */
#include "Sync/sync_observation.c"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static char lines[32][256];
static unsigned int line_count;

static void Capture(const char *line, int length)
{
    assert(length > 0 && length < 256);
    assert(line_count < 32U);
    assert((size_t)length == strlen(line));
    assert(line[length - 2] == '\r' && line[length - 1] == '\n');
    memcpy(lines[line_count++], line, (size_t)length + 1U);
}

static void Reset(uint32_t now_ms)
{
    line_count = 0U;
    memset(lines, 0, sizeof(lines));
    SyncObservation_Init(now_ms, Capture);
}

static SyncObservationInput Input(uint32_t seq, uint32_t created_ms)
{
    return (SyncObservationInput){
        .sequence = seq, .client_tick_ms = 123U,
        .uid_words = {UINT32_C(0x12345678), UINT32_C(0x9ABCDEF0), UINT32_C(0x10203040)},
        .session_hi = UINT32_C(0x89ABCDEF), .session_lo = UINT32_C(0x01234567),
        .created_ms = created_ms,
        .exchange = {1000000000, 1000150000, 1000350000, 1000400000}
    };
}

static unsigned int Results(void)
{
    unsigned int count = 0U;
    for (unsigned int i = 0U; i < line_count; ++i)
    {
        if (strncmp(lines[i], "$SYNC_OBS,", 10U) == 0) { ++count; }
    }
    return count;
}

static const char *Result(unsigned int index)
{
    for (unsigned int i = 0U; i < line_count; ++i)
    {
        if (strncmp(lines[i], "$SYNC_OBS,", 10U) == 0)
        {
            if (index == 0U) { return lines[i]; }
            --index;
        }
    }
    assert(!"missing observation result");
    return NULL;
}

static void Admission(void)
{
    SyncObservationInput input = Input(1U, 100U);
    assert(!SyncObservation_Submit(&input));
    Reset(100U);
    assert(!SyncObservation_Submit(NULL));
    assert(SyncObservation_Submit(&input));
    assert(line_count == 0U);
    SyncObservation_Process(100U);
    assert(Results() == 1U);
    assert(strcmp(Result(0), "$SYNC_OBS,SESSION=89ABCDEF01234567,REQ_SEQ=1,"
        "CLIENT_TICK_MS=123,UID_W0=12345678,UID_W1=9ABCDEF0,UID_W2=10203040,"
        "STATUS=0,OFFSET_X2_NS=100000,DELAY_X2_NS=200000,SYNC=0,PC=SW,BOARD=HW\r\n") == 0);
    puts("PASS: admission requires initialization and copies real-estimator success schema");
}

static void QueueAndRate(void)
{
    Reset(0U);
    for (uint32_t i = 0U; i < SYNC_OBSERVATION_CAPACITY; ++i)
    {
        SyncObservationInput input = Input(i + 1U, 0U);
        assert(SyncObservation_Submit(&input));
        memset(&input, 0xFF, sizeof(input));
    }
    SyncObservationInput extra = Input(999U, 0U);
    assert(!SyncObservation_Submit(&extra));
    assert(line_count == 0U);
    SyncObservation_Process(0U);
    SyncObservation_Process(0U);
    SyncObservation_Process(999U);
    assert(Results() == 1U);
    SyncObservation_Process(1000U);
    SyncObservation_Process(1000U);
    assert(Results() == 2U);
    assert(strstr(Result(0), "REQ_SEQ=1,") != NULL);
    assert(strstr(Result(1), "REQ_SEQ=2,") != NULL);
    assert(strstr(lines[line_count - 1U], "QUEUED=4,CALC_OK=2,CALC_ERR=0,EXPIRED=0,QUEUE_FULL=1,FORMAT_ERR=0") != NULL);
    SyncObservation_Process(2000U);
    SyncObservation_Process(3000U);
    assert(Results() == 4U);
    assert(strstr(Result(2), "REQ_SEQ=3,") != NULL);
    assert(strstr(Result(3), "REQ_SEQ=4,") != NULL);
    assert(strstr(Result(2), "STATUS=6,") != NULL);
    assert(strstr(Result(3), "STATUS=6,") != NULL);
    assert(strstr(Result(2), "OFFSET_X2_NS") == NULL);
    puts("PASS: four copied FIFO entries survive caller mutation, reject overflow, rate limit and expire while queued");
}

static void Expiry(void)
{
    const uint32_t starts[] = {0U, UINT32_MAX - 999U};
    for (unsigned int i = 0U; i < 2U; ++i)
    {
        const uint32_t start = starts[i];
        Reset(start);
        SyncObservationInput input = Input(1U, start);
        assert(SyncObservation_Submit(&input));
        SyncObservation_Process(start + 1999U);
        assert(strstr(Result(0), "STATUS=0,") != NULL);
        Reset(start);
        assert(SyncObservation_Submit(&input));
        SyncObservation_Process(start + 2000U);
        assert(strstr(Result(0), "STATUS=6,") != NULL);
        assert(strstr(Result(0), "DELAY_X2_NS") == NULL);
        assert(strstr(lines[line_count - 1U], "CALC_OK=0,CALC_ERR=0,EXPIRED=1,") != NULL);
    }
    puts("PASS: original creation time expires at exact 2000 ms including uint32 wrap");
}

static void ErrorStatuses(void)
{
    const SyncExchange invalid[] = {
        {SYNC_ESTIMATOR_MAX_TIMESTAMP_NS + 1U, 0, 0, 0},
        {10, 20, 19, 30},
        {0, 0, 0, SYNC_OBSERVATION_WINDOW_NS + 1U},
        {0, 0, 100, 99}
    };
    for (unsigned int i = 0U; i < 4U; ++i)
    {
        Reset(0U);
        SyncObservationInput input = Input(1U, 0U);
        assert(SyncObservation_Submit(&input));
        SyncObservation_Process(0U);
        input.sequence = 2U;
        input.created_ms = 1000U;
        input.exchange = invalid[i];
        assert(SyncObservation_Submit(&input));
        SyncObservation_Process(1000U);
        char status[20];
        snprintf(status, sizeof(status), "STATUS=%u,", i + 2U);
        assert(strstr(Result(1), status) != NULL);
        assert(strstr(Result(1), "OFFSET_X2_NS") == NULL);
        assert(strstr(Result(1), "DELAY_X2_NS") == NULL);
        assert(strstr(lines[line_count - 1U], "CALC_OK=1,CALC_ERR=1,EXPIRED=0,") != NULL);
    }
    Reset(0U);
    SyncObservationInput input = Input(1U, 0U);
    input.exchange = (SyncExchange){0, 0, SYNC_OBSERVATION_WINDOW_NS, SYNC_OBSERVATION_WINDOW_NS};
    assert(SyncObservation_Submit(&input));
    SyncObservation_Process(0U);
    assert(strstr(Result(0), "STATUS=0,OFFSET_X2_NS=0,DELAY_X2_NS=0,") != NULL);
    puts("PASS: real estimator errors 2..5 never reuse success values; exact 800 ms window is accepted");
}

static void ResetAndStats(void)
{
    Reset(UINT32_MAX - 99U);
    SyncObservation_Process(899U);
    assert(line_count == 0U);
    SyncObservation_Process(900U);
    assert(line_count == 1U);
    assert(strcmp(lines[0], "$SYNC_OBS_STATS,QUEUED=0,CALC_OK=0,CALC_ERR=0,EXPIRED=0,QUEUE_FULL=0,FORMAT_ERR=0\r\n") == 0);
    SyncObservation_Process(900U);
    assert(line_count == 1U);
    SyncObservationInput input = Input(1U, 900U);
    assert(SyncObservation_Submit(&input));
    SyncObservation_Init(900U, NULL);
    assert(!SyncObservation_Submit(&input));
    SyncObservation_Process(9999U);
    assert(line_count == 1U);
    Reset(1000U);
    SyncObservation_Process(1000U);
    assert(line_count == 0U);
    assert(SyncObservation_Submit(&input));
    SyncObservation_Process(1000U);
    assert(Results() == 1U);
    SyncObservation_Process(2000U);
    assert(strstr(lines[line_count - 1U], "QUEUED=1,CALC_OK=1,CALC_ERR=0,EXPIRED=0,QUEUE_FULL=0,FORMAT_ERR=0") != NULL);
    puts("PASS: loggerless mode, Init reset and independent periodic stats are wrap safe");
}

static void Formatting(void)
{
    const int64_t values[] = {INT64_MIN, INT64_MAX, -1, 0, 1};
    const char *expected[] = {"-9223372036854775808", "9223372036854775807", "-1", "0", "1"};
    for (unsigned int i = 0U; i < 5U; ++i)
    {
        char text[21];
        FormatSigned64(text, values[i]);
        assert(strcmp(text, expected[i]) == 0);
    }
    Reset(0U);
    SyncObservationInput input = Input(UINT32_MAX, 0U);
    input.client_tick_ms = UINT32_MAX;
    input.session_hi = input.session_lo = UINT32_MAX;
    input.uid_words[0] = input.uid_words[1] = input.uid_words[2] = UINT32_MAX;
    const SyncEstimate result = {INT64_MIN, INT64_MAX};
    char text[256];
    const int length = FormatObservation(text, sizeof(text), &input, 0U, &result);
    assert(length > 0 && length < 256);
    assert((size_t)length == strlen(text));
    assert(strstr(text, "SESSION=FFFFFFFFFFFFFFFF,REQ_SEQ=4294967295,CLIENT_TICK_MS=4294967295,") != NULL);
    assert(strstr(text, "OFFSET_X2_NS=-9223372036854775808,DELAY_X2_NS=9223372036854775807,") != NULL);
    assert(FormatObservation(text, 16U, &input, 0U, &result) >= 16);
    for (unsigned int status = 1U; status <= 6U; ++status)
    {
        assert(FormatObservation(text, sizeof(text), &input, status, NULL) > 0);
        char field[20];
        snprintf(field, sizeof(field), "STATUS=%u,", status);
        assert(strstr(text, field) != NULL);
        assert(strstr(text, "OFFSET_X2_NS") == NULL);
        assert(strstr(text, "DELAY_X2_NS") == NULL);
        assert(strstr(text, ",SYNC=0,PC=SW,BOARD=HW\r\n") != NULL);
    }
    EmitLine(text, -1);
    EmitLine(text, 256);
    assert(line_count == 0U);
    SyncObservation_Process(1000U);
    assert(strstr(lines[0], "FORMAT_ERR=2\r\n") != NULL);
    puts("PASS: bounded int64 extrema formatting, maximum keys, all status schemas and truncation rejection");
}

static void RingAndWrap(void)
{
    const uint32_t start = UINT32_MAX - 499U;
    Reset(start);
    for (uint32_t seq = 1U; seq <= SYNC_OBSERVATION_CAPACITY; ++seq)
    {
        SyncObservationInput input = Input(seq, start);
        assert(SyncObservation_Submit(&input));
    }
    SyncObservation_Process(start);
    SyncObservationInput input = Input(5U, start);
    assert(SyncObservation_Submit(&input));
    SyncObservation_Process(start + 999U);
    assert(Results() == 1U);
    SyncObservation_Process(start + 1000U);
    SyncObservation_Process(start + 1000U);
    assert(Results() == 2U);
    for (uint32_t i = 2U; i <= 4U; ++i)
    {
        SyncObservation_Process(start + i * 1000U);
    }
    assert(Results() == 5U);
    for (unsigned int i = 0U; i < 5U; ++i)
    {
        char key[20];
        snprintf(key, sizeof(key), "REQ_SEQ=%u,", i + 1U);
        assert(strstr(Result(i), key) != NULL);
    }
    assert(strstr(Result(4), "STATUS=6,") != NULL);
    assert(strstr(lines[line_count - 1U], "QUEUED=5,CALC_OK=2,CALC_ERR=0,EXPIRED=3,") != NULL);
    puts("PASS: queue tail reuses freed slots without overwrite; result cooldown crosses uint32 wrap");
}

int main(void)
{
    Admission();
    QueueAndRate();
    Expiry();
    ErrorStatuses();
    ResetAndStats();
    Formatting();
    RingAndWrap();
    puts("SUMMARY: 7 observation groups passed (real estimator linked unchanged)");
    return 0;
}
