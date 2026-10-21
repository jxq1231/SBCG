#include "temperature_service.h"
#include "temperature_config.h"
#include "dht11_frame.h"
#include "sound_speed.h"
#include "BSP/bsp_dht11.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { TEMP_LINE_CAPACITY = 257 };

/* Each uint32_t occupies at most ten decimal characters. Include CRLF and
 * NUL in this compile-time bound; do not rely on typical small counters. */
_Static_assert(sizeof("$TEMP_STATS,ATT=4294967295,OK=4294967295,"
    "PRIME=4294967295,INIT=4294967295,BUS=4294967295,START=4294967295,"
    "TIMEOUT=4294967295,PULSE=4294967295,CLOCK=4294967295,"
    "CRC=4294967295,RANGE=4294967295,FORMAT=4294967295\r\n")
    <= TEMP_LINE_CAPACITY, "temperature statistics exceed UART line bound");
_Static_assert(sizeof("$TEMP,STATE=HOLD_LAST,VERSION=4294967295,"
    "T_MILLI_C=-20000,RH_MILLI_PCT=95000,C_MM_S=367660,"
    "AGE_MS=4294967295,FRESH=0,LAST=PRIMING\r\n") <= TEMP_LINE_CAPACITY,
    "temperature status exceeds UART line bound");

typedef struct
{
    uint32_t attempts, successes, primes, init_errors, bus_errors;
    uint32_t start_errors, timeouts, pulse_errors, clock_errors;
    uint32_t checksum_errors, range_errors, format_errors;
} TemperatureStats;

typedef struct
{
    TemperatureLog log;
    TemperatureStats stats;
    Dht11Sample sample;
    const char *last;
    uint32_t init_ms, last_attempt_ms, low_start_ms, last_log_ms;
    uint32_t valid_ms, version, sound_speed_mm_s;
    bool enabled, ready, attempted, waiting, primed, have_value, fresh, manual;
} TemperatureService;

static TemperatureService service;

bool TemperatureService_IsStartLowActive(void)
{
    return service.waiting;
}

static void increment(uint32_t *counter)
{
    if (*counter < UINT32_MAX) { ++*counter; }
}

void TemperatureService_GetSnapshot(uint32_t now_ms, TemperatureSnapshot *out)
{
    if (out == NULL) { return; }
    memset(out, 0, sizeof *out);
    out->manual = service.manual;
    out->version = service.version;
    out->valid = service.enabled && service.have_value
        && (service.manual || (service.fresh
            && (uint32_t)(now_ms - service.valid_ms) < TEMP_STALE_MS));
    if (service.have_value)
    {
        out->age_ms = now_ms - service.valid_ms;
        out->temperature_milli_c = service.sample.temperature_milli_c;
        out->sound_speed_mm_s = service.sound_speed_mm_s;
    }
}

bool TemperatureService_SetManual(uint32_t now_ms, int32_t temperature_milli_c)
{
    uint32_t sound_speed_mm_s;
    if (!service.enabled || service.waiting || service.version == UINT32_MAX
        || !SoundSpeed_Calculate(temperature_milli_c, &sound_speed_mm_s))
    { return false; }
    service.manual = true;
    service.have_value = service.fresh = true;
    service.sample.temperature_milli_c = temperature_milli_c;
    service.sample.relative_humidity_milli_pct = 0U; /* Not a manual input. */
    service.sound_speed_mm_s = sound_speed_mm_s;
    service.valid_ms = now_ms;
    ++service.version;
    service.last = "SET";
    return true;
}

bool TemperatureService_UseAuto(void)
{
    if (!service.enabled || service.waiting) { return false; }
    if (!service.manual) { return true; }
    service.manual = false;
    service.have_value = service.fresh = service.primed = false;
    service.last = "AUTO_WAIT";
    return true;
}

