/**
 * @file        audio_time_map_monitor.c
 * @brief       用三个顺序锚点检查本地音频时间映射。
 * @details     消费单槽观测，检查连续性与新鲜度；三个不重叠观测以两端估计中间进度，输出模型时间和残差。
 * @note        仅前台门控放行时执行，不是事件检测或PCM时间戳查询服务；SYNC=0。
 */
#include "audio_time_map_monitor.h"
#include "audio_time_anchor.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define OBSERVATION_MAX_AGE_MS 250U
#define LINE_CAPACITY 256U
_Static_assert(sizeof("$AUDIO_MAP,SEQ=4294967295,STATUS=OK,CORE=0,VALID=1,"
    "SESSION=4294967295,L=4294967295,M=4294967295,R=4294967295,"
    "FRAME=18446744073709551615,EST_NS=18446744073709551615,"
    "LO_NS=18446744073709551615,HI_NS=18446744073709551615,SYNC=0\r\n")
    <= LINE_CAPACITY, "map line exceeds application queue capacity");

static AudioTimeMapLog log_line;
static AudioTimeAnchorObservation window[3];
static uint32_t count, output_sequence, last_sequence, last_session;
static bool have_sequence, have_session;
/* Foreground owns this buffer and logger consumes/copies synchronously. */
static char line[LINE_CAPACITY];

/**
 * @brief   格式化无符号64位整数。
 * @param   value 待格式化整数。
 * @param   out 至少21字节可写缓冲，不可为NULL，输出NUL结尾。
 * @return  无返回值。
 * @note    仅前台，不保留缓冲指针。
 */
