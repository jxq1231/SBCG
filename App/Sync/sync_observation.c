/**
 * @file        sync_observation.c
 * @brief       排队并限速计算已配对四时刻交换的观测结果。
 * @details     复制上游提供的序号、UID、会话和四时刻到4项CPU队列；每1000毫秒至多处理一项，年龄达到2000毫秒按状态6过期。
 * @note        NO_SYS前台串行使用；PC时刻为软件时间、板端为硬件时间，日志SYNC=0，不表示同步锁定。
 */

#include "sync_observation.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define OBSERVATION_PERIOD_MS 1000U
#define OBSERVATION_LINE_SIZE 256U
#define OBSERVATION_QUEUE_EXPIRED 6U

/* CPU-only storage, used serially from the NO_SYS main loop. No DMA shares
   this queue, and no clock/PTP state is changed by observation. */
static struct
{
    SyncObservationInput queue[SYNC_OBSERVATION_CAPACITY];
    uint32_t head;
    uint32_t count;
    uint32_t last_result_ms;
    uint32_t last_stats_ms;
    uint32_t queued;
    uint32_t calc_ok;
    uint32_t calc_err;
    uint32_t expired;
    uint32_t queue_full;
    uint32_t format_err;
    SyncObservation_Log log;
    bool initialized;
} observation;

/* newlib-nano need not support 64-bit printf. Convert the magnitude using
   unsigned arithmetic: -(INT64_MIN) would overflow a signed integer. */
/**
 * @brief   用无符号幅值运算格式化带符号64位十进制数。
 * @param   text 至少21字节可写缓冲，不可为NULL；包括负号及结尾零字符。
 * @param   value 待格式化值，支持INT64_MIN。
 * @return  无返回值。
 * @note    避免newlib-nano缺少64位printf格式转换。
 */
static void FormatSigned64(char text[21], int64_t value)
{
    char reversed[20];
    size_t digits = 0U;
    size_t length = 0U;
    uint64_t magnitude = (value < 0) ? UINT64_C(0) - (uint64_t)value : (uint64_t)value;
    do
    {
        reversed[digits++] = (char)('0' + magnitude % UINT64_C(10));
        magnitude /= UINT64_C(10);
    } while (magnitude != 0U);
    if (value < 0) { text[length++] = '-'; }
    while (digits != 0U) { text[length++] = reversed[--digits]; }
    text[length] = '\0';
}

/**
 * @brief   将一项交换标识和估计状态格式化为SYNC_OBS日志。
 * @param   line 不可为NULL的可写输出缓冲，不保存指针。
 * @param   capacity line可用容量，单位字节。
 * @param   input 不可为NULL的只读交换记录，仅本次借用。
 * @param   status 估计状态或队列过期状态6。
 * @param   result 仅status为OK时必须非NULL；其他状态允许NULL且不访问。
 * @return  snprintf所需字符数（不含零字符）；中间数值格式化失败返回-1，调用方还须检查最终截断。
 * @note    固定输出SYNC=0、PC=SW、BOARD=HW；成功仅是观测算术结果。
 */
static int FormatObservation(char *line, size_t capacity,
                             const SyncObservationInput *input,
                             unsigned int status, const SyncEstimate *result)
{
    char values[80] = "";
    if (status == SYNC_ESTIMATOR_OK)
    {
        char offset_text[21];
        char delay_text[21];
        FormatSigned64(offset_text, result->offset_x2_ns);
        FormatSigned64(delay_text, result->mean_path_delay_x2_ns);
        const int length = snprintf(values, sizeof(values),
            ",OFFSET_X2_NS=%s,DELAY_X2_NS=%s", offset_text, delay_text);
        if (length < 0 || (size_t)length >= sizeof(values)) { return -1; }
    }
    return snprintf(line, capacity,
        "$SYNC_OBS,SESSION=%08" PRIX32 "%08" PRIX32
        ",REQ_SEQ=%" PRIu32 ",CLIENT_TICK_MS=%" PRIu32
        ",UID_W0=%08" PRIX32 ",UID_W1=%08" PRIX32 ",UID_W2=%08" PRIX32
        ",STATUS=%u%s,SYNC=0,PC=SW,BOARD=HW\r\n",
        input->session_hi, input->session_lo, input->sequence,
        input->client_tick_ms, input->uid_words[0], input->uid_words[1],
        input->uid_words[2], status, values);
}

/**
 * @brief   检查格式化结果长度后提交给已初始化日志回调。
 * @param   line 有效且长度匹配的只读日志缓冲，不可为NULL；回调须在返回前消费或复制。
 * @param   length 格式化器返回的字符数，不含零字符；须在1至容量减1内。
 * @return  无返回值；无效长度累计格式错误且不输出。
 * @note    仅在已确认observation.log非NULL的前台处理路径中调用。
 */
