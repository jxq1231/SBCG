/**
 * @file test_temperature_console.c
 * @brief 使用模拟串口验证前台命令的组帧、节流和模式诊断入口。
 * @details 分别编译普通版和诊断版，不访问硬件。
 * @note 使用断言验证命令输出和交给应用的参数。
 */
#include "Console/temperature_console.h"
#include "BSP/bsp_console.h"
#include "Temperature/temperature_service.h"
#ifdef SBCG_MODE_TEST_CONTROL
#include "app_main.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned symbols[2048], rd, wr, sets, autos, logs;
static char output[8192];
static TemperatureSnapshot snapshot;
static bool low, allow = true;

#ifdef SBCG_MODE_TEST_CONTROL
static uint64_t boot_id;
static ModeKind requested_mode;
static unsigned mode_reports;

/** @brief 模拟应用一次性接受启动代次。@param boot 非零代次。@return 首次有效输入为真。 */
bool App_ModeSetBoot(uint64_t boot)
{
    if (boot == 0U || boot_id != 0U) { return false; }
    boot_id = boot;
    return true;
}

/** @brief 记录请求参数。@param mode 诊断请求的模式。@return 已接收请求。 */
bool App_ModeRequest(ModeKind mode)
{
    requested_mode = mode;
    return true;
}

/** @brief 模拟输出模式快照。@return 无返回值，只递增次数。 */
void App_ReportMode(void) { ++mode_reports; }
#endif

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

/** @brief 按虚拟毫秒时间驱动并验证普通和诊断命令。@return 全部断言成功时返回0。 */
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
    clear();
    feed("MODE BOOT 18446744073709551616\nMODE BOOT 0\nMODE BOOT +1\n");
    drain(&tick);
#ifdef SBCG_MODE_TEST_CONTROL
    assert(boot_id == 0U && strstr(output, "NAME=MODE_BOOT,RESULT=ERROR,REASON=VALUE"));
    clear(); low = true;
    feed("MODE BOOT 18446744073709551615\n");
    TemperatureConsole_Process(tick);
    assert(boot_id == 0U && logs == 0U);
    low = false; drain(&tick);
    assert(boot_id == UINT64_MAX && strstr(output, "NAME=MODE_BOOT,RESULT=OK"));
    clear(); feed("MODE BOOT 1\nMODE SET WAVE\nMODE?\n"); drain(&tick);
    assert(boot_id == UINT64_MAX && strstr(output, "NAME=MODE_BOOT,RESULT=ERROR,REASON=UNAVAILABLE"));
    assert(requested_mode == MODE_WAVE && strstr(output, "NAME=MODE_SET,RESULT=OK"));
    assert(mode_reports == 1U);
    clear(); feed("MODE SET NOT_A_MODE\n"); drain(&tick);
    assert(requested_mode == MODE_WAVE && strstr(output, "NAME=MODE_SET,RESULT=ERROR,REASON=VALUE"));
#else
    assert(strstr(output, "NAME=INVALID,RESULT=ERROR,REASON=COMMAND"));
#endif
    puts("PASS console: fragmentation, commands, limits, resync, rate/low guards");
    return 0;
}
