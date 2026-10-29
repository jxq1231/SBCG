/**
 * @file        codec_monitor.c
 * @brief       轮询编解码器标识并输出诊断。
 * @details     前台至少两秒间隔通过BSP读取芯片ID，累计成功和失败次数并输出日志。
 * @note        DHT11启动拉低期间应禁止I2C诊断；不在此配置音频采集通路。
 */
#include "codec_monitor.h"
#include "BSP/bsp_audio_codec.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#define CODEC_POLL_PERIOD_MS 2000U
#define CODEC_LOG_CAPACITY 192U

static CodecMonitor_Log log_line;
static uint32_t last_poll_ms, sequence, ok_count, error_count;

/**
 * @brief   复位编解码器ID轮询状态与日志接口。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   log 日志回调，可为NULL以关闭对应前台处理；非空回调须返回前消费或复制字节，不保留临时缓冲指针。
 * @return  无返回值。
 * @note    应用前台初始化，仅保存回调与计数，不访问I2C。
 */
void CodecMonitor_Init(uint32_t now_ms, CodecMonitor_Log log)
{
    log_line = log;
    last_poll_ms = now_ms;
    sequence = ok_count = error_count = 0U;
}

/**
 * @brief   将编解码器BSP状态映射为日志名称。
 * @param   status BSP读取ID的返回状态。
 * @return  静态只读字符串，不得修改或释放。
 * @note    未知状态映射为INVALID_ARG。
 */
static const char *StatusName(BSP_AudioCodecStatus status)
{
    switch (status)
    {
        case BSP_CODEC_OK: return "OK";
        case BSP_CODEC_BUSY: return "BUSY";
        case BSP_CODEC_IO_ERROR: return "IO_ERROR";
        case BSP_CODEC_ID_MISMATCH: return "ID_MISMATCH";
        default: return "INVALID_ARG";
    }
}

/**
 * @brief   放行且距上次至少两秒时读取芯片ID并输出诊断。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   io_allowed 是否允许I2C诊断；DHT11启动拉低期间必须为false。
 * @return  无返回值。
 * @note    由App_Process前台调度，错过周期不补发；ID读取成功不代表音频采集验收。
 */
void CodecMonitor_Process(uint32_t now_ms, bool io_allowed)
{
    if (!io_allowed || log_line == NULL ||
        (uint32_t)(now_ms - last_poll_ms) < CODEC_POLL_PERIOD_MS) { return; }
    last_poll_ms = now_ms;
    ++sequence;
    BSP_AudioCodecResult result;
    const BSP_AudioCodecStatus status = BSP_AudioCodec_ReadId(&result);
    if (status == BSP_CODEC_OK) { ++ok_count; }
    else { ++error_count; }

    char id_field[16] = "";
    if (result.id_valid)
    {
        (void)snprintf(id_field, sizeof id_field, ",ID=%04" PRIX16, result.id);
    }
    char line[CODEC_LOG_CAPACITY];
    const int length = snprintf(line, sizeof line,
        "$CODEC,SEQ=%" PRIu32 ",TICK_MS=%" PRIu32 ",ADDR7=%02X,RESULT=%s%s"
        ",HAL_ERR=%08" PRIX32 ",READ_MS=%" PRIu32 ",OK=%" PRIu32 ",ERRORS=%" PRIu32 "\r\n",
        sequence, now_ms, (unsigned)BSP_AUDIO_CODEC_ADDRESS_7BIT, StatusName(status), id_field,
        result.hal_error, result.read_ms, ok_count, error_count);
    if (length > 0 && (size_t)length < sizeof line) { log_line(line, length); }
}
