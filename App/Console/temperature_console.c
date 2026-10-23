/**
 * @file        temperature_console.c
 * @brief       解析串口温度命令并返回环境快照。
 * @details     每次至多读取64字节，按LF分帧且回复间隔至少100毫秒，支持PING、TEMP?、TEMP SET和TEMP AUTO。
 * @note        仅前台；依赖控制台接收BSP和温度服务，DHT11启动低电平期间暂停，非法/超长/接收间隙帧丢弃至LF。
 */
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

/**
 * @brief   将完整控制台回复交给日志回调。
 * @param   line 只读回复字节，不可为NULL；回调返回前消费或复制。
 * @param   length 字节数，正值且小于REPLY_CAPACITY才发送。
 * @return  无返回值。
 * @note    仅前台，logger为空时跳过；不返回传输错误。
 */
static void emit(const char *line, int length)
{
    if (logger != NULL && length > 0 && length < REPLY_CAPACITY)
    { logger(line, length); }
}

/**
 * @brief   格式化并发送命令结果及原因。
 * @param   name 只读NUL结尾命令名，不可为NULL。
 * @param   outcome 只读NUL结尾结果名，不可为NULL。
 * @param   reason 只读NUL结尾原因名，不可为NULL。
 * @return  无返回值。
 * @note    仅前台且不可重入，覆写本模块共享reply缓冲。
 */
static void result(const char *name, const char *outcome, const char *reason)
{
    int length = snprintf(reply, sizeof reply, "$CMD,NAME=%s,RESULT=%s,REASON=%s\r\n",
                          name, outcome, reason);
    emit(reply, length);
}

/**
 * @brief   读取温度快照并回复模式、来源及有效数值。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @return  无返回值。
 * @note    仅前台；valid=false时不输出温度与声速，避免把保留旧值当有效值。
 */
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

/**
 * @brief   解析可选负号、一至两位整数及可选一位小数。
 * @param   text 只读NUL结尾温度文本，不可为NULL，不接受加号、空格或尾字符。
 * @param   out 可写毫摄氏度输出，不可为NULL；语法失败不写，范围失败可能已写越界值。
 * @return  语法及范围均合格返回true，否则false。
 * @note    仅解析不改服务，false时调用方不得使用输出。
 */
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

/**
 * @brief   执行当前完整命令并输出回复。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @return  无返回值。
 * @note    前台在command已NUL结尾且低电平门控放行后调用；SET/AUTO更改服务，TEMP?另输出接收统计。
 */
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

/**
 * @brief   复位组帧、丢弃和回复节流状态。
 * @param   log 日志回调，可为NULL以关闭对应前台处理；非空回调须返回前消费或复制字节，不保留临时缓冲指针。
 * @return  无返回值。
 * @note    应用前台初始化，不初始化串口接收硬件。
 */
void TemperatureConsole_Init(TemperatureConsole_Log log)
{
    logger = log;
    used = 0U;
    discard = replied = false;
    last_reply_ms = 0U;
}

/**
 * @brief   放行时读取有限字节并执行至多一条非空命令。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @return  无返回值。
 * @note    App_Process前台调度，每次最多64字节，回复间隔至少100毫秒；接收间隙/非法字符/超长丢弃至LF，接受末尾CRLF。
 */
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