static void Decimal(uint64_t value, char out[21])
{
    char reversed[20]; unsigned n = 0U;
    do { reversed[n++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U);
    for (unsigned i=0U; i<n; ++i) { out[i] = reversed[n-i-1U]; }
    out[n] = '\0';
}
/**
 * @brief   格式化带符号64位整数并安全处理最小负值。
 * @param   value 待格式化整数。
 * @param   out 至少22字节可写缓冲，不可为NULL，输出NUL结尾。
 * @return  无返回值。
 * @note    负值通过分解处理，避免直接对INT64_MIN取负。
 */
static void SignedDecimal(int64_t value, char out[22])
{
    if (value < 0)
    {
        out[0] = '-';
        Decimal((uint64_t)(-(value + 1)) + 1U, out + 1);
    }
    else { Decimal((uint64_t)value, out); }
}
/**
 * @brief   将锚点秒和纳秒字段合成绝对纳秒数。
 * @param   a 已验证只读锚点，不可为NULL。
 * @return  本地PTP绝对纳秒数。
 * @note    不读取硬件，不改变输入；有效32位秒和纳秒组合可由uint64_t容纳。
 */
static uint64_t TimeNs(const AudioTimeMapAnchor *a)
{ return (uint64_t)a->sec * UINT64_C(1000000000) + a->nsec; }
/**
 * @brief   检查监测锚点局部字段和会话、上界溢出约束。
 * @param   a 只读锚点，不可为NULL。
 * @return  满足监测器局部约束返回true，否则false。
 * @note    序号、年龄和跨锚点顺序另由Process校验。
 */
static bool Valid(const AudioTimeMapAnchor *a)
{
    return a->valid && a->session != 0U && a->nsec < 1000000000U &&
           a->frame_hi >= a->frame_lo && a->frame_hi != UINT64_MAX &&
           a->frame_hi-a->frame_lo <= AUDIO_TIME_MAP_MAX_WIDTH_FRAMES;
}
/**
 * @brief   发送完整且未截断的映射监测日志。
 * @param   length snprintf返回长度；正值且小于静态line容量才发送。
 * @return  无返回值。
 * @note    前台使用共享line，须已确认log_line非NULL，不可重入。
 */
static void Emit(int length)
{ if (length > 0 && (size_t)length < sizeof line) { log_line(line,length); } }
/**
 * @brief   清空三锚点窗口并输出失败诊断。
 * @param   status 只读NUL结尾状态名，不可为NULL，调用期间有效。
 * @param   source 触发失败的锚点观测序号。
 * @param   core 核心返回码；非核心失败以OK占位。
 * @return  无返回值。
 * @note    前台递增输出序号；不清除最新观测序号及会话记录。
 */
static void Failure(const char *status, uint32_t source, AudioTimeMapStatus core)
{
    count = 0U;
    ++output_sequence;
    Emit(snprintf(line,sizeof line,
        "$AUDIO_MAP,SEQ=%" PRIu32 ",STATUS=%s,CORE=%u,VALID=0,SRC=%" PRIu32 ",SYNC=0\r\n",
        output_sequence,status,(unsigned)core,source));
}
/**
 * @brief   用窗口两端估计中间帧进度并输出模型与残差。
 * @return  无返回值。
 * @note    前台在window含三个已验证观测时调用；成功或核心失败都清窗，为不重叠三点诊断。
 */
static void Calculate(void)
{
    const AudioTimeMapAnchor *middle = &window[1].anchor;
    const uint64_t frame = middle->frame_lo + (middle->frame_hi-middle->frame_lo+1U)/2U;
    AudioTimeMapResult result;
    AudioTimeMapStatus status = AudioTimeMap_Estimate(&window[0].anchor,&window[2].anchor,
                                                    middle->session,frame,&result);
    if (status != AUDIO_TIME_MAP_OK) { Failure("CORE",window[2].sequence,status);return; }
    char target[21],estimate[21],lower[21],upper[21];
    Decimal(frame,target);Decimal(result.estimate_ns,estimate);
    Decimal(result.model_lower_ns,lower);Decimal(result.model_upper_ns,upper);
    ++output_sequence;
    Emit(snprintf(line,sizeof line,
        "$AUDIO_MAP,SEQ=%" PRIu32 ",STATUS=OK,CORE=0,VALID=1,SESSION=%" PRIu32
        ",L=%" PRIu32 ",M=%" PRIu32 ",R=%" PRIu32 ",FRAME=%s,EST_NS=%s,LO_NS=%s,HI_NS=%s,SYNC=0\r\n",
        output_sequence,middle->session,window[0].sequence,window[1].sequence,
        window[2].sequence,target,estimate,lower,upper));
    const uint64_t observed_ns = TimeNs(middle);
    /* Both validated absolute PTP values <= 4.295e18, safely below INT64_MAX. */
    const int64_t residual_ns = observed_ns >= result.estimate_ns ?
        (int64_t)(observed_ns-result.estimate_ns) : -(int64_t)(result.estimate_ns-observed_ns);
    char observed[21],residual[22];
    Decimal(observed_ns,observed);SignedDecimal(residual_ns,residual);
    Emit(snprintf(line,sizeof line,
        "$AUDIO_MAP_CHECK,SEQ=%" PRIu32 ",OBS_NS=%s,RES_NS=%s,SYNC=0\r\n",
        output_sequence,observed,residual));
    count = 0U;
}
/**
 * @brief   复位诊断窗口、连续性记录及输出序号。
 * @param   log 日志回调，可为NULL以关闭对应前台处理；非空回调须返回前消费或复制字节，不保留临时缓冲指针。
 * @return  无返回值。
 * @note    应用前台初始化，不消费已有锚点单槽、不改变外设。
 */
void AudioTimeMapMonitor_Init(AudioTimeMapLog log)
{
    log_line = log;count = output_sequence = last_sequence = last_session = 0U;
    have_sequence = have_session = false;
    memset(window,0,sizeof window);
}
/**
 * @brief   消费一个锚点观测并在三点齐备时留中检验。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   processing_allowed 本次是否允许前台处理；DHT11启动拉低期间应为false。
 * @return  无返回值。
 * @note    App_Process前台调度；超过250毫秒判陈旧，失败清窗；序号或会话变化时可用当前有效观测开启新窗。
 */
void AudioTimeMapMonitor_Process(uint32_t now_ms, bool processing_allowed)
{
    AudioTimeAnchorObservation observation;
    if (log_line == NULL || !processing_allowed || !AudioTimeAnchor_TakeObservation(&observation)) { return; }
    const bool sequence_gap = have_sequence && observation.sequence != last_sequence + 1U;
    have_sequence = true;last_sequence = observation.sequence;
    if (!Valid(&observation.anchor)) { Failure("ANCHOR",observation.sequence,AUDIO_TIME_MAP_OK);return; }
    if ((uint32_t)(now_ms-observation.captured_ms) > OBSERVATION_MAX_AGE_MS)
    { Failure("STALE",observation.sequence,AUDIO_TIME_MAP_OK);return; }
    if (sequence_gap) { Failure("SEQUENCE",observation.sequence,AUDIO_TIME_MAP_OK); }
    else if (have_session && observation.anchor.session != last_session)
    { Failure("SESSION",observation.sequence,AUDIO_TIME_MAP_OK); }
    else if (count != 0U && (TimeNs(&observation.anchor) <= TimeNs(&window[count-1U].anchor) ||
             observation.anchor.frame_lo <= window[count-1U].anchor.frame_hi))
    { Failure("ORDER",observation.sequence,AUDIO_TIME_MAP_OK);return; }
    last_session = observation.anchor.session;have_session = true;
    window[count++] = observation;
    if (count == 3U) { Calculate(); }
}

/**
 * @brief   清空三点窗口，禁止模型跨相位或速率修订插值。
 * @return  无返回值。
 * @note    仅串行前台，校时或频率变化边界调用；不重启外设，不重置输出序号/累计计数。
 */
void AudioTimeMapMonitor_NotifyClockChange(void)
{
    count = last_sequence = last_session = 0U;
    have_sequence = have_session = false;
    memset(window, 0, sizeof window);
}
