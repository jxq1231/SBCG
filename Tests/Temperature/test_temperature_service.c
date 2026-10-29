#include "Temperature/temperature_service.h"
#include "Temperature/temperature_config.h"
#include "BSP/bsp_dht11.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static const uint32_t allowed_uid[3] = {
    TEMP_TEST_UID_W0, TEMP_TEST_UID_W1, TEMP_TEST_UID_W2
};
static unsigned init_calls, start_calls, capture_calls, abort_calls;
static bool line_low;
static BSP_Dht11Status init_result, start_result, capture_result;
static uint8_t frame[5];
static char status_line[257], stats_line[257];
static unsigned status_logs, stats_logs;

BSP_Dht11Status BSP_DHT11_Init(void)
{
    ++init_calls;
    return init_result;
}

BSP_Dht11Status BSP_DHT11_Start(void)
{
    CHECK(!line_low);
    ++start_calls;
    if (start_result == BSP_DHT11_OK) { line_low = true; }
    return start_result;
}

BSP_Dht11Status BSP_DHT11_Capture(uint8_t out[5])
{
    CHECK(line_low);
    line_low = false;
    ++capture_calls;
    if (capture_result == BSP_DHT11_OK) { memcpy(out, frame, 5U); }
    return capture_result;
}

void BSP_DHT11_Abort(void)
{
    CHECK(line_low);
    ++abort_calls;
    line_low = false;
}

static void log_line(const char *line, int length)
{
    CHECK(!line_low);
    CHECK(length > 0 && length <= 256);
    CHECK(!TemperatureService_IsStartLowActive());
    CHECK((size_t)length == strlen(line));
    CHECK(line[length - 2] == '\r' && line[length - 1] == '\n');
    if (strncmp(line, "$TEMP_STATS,", 12U) == 0)
    {
        memcpy(stats_line, line, (size_t)length + 1U);
        ++stats_logs;
    }
    else
    {
        CHECK(strncmp(line, "$TEMP,", 6U) == 0);
        memcpy(status_line, line, (size_t)length + 1U);
        ++status_logs;
    }
}

static void sample_frame(unsigned t, unsigned fraction, unsigned rh)
{
    frame[0] = (uint8_t)rh; frame[1] = 0U;
    frame[2] = (uint8_t)t; frame[3] = (uint8_t)fraction;
    frame[4] = (uint8_t)(frame[0] + frame[2] + frame[3]);
}

static void reset(uint32_t now_ms)
{
    /* Let the service release a previous owned pulse before resetting stubs. */
    TemperatureService_Init(now_ms, NULL, NULL);
    init_calls = start_calls = capture_calls = abort_calls = 0U;
    status_logs = stats_logs = 0U;
    status_line[0] = stats_line[0] = '\0';
    line_low = false;
    init_result = start_result = capture_result = BSP_DHT11_OK;
    sample_frame(24U, 4U, 53U);
}

static void attempt(uint32_t now_ms)
{
    unsigned before = start_calls;
    TemperatureService_Process(now_ms);
    CHECK(start_calls == before + 1U);
    if (line_low) { TemperatureService_Process(now_ms + TEMP_START_LOW_MS); }
}

static void contains(const char *line, const char *part)
{
    if (strstr(line, part) == NULL)
    {
        fprintf(stderr, "Expected [%s] in [%s]\n", part, line);
        CHECK(false);
    }
    ++checks;
}

static void two_good_samples(uint32_t base)
{
    TemperatureService_Init(base, allowed_uid, log_line);
    attempt(base + 1000U);
    attempt(base + 6000U);
}

static void powerup_priming_and_no_logs_while_low(void)
{
    reset(0U);
    TemperatureService_Init(0U, allowed_uid, log_line);
    CHECK(init_calls == 1U);
    TemperatureService_Process(999U);
    CHECK(start_calls == 0U && status_logs == 0U);
    TemperatureService_Process(1000U);
    CHECK(start_calls == 1U && capture_calls == 0U && status_logs == 0U);
    CHECK(TemperatureService_IsStartLowActive());
    TemperatureService_Process(1019U);
    CHECK(capture_calls == 0U && status_logs == 0U);
    TemperatureService_Process(1020U);
    CHECK(capture_calls == 1U && status_logs == 1U && stats_logs == 1U);
    CHECK(!TemperatureService_IsStartLowActive());
    contains(status_line, "STATE=UNREADY");
    contains(status_line, "LAST=PRIMING");
    CHECK(strstr(status_line, "T_MILLI_C=") == NULL);
    CHECK(strstr(status_line, "C_MM_S=") == NULL);
    contains(stats_line, "ATT=1,OK=0,PRIME=1");
    TemperatureService_Process(5999U);
    CHECK(start_calls == 1U);
    attempt(6000U);
    TemperatureService_Process(7020U);
    contains(status_line, "$TEMP,STATE=OK,VERSION=1,T_MILLI_C=24400,");
    contains(status_line, "RH_MILLI_PCT=53000,C_MM_S=346086,AGE_MS=1000,FRESH=1,LAST=OK\r\n");
    contains(stats_line, "ATT=2,OK=1,PRIME=1");
}