static void bsp_failure(BSP_Dht11Status result)
{
    uint32_t *counter;
    service.primed = false;
    service.fresh = false;
    switch (result)
    {
    case BSP_DHT11_LINE_BUSY:
        counter = &service.stats.bus_errors; service.last = "BUS"; break;
    case BSP_DHT11_START_WINDOW:
        counter = &service.stats.start_errors; service.last = "START"; break;
    case BSP_DHT11_TIMEOUT:
        counter = &service.stats.timeouts; service.last = "TIMEOUT"; break;
    case BSP_DHT11_PULSE_ERROR:
        counter = &service.stats.pulse_errors; service.last = "PULSE"; break;
    case BSP_DHT11_CLOCK_ERROR:
        counter = &service.stats.clock_errors; service.last = "CLOCK"; break;
    default:
        /* ARGUMENT is an internal invariant failure, never a sensor value. */
        counter = &service.stats.init_errors; service.last = "INIT"; break;
    }
    increment(counter);
}

static void accept_frame(const uint8_t frame[5], uint32_t now_ms)
{
    Dht11Sample sample;
    uint32_t sound_speed_mm_s;
    Dht11FrameStatus result = Dht11Frame_Decode(frame, &sample);

    if (result != DHT11_FRAME_OK)
    {
        service.primed = false;
        service.fresh = false;
        if (result == DHT11_FRAME_CHECKSUM)
        {
            increment(&service.stats.checksum_errors); service.last = "CRC";
        }
        else
        {
            increment(&service.stats.range_errors); service.last = "RANGE";
        }
        return;
    }
    if (!SoundSpeed_Calculate(sample.temperature_milli_c, &sound_speed_mm_s))
    {
        service.primed = false;
        service.fresh = false;
        increment(&service.stats.range_errors);
        service.last = "RANGE";
        return;
    }
    if (!service.primed)
    {
        /* DHT11 may initially return its previous internal measurement. */
        service.primed = true;
        service.fresh = false;
        increment(&service.stats.primes);
        service.last = "PRIMING";
        return;
    }
    if (service.version == UINT32_MAX)
    {
        service.fresh = false;
        service.last = "VERSION";
        return;
    }
    service.sample = sample;
    service.sound_speed_mm_s = sound_speed_mm_s;
    service.valid_ms = now_ms;
    ++service.version;
    increment(&service.stats.successes);
    service.have_value = true;
    service.fresh = true;
    service.last = "OK";
}

static void emit_line(const char *line, int length)
{
    if (length > 0 && length < TEMP_LINE_CAPACITY)
    {
        service.log(line, length);
    }
    else
    {
        /* The callback is void: UART transport errors belong to the caller.
         * FORMAT counts only local snprintf failures/truncation. */
        increment(&service.stats.format_errors);
    }
}

static void log_status(uint32_t now_ms)
{
    char line[TEMP_LINE_CAPACITY];
    int length;
    const char *state;
    uint32_t age_ms = now_ms - service.valid_ms;

    if (service.log == NULL
        || (uint32_t)(now_ms - service.last_log_ms) < TEMP_LOG_PERIOD_MS)
    {
        return;
    }
    /* One status and one statistics line per elapsed second, without catchup.
     * Call only with the bus released so blocking UART cannot extend low. */
    service.last_log_ms = now_ms;
    if (!service.enabled) { state = "DISABLED"; }
    else if (service.manual) { state = "MANUAL"; }
    else if (!service.have_value) { state = "UNREADY"; }
    else if (age_ms >= TEMP_STALE_MS) { state = "STALE"; }
    else if (!service.fresh) { state = "HOLD_LAST"; }
    else { state = "OK"; }

    if (service.manual)
    {
        length = snprintf(line, sizeof line,
            "$TEMP,STATE=MANUAL,VERSION=%" PRIu32 ",T_MILLI_C=%" PRId32
            ",C_MM_S=%" PRIu32 ",AGE_MS=%" PRIu32
            ",FRESH=1,LAST=SET,MODE=MANUAL,SOURCE=MANUAL\r\n",
            service.version, service.sample.temperature_milli_c,
            service.sound_speed_mm_s, age_ms);
    }
    else if (service.have_value)
    {
        length = snprintf(line, sizeof line,
            "$TEMP,STATE=%s,VERSION=%" PRIu32 ",T_MILLI_C=%" PRId32
            ",RH_MILLI_PCT=%" PRIu32 ",C_MM_S=%" PRIu32
            ",AGE_MS=%" PRIu32 ",FRESH=%u,LAST=%s\r\n",
            state, service.version, service.sample.temperature_milli_c,
            service.sample.relative_humidity_milli_pct,
            service.sound_speed_mm_s, age_ms,
            (unsigned)(service.fresh && age_ms < TEMP_STALE_MS), service.last);
    }
    else
    {
        length = snprintf(line, sizeof line,
            "$TEMP,STATE=%s,VERSION=%" PRIu32 ",FRESH=0,LAST=%s\r\n",
            state, service.version, service.last);
    }
    emit_line(line, length);
    length = snprintf(line, sizeof line,
        "$TEMP_STATS,ATT=%" PRIu32 ",OK=%" PRIu32 ",PRIME=%" PRIu32
        ",INIT=%" PRIu32 ",BUS=%" PRIu32 ",START=%" PRIu32
        ",TIMEOUT=%" PRIu32 ",PULSE=%" PRIu32 ",CLOCK=%" PRIu32
        ",CRC=%" PRIu32 ",RANGE=%" PRIu32 ",FORMAT=%" PRIu32 "\r\n",
        service.stats.attempts, service.stats.successes, service.stats.primes,
        service.stats.init_errors, service.stats.bus_errors,
        service.stats.start_errors, service.stats.timeouts,
        service.stats.pulse_errors, service.stats.clock_errors,
        service.stats.checksum_errors, service.stats.range_errors,
        service.stats.format_errors);
    emit_line(line, length);
}

