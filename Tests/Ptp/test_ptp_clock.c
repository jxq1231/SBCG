#include "Sync/ptp_clock.h"
#include "BSP/bsp_ptp.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static BSP_PtpStatus init_result, read_result;
static BSP_PtpTime current;
static unsigned int init_calls, read_calls, logs;
static char line[160];
/* Enforce the target nano formatter's lack of long-long conversions. */
int __wrap___mingw_snprintf(char *buffer, size_t size, const char *format, ...)
{
    assert(strstr(format, "ll") == NULL && strstr(format, "I64") == NULL);
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}
int __wrap_snprintf(char *buffer, size_t size, const char *format, ...)
{
    assert(strstr(format, "ll") == NULL && strstr(format, "I64") == NULL);
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}
BSP_PtpStatus BSP_PTP_Init(void) { ++init_calls; return init_result; }
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *time)
{ ++read_calls; if (read_result == BSP_PTP_OK) { *time = current; } return read_result; }
static void Log(const char *data, int length)
{
    assert(length > 0 && (size_t)length < sizeof(line));
    memcpy(line, data, (size_t)length); line[length] = 0; ++logs;
}
static void Reset(uint32_t now_ms)
{
    init_result = read_result = BSP_PTP_OK;
    init_calls = read_calls = logs = 0;
    current = (BSP_PtpTime){1,5};
    PtpClock_Init(now_ms, Log);
}
int main(void)
{
    Reset(0); assert(init_calls == 1 && logs == 0);
    PtpClock_Process(999); assert(logs == 0 && read_calls == 0);
    PtpClock_Process(1000);
    assert(logs == 1 && strstr(line,"STATE=LOCAL,SYNC=0") && strstr(line,"PREV=0"));
    assert(strstr(line,"SEC=1,NSEC=5") && strstr(line,"ERRORS=0"));
    current = (BSP_PtpTime){2,5}; PtpClock_Process(2000);
    assert(strstr(line,"DELTA_NS=1000000000,PREV=1"));
    puts("PASS: local clock logs once per second with explicit units and SYNC=0");

    Reset(UINT32_MAX-499); PtpClock_Process(499); assert(logs == 0);
    PtpClock_Process(500); assert(logs == 1);
    Reset(0); PtpClock_Process(10000); PtpClock_Process(10001); assert(logs == 1);
    puts("PASS: log cadence survives tick wrap and delayed loops do not burst");

    Reset(0); PtpClock_Process(1000); PtpClock_Process(2000);
    assert(strstr(line,"ERR=STALLED") && strstr(line,"ERRORS=1"));
    current.seconds = 0; PtpClock_Process(3000);
    assert(strstr(line,"ERR=BACKWARD") && strstr(line,"ERRORS=2"));
    current.seconds = 3; PtpClock_Process(4000);
    assert(strstr(line,"STATE=LOCAL") && strstr(line,"DELTA_NS=2000000000") && strstr(line,"ERRORS=2"));
    puts("PASS: stopped/backward clocks are errors and recovery retains the last good baseline");

    Reset(0); read_result = BSP_PTP_BAD_SNAPSHOT; PtpClock_Process(1000);
    assert(strstr(line,"STATE=ERROR") && strstr(line,"ERR=BAD_SNAPSHOT"));
    logs = read_calls = 0; init_result = BSP_PTP_INIT_TIMEOUT;
    PtpClock_Init(0, Log); PtpClock_Process(1000);
    assert(read_calls == 0 && logs == 1 && strstr(line,"ERR=INIT_TIMEOUT"));
    puts("PASS: initialization and read failures stay visible without reinitialization");

    Reset(0); PtpClock_Process(1000);
    current = (BSP_PtpTime){UINT32_MAX,999999999};
    PtpClock_Process(UINT32_MAX);
    assert(strstr(line,"SEC=4294967295,NSEC=999999999") && strstr(line,"STATE=LOCAL"));
    assert(strstr(line,"DELTA_NS=4294967294999999994,PREV=1,ERRORS=0"));
    assert(strlen(line) < sizeof(line));
    /* No callback is a supported observation-disabled state. */
    PtpClock_Init(0, NULL); PtpClock_Process(1000);
    assert(logs == 2);
    puts("PASS: full-width time fields fit the bounded log and a disabled logger is safe");
    return 0;
}
