#include "temperature_console.h"
#include "BSP/bsp_console.h"
#include "Temperature/temperature_service.h"
#include "Temperature/temperature_config.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { COMMAND_CAPACITY = 64, BYTE_BUDGET = 64, REPLY_CAPACITY = 224 };
#define COMMAND_INTERVAL_MS 100U
static TemperatureConsole_Log logger;
static char command[COMMAND_CAPACITY];
/* Main-context-only, non-reentrant formatter. SendLine consumes or copies the
 * bytes before returning. Avoid nested 224-byte buffers on the MSP stack. */
static char reply[REPLY_CAPACITY];
static unsigned used;
static bool discard, replied;
static uint32_t last_reply_ms;

static void emit(const char *line, int length)
{
    if (logger != NULL && length > 0 && length < REPLY_CAPACITY)
    { logger(line, length); }
}

static void result(const char *name, const char *outcome, const char *reason)
{
    int length = snprintf(reply, sizeof reply, "$CMD,NAME=%s,RESULT=%s,REASON=%s\r\n",
                          name, outcome, reason);
    emit(reply, length);
}

static void environment(uint32_t now_ms)
{
    TemperatureSnapshot value;
    TemperatureService_GetSnapshot(now_ms, &value);
    const char *mode = value.manual ? "MANUAL" : "AUTO";
    const char *source = !value.valid ? "NONE" : (value.manual ? "MANUAL" : "DHT11");
    int length;
    if (value.valid)
    {
        length = snprintf(reply, sizeof reply,
            "$ENV,MODE=%s,SOURCE=%s,VALID=1,VERSION=%" PRIu32
            ",T_MILLI_C=%" PRId32 ",C_MM_S=%" PRIu32 ",AGE_MS=%" PRIu32 "\r\n",
            mode, source, value.version, value.temperature_milli_c,
            value.sound_speed_mm_s, value.age_ms);
    }
    else
    {
        length = snprintf(reply, sizeof reply,
            "$ENV,MODE=%s,SOURCE=%s,VALID=0,VERSION=%" PRIu32 "\r\n",
            mode, source, value.version);
    }
    emit(reply, length);
}

static bool parse_temperature(const char *text, int32_t *out)
{
    bool negative = *text == '-';
    if (negative) { ++text; }
    int32_t magnitude = 0;
    unsigned digits = 0;
    while (*text >= '0' && *text <= '9')
    {
        if (++digits > 2U) { return false; }
        magnitude = magnitude * 10 + (*text++ - '0');
    }
    if (digits == 0U) { return false; }
    magnitude *= 1000;
    if (*text == '.')
    {
        ++text;
        if (*text < '0' || *text > '9') { return false; }
        magnitude += (*text++ - '0') * 100;
    }
    if (*text != '\0') { return false; }
    *out = negative ? -magnitude : magnitude;
    return *out >= TEMP_MIN_MILLI_C && *out <= TEMP_MAX_MILLI_C;
}

static void execute(uint32_t now_ms)
{
    if (strcmp(command, "PING") == 0)
    {
        static const char pong[] = "$CMD,NAME=PING,RESULT=OK\r\n";
        emit(pong, (int)sizeof pong - 1);
    }
    else if (strcmp(command, "TEMP?") == 0)
    {
        BSP_ConsoleRxStats stats;
        environment(now_ms);
        BSP_Console_GetRxStats(&stats);
        int length = snprintf(reply, sizeof reply,
            "$CONSOLE,RX_ERRORS=%" PRIu32 ",RX_OVERFLOWS=%" PRIu32
            ",RX_REARM_ERRORS=%" PRIu32 "\r\n",
            stats.errors, stats.overflows, stats.rearm_errors);
        emit(reply, length);
    }
    else if (strncmp(command, "TEMP SET ", 9U) == 0)
    {
        int32_t temperature_milli_c;
        if (!parse_temperature(command + 9, &temperature_milli_c))
        { result("SET", "ERROR", "VALUE"); }
        else if (!TemperatureService_SetManual(now_ms, temperature_milli_c))
        { result("SET", "ERROR", "UNAVAILABLE"); }
        else
        {
            static const char ok[] = "$CMD,NAME=SET,RESULT=OK\r\n";
            emit(ok, (int)sizeof ok - 1); environment(now_ms);
        }
    }
    else if (strcmp(command, "TEMP AUTO") == 0)
    {
        if (!TemperatureService_UseAuto()) { result("AUTO", "ERROR", "UNAVAILABLE"); }
        else
        {
            static const char ok[] = "$CMD,NAME=AUTO,RESULT=OK\r\n";
            emit(ok, (int)sizeof ok - 1); environment(now_ms);
        }
    }
    else { result("INVALID", "ERROR", "COMMAND"); }
}

void TemperatureConsole_Init(TemperatureConsole_Log log)
{
    logger = log;
    used = 0U;
    discard = replied = false;
    last_reply_ms = 0U;
}

void TemperatureConsole_Process(uint32_t now_ms)
{
    if (logger == NULL || TemperatureService_IsStartLowActive()) { return; }
    BSP_Console_RxProcess();
    if (replied && (uint32_t)(now_ms - last_reply_ms) < COMMAND_INTERVAL_MS) { return; }
    for (unsigned i = 0; i < BYTE_BUDGET; ++i)
    {
        uint8_t byte;
        BSP_ConsoleRxResult read = BSP_Console_Read(&byte);
        if (read == BSP_CONSOLE_RX_NONE) { return; }
        if (read == BSP_CONSOLE_RX_GAP) { discard = true; used = 0U; continue; }
        if (byte == '\n')
        {
            if (discard) { result("INVALID", "ERROR", "FRAME"); }
            else
            {
                if (used != 0U && command[used - 1U] == '\r') { --used; }
                if (used == 0U) { continue; }
                command[used] = '\0'; execute(now_ms);
            }
            used = 0U; discard = false; replied = true; last_reply_ms = now_ms;
            return;
        }
        if (discard) { continue; }
        if ((byte < 32U && byte != '\r') || byte > 126U || used == COMMAND_CAPACITY - 1U)
        { discard = true; used = 0U; continue; }
        command[used++] = (char)byte;
    }
}
