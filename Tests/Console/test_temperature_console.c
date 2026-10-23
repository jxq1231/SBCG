#include "Console/temperature_console.h"
#include "BSP/bsp_console.h"
#include "Temperature/temperature_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned symbols[2048], rd, wr, sets, autos, logs;
static char output[8192];
static TemperatureSnapshot snapshot;
static bool low, allow = true;

BSP_ConsoleRxResult BSP_Console_Read(uint8_t *out)
{
    if (rd == wr) { return BSP_CONSOLE_RX_NONE; }
    unsigned symbol = symbols[rd++];
    if (symbol == 256U) { return BSP_CONSOLE_RX_GAP; }
    *out = (uint8_t)symbol; return BSP_CONSOLE_RX_BYTE;
}
void BSP_Console_RxProcess(void) {}
void BSP_Console_GetRxStats(BSP_ConsoleRxStats *out)
{ memset(out, 0, sizeof *out); }
bool TemperatureService_IsStartLowActive(void) { return low; }
bool TemperatureService_SetManual(uint32_t now, int32_t t)
{
    (void)now;
    ++sets;
    if (!allow) { return false; }
    snapshot.manual = snapshot.valid = true;
    snapshot.temperature_milli_c = t;
    snapshot.sound_speed_mm_s = (uint32_t)(331300 + 606 * t / 1000);
    ++snapshot.version;
    return true;
}
bool TemperatureService_UseAuto(void)
{
    ++autos;
    if (!allow) { return false; }
    snapshot.manual = snapshot.valid = false; return true;
}
void TemperatureService_GetSnapshot(uint32_t now, TemperatureSnapshot *out)
{ (void)now; *out = snapshot; }
static void log_line(const char *line, int length)
{
    assert(length > 0 && length < 256 && !low);
    assert((int)strlen(line) == length && line[length-1] == '\n');
    assert(strlen(output) + (size_t)length < sizeof output);
    strcat(output, line); ++logs;
}
static void feed(const char *text)
{ while (*text) { assert(wr < 2048U); symbols[wr++] = (unsigned char)*text++; } }
static void clear(void) { output[0] = '\0'; logs = 0; }
static void drain(uint32_t *tick)
{ while (rd != wr) { TemperatureConsole_Process(*tick); *tick += 100U; } }

int main(void)
{
    uint32_t tick = 0;
    TemperatureConsole_Init(log_line);
    feed("TEMP SET 25"); drain(&tick); assert(sets == 0);
    feed(".0\r\nTEMP?\nPING\n"); drain(&tick);
    assert(sets == 1 && snapshot.temperature_milli_c == 25000);
    assert(strstr(output, "$CMD,NAME=SET,RESULT=OK\r\n"));
    assert(strstr(output, "MODE=MANUAL,SOURCE=MANUAL,VALID=1"));
    assert(strstr(output, "C_MM_S=346450"));
    assert(strstr(output, "$CMD,NAME=PING,RESULT=OK\r\n"));
    clear();
    const char *bad[] = {"TEMP SET 60.1\n", "TEMP SET -20.1\n", "TEMP SET 100\n",
        "TEMP SET 1.01\n", "TEMP SET +1\n", "TEMP SET 1e1\n", "TEMP SET nan\n",
        "TEMP SET 1 \n", "TEMP SET .1\n", "TEMP SET -\n", "TEMP SET 2\rX\n"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i)
    { feed(bad[i]); drain(&tick); }
    assert(sets == 1 && snapshot.version == 1 && snapshot.temperature_milli_c == 25000);
    assert(strstr(output, "RESULT=ERROR"));
    feed("TEMP SET -20.0\nTEMP SET 60.0\nTEMP SET 0\n"); drain(&tick);
    assert(sets == 4 && snapshot.temperature_milli_c == 0);
    clear(); feed("TEMP SET 10"); drain(&tick);
    symbols[wr++] = 256U;
    feed(".0\nPING\n"); drain(&tick);
    assert(sets == 4 && strstr(output, "NAME=PING,RESULT=OK"));
    clear();
    for (unsigned i = 0; i < 80; ++i) { feed("X"); }
    feed("TEMP SET 5\nPING\n"); drain(&tick);
    assert(sets == 4 && strstr(output, "NAME=PING,RESULT=OK"));
    clear(); symbols[wr++] = 0U; feed("TEMP SET 5\nPING\n"); drain(&tick);
    assert(sets == 4 && strstr(output, "NAME=PING,RESULT=OK"));
    clear(); feed("TEMP AUTO\nTEMP?\n"); drain(&tick);
    assert(autos == 1 && !snapshot.manual && !snapshot.valid);
    assert(strstr(output, "MODE=AUTO,SOURCE=NONE,VALID=0"));
    assert(!strstr(output, "T_MILLI_C="));
    clear(); low = true; feed("TEMP SET 20\n");
    TemperatureConsole_Process(tick); assert(rd != wr && sets == 4 && logs == 0);
    low = false; drain(&tick); assert(sets == 5);
    allow = false; clear(); feed("TEMP SET 10\n"); drain(&tick);
    assert(strstr(output, "RESULT=ERROR") && snapshot.temperature_milli_c == 20000);
    allow = true;
    clear(); feed("PING\nPING\n"); TemperatureConsole_Process(tick);
    unsigned before = logs; TemperatureConsole_Process(tick + 99U); assert(logs == before);
    TemperatureConsole_Process(tick + 100U); assert(logs == before + 1);
    clear(); TemperatureConsole_Init(log_line);
    feed("PING\r"); drain(&tick); assert(logs == 0);
    feed("\n"); drain(&tick); assert(logs == 1);
    clear(); TemperatureConsole_Init(log_line); tick = UINT32_MAX - 50U;
    feed("PING\nPING\n"); TemperatureConsole_Process(tick);
    assert(logs == 1U);
    TemperatureConsole_Process(48U); assert(logs == 1U);
    TemperatureConsole_Process(49U); assert(logs == 2U);
    clear(); rd = wr = 0U; TemperatureConsole_Init(log_line);
    for (unsigned i=0; i<63U; ++i) { feed("X"); }
    feed("\n"); drain(&tick);
    assert(strstr(output, "REASON=COMMAND"));
    clear();
    for (unsigned i=0; i<64U; ++i) { feed("X"); }
    feed("\n"); drain(&tick);
    assert(strstr(output, "REASON=FRAME"));
    puts("PASS console: fragmentation, commands, limits, resync, rate/low guards");
    return 0;
}
