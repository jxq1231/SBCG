/**
 * @file        ptp_clock.c
 * @brief       初始化并观测本地以太网MAC PTP计时器。
 * @details     经BSP_PTP读取秒和纳秒，每秒最多输出一次状态，检测停滞或倒退；LOCAL及SYNC=0明确表示未建立同步。
 * @note        在LwIP/ETH初始化完成后由前台初始化和轮询；不执行跨节点时钟校正。
 */

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
/**
 * @brief   将64位纳秒值转换为十进制文本，避免依赖nano格式库的长整型支持。
 * @param   nanoseconds 待格式化的无符号纳秒值。
 * @param   text 至少21字节的可写缓冲区，不可为NULL；包含末尾零字符。
 * @return  指向text内首个数字的指针；其有效期随调用方缓冲区。
 */
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

/**
 * @brief   将PTP访问状态转换为只读日志名称。
 * @param   status BSP_PTP访问结果，未知枚举映射为UNKNOWN。
 * @return  静态字符串指针，调用方不得释放。
 */
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

/**
 * @brief   复位本地时钟观测状态，并调用BSP初始化MAC PTP计数器。
 * @param   now_ms 前台初始化毫秒计数，作为日志节流起点。
 * @param   log 日志回调，可为NULL；后续处理期间须有效。
 * @return  无返回值；BSP初始化状态保存至后续日志。
 * @note    在MX_LWIP_Init之后前台执行；初始化本地计数不代表与PC同步。
 */
void PtpClock_Init(uint32_t now_ms, PtpClock_Log log)
{
    emit = log;
    last_log_ms = now_ms;
    sequence = errors = 0U;
    previous_ns = 0U;
    have_previous = false;
    init_status = BSP_PTP_Init();
}

/**
 * @brief   每秒最多读取一次本地PTP时间并报告运行或错误状态。
 * @param   now_ms 前台当前毫秒计数，按无符号差值节流并允许回绕。
 * @return  无返回值。
 * @note    LOCAL日志固定SYNC=0；读失败、停滞或倒退累计错误，不执行校时。
 */
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