static void uid_and_reinitialization_ownership(void)
{
    uint32_t other[3] = {TEMP_TEST_UID_W0, TEMP_TEST_UID_W1, TEMP_TEST_UID_W2 ^ 1U};
    reset(0U);
    TemperatureService_Init(0U, other, log_line);
    TemperatureService_Process(1000U);
    CHECK(init_calls == 0U && start_calls == 0U && abort_calls == 0U);
    contains(status_line, "STATE=DISABLED");
    CHECK(strstr(status_line, "T_MILLI_C=") == NULL);
    TemperatureService_Init(0U, NULL, log_line);
    TemperatureService_Process(1000U);
    CHECK(init_calls == 0U && start_calls == 0U);
    TemperatureService_Init(0U, allowed_uid, NULL);
    TemperatureService_Process(1000U);
    CHECK(init_calls == 0U && start_calls == 0U && abort_calls == 0U);
    TemperatureService_Init(2000U, allowed_uid, log_line);
    TemperatureService_Process(3000U);
    CHECK(line_low);
    TemperatureService_Init(3001U, other, log_line);
    CHECK(!line_low && abort_calls == 1U);
    TemperatureService_Init(3002U, other, log_line);
    CHECK(abort_calls == 1U);
}

static void initialization_error_is_not_retried(void)
{
    static const BSP_Dht11Status errors[] = {
        BSP_DHT11_INIT_ERROR, BSP_DHT11_CLOCK_ERROR, BSP_DHT11_ARGUMENT
    };
    unsigned i;
    for (i = 0U; i < sizeof errors / sizeof errors[0]; ++i)
    {
        reset(0U);
        init_result = errors[i];
        TemperatureService_Init(0U, allowed_uid, log_line);
        TemperatureService_Process(1000U);
        TemperatureService_Process(100000U);
        CHECK(init_calls == 1U && start_calls == 0U);
        contains(status_line, "STATE=UNREADY");
        contains(status_line, "LAST=INIT");
        contains(stats_line, "ATT=0,OK=0,PRIME=0,INIT=1");
    }
}

static void zero_is_real_and_logging_is_bounded(void)
{
    unsigned before_status, before_stats;
    reset(0U);
    sample_frame(0U, 0U, 53U);
    two_good_samples(0U);
    TemperatureService_Process(7020U);
    contains(status_line, "STATE=OK,VERSION=1,T_MILLI_C=0,");
    contains(status_line, "C_MM_S=331300");
    before_status = status_logs; before_stats = stats_logs;
    TemperatureService_Process(7020U);
    TemperatureService_Process(8019U);
    CHECK(status_logs == before_status && stats_logs == before_stats);
    TemperatureService_Process(8020U);
    CHECK(status_logs == before_status + 1U && stats_logs == before_stats + 1U);
}

static void hold_stale_and_two_frame_recovery(void)
{
    reset(0U);
    two_good_samples(0U);
    capture_result = BSP_DHT11_TIMEOUT;
    attempt(11000U);
    contains(status_line, "STATE=HOLD_LAST,VERSION=1,T_MILLI_C=24400");
    contains(status_line, "AGE_MS=5000,FRESH=0,LAST=TIMEOUT");
    capture_result = BSP_DHT11_OK;
    sample_frame(30U, 0U, 54U);
    attempt(16000U);
    contains(status_line, "STATE=HOLD_LAST,VERSION=1,T_MILLI_C=24400");
    contains(status_line, "LAST=PRIMING");
    capture_result = BSP_DHT11_TIMEOUT;
    attempt(21000U);
    contains(status_line, "STATE=STALE,VERSION=1,T_MILLI_C=24400");
    contains(status_line, "AGE_MS=15000,FRESH=0,LAST=TIMEOUT");
    capture_result = BSP_DHT11_OK;
    attempt(26000U);
    contains(status_line, "STATE=STALE,VERSION=1");
    contains(status_line, "LAST=PRIMING");
    attempt(31000U);
    contains(status_line, "STATE=OK,VERSION=2,T_MILLI_C=30000");
}

