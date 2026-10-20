#include "ptp_clock.h"
#include "BSP/bsp_ptp.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#define PTP_LOG_PERIOD_MS 1000U
#define PTP_LOG_CAPACITY 160U

static PtpClock_Log emit;
static BSP_PtpStatus init_status;
static uint32_t last_log_ms, sequence, errors;
static uint64_t previous_ns;
static bool have_previous;

/* nano.specs omits long-long printf conversions; keep all 64-bit digits. */
static const char *NanosecondsText(uint64_t nanoseconds, char text[21])
{
    char *digit = text + 20;
    *digit = '\0';
    do
    {
        *--digit = (char)('0' + nanoseconds % 10U);
        nanoseconds /= 10U;
    } while (nanoseconds != 0U);
    return digit;
}

static const char *StatusName(BSP_PtpStatus status)
{
    switch (status)
    {
        case BSP_PTP_OK: return "OK";
        case BSP_PTP_NOT_READY: return "NOT_READY";
        case BSP_PTP_BAD_CLOCK: return "BAD_CLOCK";
        case BSP_PTP_HAL_ERROR: return "HAL_ERROR";
        case BSP_PTP_INIT_TIMEOUT: return "INIT_TIMEOUT";
        case BSP_PTP_BAD_CONFIG: return "BAD_CONFIG";
        case BSP_PTP_BAD_SNAPSHOT: return "BAD_SNAPSHOT";
        default: return "UNKNOWN";
    }
}

void PtpClock_Init(uint32_t now_ms, PtpClock_Log log)
{
    emit = log;
    last_log_ms = now_ms;
    sequence = errors = 0U;
    previous_ns = 0U;
    have_previous = false;
    init_status = BSP_PTP_Init();
}

void PtpClock_Process(uint32_t now_ms)
{
    if (emit == NULL || (uint32_t)(now_ms - last_log_ms) < PTP_LOG_PERIOD_MS) { return; }
    last_log_ms = now_ms;
    ++sequence;

    BSP_PtpTime sample;
    BSP_PtpStatus status = init_status;
    if (status == BSP_PTP_OK) { status = BSP_PTP_Read(&sample); }
    const char *reason = (status == BSP_PTP_OK) ? NULL : StatusName(status);
    uint64_t now_ns = 0U;
    if (status == BSP_PTP_OK)
    {
        now_ns = (uint64_t)sample.seconds * UINT64_C(1000000000) + sample.nanoseconds;
        if (have_previous && now_ns <= previous_ns)
        {
            reason = (now_ns == previous_ns) ? "STALLED" : "BACKWARD";
        }
    }

    char line[PTP_LOG_CAPACITY];
    int length;
    if (reason != NULL)
    {
        ++errors;
        length = snprintf(line, sizeof(line),
            "$PTP,STATE=ERROR,SYNC=0,SEQ=%" PRIu32 ",TICK_MS=%" PRIu32
            ",ERR=%s,ERRORS=%" PRIu32 "\r\n", sequence, now_ms, reason, errors);
    }
    else
    {
        const uint64_t delta_ns = have_previous ? now_ns - previous_ns : 0U;
        char delta_text[21];
        length = snprintf(line, sizeof(line),
            "$PTP,STATE=LOCAL,SYNC=0,SEQ=%" PRIu32 ",TICK_MS=%" PRIu32
            ",SEC=%" PRIu32 ",NSEC=%" PRIu32 ",DELTA_NS=%s"
            ",PREV=%u,ERRORS=%" PRIu32 "\r\n", sequence, now_ms,
            sample.seconds, sample.nanoseconds, NanosecondsText(delta_ns, delta_text),
            have_previous ? 1U : 0U, errors);
        previous_ns = now_ns;
        have_previous = true;
    }
    emit(line, length);
}