void TemperatureService_Init(uint32_t now_ms, const uint32_t uid_words[3],
                             TemperatureLog log)
{
    if (service.waiting) { BSP_DHT11_Abort(); }
    memset(&service, 0, sizeof service);
    service.log = log;
    service.init_ms = now_ms;
    service.last_log_ms = now_ms;
    service.last = "DISABLED";
    service.enabled = log != NULL && uid_words != NULL
        && uid_words[0] == TEMP_TEST_UID_W0
        && uid_words[1] == TEMP_TEST_UID_W1
        && uid_words[2] == TEMP_TEST_UID_W2;
    if (service.enabled)
    {
        BSP_Dht11Status result = BSP_DHT11_Init();
        service.ready = result == BSP_DHT11_OK;
        service.last = "INIT";
        /* Init is a one-shot prerequisite. Its failure is counted once as
         * INIT; CLOCK diagnoses failures during subsequent acquisition. */
        if (!service.ready) { increment(&service.stats.init_errors); }
    }
}

void TemperatureService_Process(uint32_t now_ms)
{
    if (service.waiting)
    {
        uint8_t frame[5];
        BSP_Dht11Status result;
        /* No formatting or UART work is permitted until capture releases low. */
        if ((uint32_t)(now_ms - service.low_start_ms) < TEMP_START_LOW_MS)
        {
            return;
        }
        /* HAL tick also bounds long scheduler stalls across a full DWT wrap.
         * DWT performs the precise microsecond check for ordinary attempts. */
        if ((uint32_t)(now_ms - service.low_start_ms) > TEMP_START_MAX_US / 1000U)
        {
            BSP_DHT11_Abort();
            result = BSP_DHT11_START_WINDOW;
        }
        else { result = BSP_DHT11_Capture(frame); }
        service.waiting = false;
        if (result == BSP_DHT11_OK) { accept_frame(frame, now_ms); }
        else { bsp_failure(result); }
        log_status(now_ms);
        return;
    }

    if (service.enabled && service.ready && !service.manual
        && ((!service.attempted
             && (uint32_t)(now_ms - service.init_ms) >= TEMP_POWERUP_MS)
            || (service.attempted
                && (uint32_t)(now_ms - service.last_attempt_ms) >= TEMP_PERIOD_MS)))
    {
        BSP_Dht11Status result;
        if (service.attempted
            && (uint32_t)(now_ms - service.last_attempt_ms) >= TEMP_STALE_MS)
        {
            service.primed = false;
            service.fresh = false;
        }
        service.attempted = true;
        service.last_attempt_ms = now_ms;
        increment(&service.stats.attempts);
        result = BSP_DHT11_Start();
        if (result == BSP_DHT11_OK)
        {
            service.waiting = true;
            service.low_start_ms = now_ms;
            return;
        }
        bsp_failure(result);
    }
    log_status(now_ms);
}