static void failure_reasons_and_counters(void)
{
    static const BSP_Dht11Status errors[] = {
        BSP_DHT11_INIT_ERROR, BSP_DHT11_LINE_BUSY, BSP_DHT11_START_WINDOW,
        BSP_DHT11_TIMEOUT, BSP_DHT11_PULSE_ERROR, BSP_DHT11_CLOCK_ERROR,
        BSP_DHT11_ARGUMENT
    };
    static const char *const reasons[] = {"INIT", "BUS", "START", "TIMEOUT", "PULSE", "CLOCK", "INIT"};
    unsigned i, phase;
    for (phase = 0U; phase < 2U; ++phase)
    {
        for (i = 0U; i < sizeof errors / sizeof errors[0]; ++i)
        {
            char expected[24];
            reset(0U);
            two_good_samples(0U);
            if (phase == 0U) { start_result = errors[i]; }
            else { capture_result = errors[i]; }
            attempt(11000U);
            (void)snprintf(expected, sizeof expected, "LAST=%s", reasons[i]);
            contains(status_line, expected);
            contains(status_line, "STATE=HOLD_LAST");
            (void)snprintf(expected, sizeof expected, "%s=1", reasons[i]);
            contains(stats_line, expected);
            CHECK(start_calls == 3U);
            TemperatureService_Process(15999U);
            CHECK(start_calls == 3U);
        }
    }
    reset(0U); two_good_samples(0U);
    frame[4] ^= 1U; attempt(11000U);
    contains(status_line, "LAST=CRC"); contains(stats_line, "CRC=1");
    sample_frame(61U, 0U, 53U); attempt(16000U);
    contains(status_line, "LAST=RANGE"); contains(stats_line, "RANGE=1");
    capture_result = (BSP_Dht11Status)99; attempt(21000U);
    contains(status_line, "LAST=INIT"); contains(stats_line, "INIT=1");
}

static void tick_wrap_delayed_loop_and_long_idle(void)
{
    uint32_t base = UINT32_MAX - 500U;
    unsigned before;
    reset(base);
    two_good_samples(base);
    TemperatureService_Process(base + 7020U);
    contains(status_line, "STATE=OK,VERSION=1");
    contains(status_line, "AGE_MS=1000");
    before = start_calls;
    TemperatureService_Process(base + 30000U);
    CHECK(start_calls == before + 1U && line_low);
    TemperatureService_Process(base + 30020U);
    contains(status_line, "STATE=STALE,VERSION=1");
    contains(status_line, "LAST=PRIMING");
    TemperatureService_Process(base + 30020U);
    TemperatureService_Process(base + 30021U);
    CHECK(start_calls == before + 1U);
    attempt(base + 35000U);
    contains(status_line, "STATE=OK,VERSION=2");
    contains(status_line, "AGE_MS=0,FRESH=1,LAST=OK");
}