static void EmitLine(const char *line, int length)
{
    if (length <= 0 || (size_t)length >= OBSERVATION_LINE_SIZE)
    {
        ++observation.format_err;
        return;
    }
    observation.log(line, length);
}

/**
 * @brief   清空观测队列、计数与节流状态并保存日志接口。
 * @param   now_ms 初始化毫秒计数；首项结果允许立即处理。
 * @param   log 日志回调，可为NULL以停用入队和处理；后续使用期间须有效。
 * @return  无返回值。
 * @note    NO_SYS串行前台调用；不改变MAC PTP或系统时钟。
 */
void SyncObservation_Init(uint32_t now_ms, SyncObservation_Log log)
{
    memset(&observation, 0, sizeof(observation));
    observation.log = log;
    observation.initialized = true;
    observation.last_result_ms = now_ms - OBSERVATION_PERIOD_MS;
    observation.last_stats_ms = now_ms;
}

/**
 * @brief   将上游已配对的完整交换复制到CPU观测队列。
 * @param   input 只读完整交换，可为NULL；成功复制内容，不保存指针或任何pbuf引用。
 * @return  true表示复制入队；false表示未初始化、无日志、输入为空或队列已满。
 * @note    在串行前台/RAW回调调用；不计算或输出日志，只有满队列拒绝会增加QUEUE_FULL。
 */
bool SyncObservation_Submit(const SyncObservationInput *input)
{
    if (!observation.initialized || observation.log == NULL || input == NULL)
    {
        return false;
    }
    if (observation.count == SYNC_OBSERVATION_CAPACITY)
    {
        ++observation.queue_full;
        return false;
    }
    const uint32_t tail = (observation.head + observation.count) % SYNC_OBSERVATION_CAPACITY;
    observation.queue[tail] = *input;
    ++observation.count;
    ++observation.queued;
    return true;
}

/**
 * @brief   限速取出一项交换，检查年龄后计算并输出结果与统计。
 * @param   now_ms 前台当前毫秒计数，按uint32_t差值处理节流及年龄回绕。
 * @return  无返回值。
 * @note    至多每1000毫秒出队一项；年龄达到TTL时STATUS=6且不调用估计器，统计有独立一秒节流。
 */
void SyncObservation_Process(uint32_t now_ms)
{
    if (!observation.initialized || observation.log == NULL) { return; }
    char line[OBSERVATION_LINE_SIZE];
    if (observation.count != 0U &&
        (uint32_t)(now_ms - observation.last_result_ms) >= OBSERVATION_PERIOD_MS)
    {
        const SyncObservationInput input = observation.queue[observation.head];
        observation.head = (observation.head + 1U) % SYNC_OBSERVATION_CAPACITY;
        --observation.count;
        observation.last_result_ms = now_ms;
        SyncEstimate result;
        unsigned int status;
        if ((uint32_t)(now_ms - input.created_ms) >= SYNC_OBSERVATION_TTL_MS)
        {
            status = OBSERVATION_QUEUE_EXPIRED;
            ++observation.expired;
        }
        else
        {
            status = (unsigned int)SyncEstimator_Calculate(&input.exchange,
                SYNC_OBSERVATION_WINDOW_NS, &result);
            if (status == SYNC_ESTIMATOR_OK) { ++observation.calc_ok; }
            else { ++observation.calc_err; }
        }
        EmitLine(line, FormatObservation(line, sizeof(line), &input, status,
                                        status == SYNC_ESTIMATOR_OK ? &result : NULL));
    }
    if ((uint32_t)(now_ms - observation.last_stats_ms) >= OBSERVATION_PERIOD_MS)
    {
        observation.last_stats_ms = now_ms;
        EmitLine(line, snprintf(line, sizeof(line),
            "$SYNC_OBS_STATS,QUEUED=%" PRIu32 ",CALC_OK=%" PRIu32
            ",CALC_ERR=%" PRIu32 ",EXPIRED=%" PRIu32 ",QUEUE_FULL=%" PRIu32
            ",FORMAT_ERR=%" PRIu32 "\r\n",
            observation.queued, observation.calc_ok, observation.calc_err,
            observation.expired, observation.queue_full, observation.format_err));
    }
}

/**
 * @brief   清除尚未计算的旧时间域配对，保留日志与累计统计。
 * @return  无返回值。
 * @note    仅串行前台，校时或频率变化边界调用；不重启外设，不重置输出序号/累计计数。
 */
void SyncObservation_NotifyClockChange(void)
{
    memset(observation.queue, 0, sizeof observation.queue);
    observation.head = observation.count = 0U;
}