static void manual_selection_and_auto_recovery(void)
{
    TemperatureSnapshot value;
    reset(0U);
    TemperatureService_Init(0U, allowed_uid, log_line);
    TemperatureService_GetSnapshot(0U, &value);
    CHECK(!value.manual && !value.valid && value.version == 0U);
    CHECK(TemperatureService_SetManual(100U, 25000));
    TemperatureService_GetSnapshot(30100U, &value);
    CHECK(value.manual && value.valid && value.version == 1U);
    CHECK(value.temperature_milli_c == 25000 && value.sound_speed_mm_s == 346450U);
    TemperatureService_Process(30100U);
    CHECK(start_calls == 0U);
    contains(status_line, "STATE=MANUAL");
    CHECK(strstr(status_line, "RH_MILLI_PCT") == NULL);
    CHECK(!TemperatureService_SetManual(30101U, 60001));
    TemperatureService_GetSnapshot(30101U, &value);
    CHECK(value.manual && value.valid && value.version == 1U);
    CHECK(value.temperature_milli_c == 25000);
    CHECK(TemperatureService_SetManual(30102U, 0));
    TemperatureService_GetSnapshot(30102U, &value);
    CHECK(value.temperature_milli_c == 0 && value.sound_speed_mm_s == 331300U);
    CHECK(TemperatureService_SetManual(30103U, -10000));
    TemperatureService_GetSnapshot(30103U, &value);
    CHECK(value.temperature_milli_c == -10000 && value.sound_speed_mm_s == 325240U);
    CHECK(TemperatureService_UseAuto());
    CHECK(TemperatureService_UseAuto());
    TemperatureService_GetSnapshot(30103U, &value);
    CHECK(!value.manual && !value.valid && value.version == 3U);
    attempt(30104U);
    TemperatureService_GetSnapshot(30124U, &value);
    CHECK(!value.valid && value.version == 3U);
    attempt(35104U);
    TemperatureService_GetSnapshot(35124U, &value);
    CHECK(!value.manual && value.valid && value.version == 4U);
    CHECK(value.temperature_milli_c == 24400);
    /* Never interrupt an owned start pulse from a public mode API. */
    TemperatureService_Process(40104U);
    CHECK(line_low && !TemperatureService_SetManual(40104U, 20000));
    CHECK(!TemperatureService_UseAuto());
    TemperatureService_Process(40124U);
    CHECK(TemperatureService_SetManual(40125U, 20000));
    CHECK(TemperatureService_UseAuto());
    TemperatureService_Process(45103U);
    CHECK(start_calls == 3U); /* Still respects the previous attempt interval. */
    attempt(45104U);
    TemperatureService_GetSnapshot(45124U, &value);
    CHECK(!value.valid);
    reset(0U);
    TemperatureService_Init(0U, NULL, log_line);
    CHECK(!TemperatureService_SetManual(0U, 20000));
    CHECK(!TemperatureService_UseAuto());
    reset(0U);
    init_result = BSP_DHT11_INIT_ERROR;
    TemperatureService_Init(0U, allowed_uid, log_line);
    CHECK(TemperatureService_SetManual(0U, 20000));
    TemperatureService_GetSnapshot(UINT32_MAX, &value);
    CHECK(value.manual && value.valid);
}

static void remote_temperature_display(void)
{
    uint32_t other[3] = {TEMP_TEST_UID_W0, TEMP_TEST_UID_W1, TEMP_TEST_UID_W2 ^ 1U};
    TemperatureSnapshot value;
    reset(0U);
    TemperatureService_Init(0U, other, log_line);
    TemperatureService_Process(1000U);
    contains(status_line, "STATE=DISABLED");
    CHECK(TemperatureService_SetRemote(1001U, 23500, 345541U, 7U, false));
    TemperatureService_GetSnapshot(1002U, &value);
    CHECK(value.valid && !value.manual && value.version == 7U);
    CHECK(value.temperature_milli_c == 23500 && value.sound_speed_mm_s == 345541U);
    CHECK(!TemperatureService_SetRemote(1003U, 23500, 345541U, 6U, false));
    CHECK(!TemperatureService_SetRemote(1004U, 23500, 345540U, 8U, false));
    CHECK(TemperatureService_SetRemote(1005U, 23500, 345541U, 7U, false));
    TemperatureService_Process(2000U);
    contains(status_line, "STATE=REMOTE,VERSION=7,T_MILLI_C=23500");
    contains(status_line, "C_MM_S=345541,AGE_MS=995,FRESH=1,LAST=REMOTE");
    contains(status_line, "SOURCE=BOARD_A");
    TemperatureService_GetSnapshot(16006U, &value);
    CHECK(!value.valid);
    CHECK(TemperatureService_SetRemote(17000U, 23500, 345541U, 6U, false));
    TemperatureService_GetSnapshot(17001U, &value);
    CHECK(value.valid && value.version == 6U);
    TemperatureService_Process(18000U);
    contains(status_line, "STATE=REMOTE,VERSION=6");
}

int main(void)
{
    manual_selection_and_auto_recovery();
    powerup_priming_and_no_logs_while_low();
    uid_and_reinitialization_ownership();
    initialization_error_is_not_retried();
    zero_is_real_and_logging_is_bounded();
    hold_stale_and_two_frame_recovery();
    failure_reasons_and_counters();
    tick_wrap_delayed_loop_and_long_idle();
    remote_temperature_display();
    reset(0U);
    TemperatureService_Init(0U,allowed_uid,log_line);
    TemperatureService_Process(1000U);
    /* DWT at 200 MHz wraps after about 21.475 seconds. HAL tick must reject
       long pending low pulses independently of a cycle-counter alias. */
    TemperatureService_Process(22500U);
    CHECK(capture_calls==0U && abort_calls==1U && !line_low);
    contains(status_line,"LAST=START");
    contains(stats_line,"START=1");
    printf("PASS temperature_service: powerup/priming, UID/ownership, init, "
           "zero/log-rate, hold/stale/recovery, errors, wrap/idle (%u checks)\n", checks);
    return 0;
}
