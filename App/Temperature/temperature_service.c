/**
 * @file        temperature_service.c
 * @brief       协调DHT11采集、温度声速有效性和手动模式。
 * @details     前台状态机跨主循环维持启动低电平，再调用BSP限时采帧；首个合格帧仅预热，后续发布温度与声速并输出日志。
 * @note        需UID白名单与非空日志；自动值须fresh且未满15秒，失败保留旧数据但撤销有效性，手动值至AUTO或重新初始化有效。
 */
#include "temperature_service.h"
#include "temperature_config.h"
#include "dht11_frame.h"
#include "sound_speed.h"
#include "BSP/bsp_dht11.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { TEMP_LINE_CAPACITY = 257 };

/* Each uint32_t occupies at most ten decimal characters. Include CRLF and
 * NUL in this compile-time bound; do not rely on typical small counters. */
_Static_assert(sizeof("$TEMP_STATS,ATT=4294967295,OK=4294967295,"
    "PRIME=4294967295,INIT=4294967295,BUS=4294967295,START=4294967295,"
    "TIMEOUT=4294967295,PULSE=4294967295,CLOCK=4294967295,"
    "CRC=4294967295,RANGE=4294967295,FORMAT=4294967295\r\n")
    <= TEMP_LINE_CAPACITY, "temperature statistics exceed UART line bound");
_Static_assert(sizeof("$TEMP,STATE=HOLD_LAST,VERSION=4294967295,"
    "T_MILLI_C=-20000,RH_MILLI_PCT=95000,C_MM_S=367660,"
    "AGE_MS=4294967295,FRESH=0,LAST=PRIMING\r\n") <= TEMP_LINE_CAPACITY,
    "temperature status exceeds UART line bound");
_Static_assert(sizeof("$TEMP,STATE=REMOTE,VERSION=4294967295,T_MILLI_C=-20000,"
    "C_MM_S=367660,AGE_MS=4294967295,FRESH=1,LAST=REMOTE,MODE=AUTO,"
    "SOURCE=BOARD_A\r\n") <= TEMP_LINE_CAPACITY,
    "remote temperature status exceeds UART line bound");

/** @brief 前台温度采集及格式诊断的饱和计数；不含UART传输错误。 */
typedef struct
{
    uint32_t attempts, successes, primes, init_errors, bus_errors;
    uint32_t start_errors, timeouts, pulse_errors, clock_errors;
    uint32_t checksum_errors, range_errors, format_errors;
} TemperatureStats;

/**
 * @brief   仅主循环持有的温度状态机与最新发布值。
 * @note    waiting表示启动低电平所有权；primed仅表明已有预热帧，have_value与fresh共同决定历史值能否继续使用。
 */
typedef struct
{
    TemperatureLog log;
    TemperatureStats stats;
    Dht11Sample sample;
    const char *last;
    uint32_t init_ms, last_attempt_ms, low_start_ms, last_log_ms;
    uint32_t valid_ms, version, sound_speed_mm_s;
    uint32_t remote_ms, remote_version, remote_speed_mm_s;
    int32_t remote_temperature_milli_c;
    bool enabled, ready, attempted, waiting, primed, have_value, fresh, manual;
    bool remote_valid, remote_manual;
} TemperatureService;

static TemperatureService service;

/**
 * @brief   查询服务是否正持有DHT11启动低电平。
 * @return  启动低电平尚未捕获或释放时返回true。
 * @note    仅主循环查询，供输出和诊断门控；不表示全部传感器或IRQ活动。
 */
bool TemperatureService_IsStartLowActive(void)
{
    return service.waiting;
}

/**
 * @brief   饱和递增温度诊断计数。
 * @param   counter 可写uint32_t计数器，不可为NULL。
 * @return  无返回值。
 * @note    仅前台，UINT32_MAX后保持，不提供IRQ同步。
 */
static void increment(uint32_t *counter)
{
    if (*counter < UINT32_MAX) { ++*counter; }
}

/**
 * @brief   复制温度、声速、版本及当前有效性。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   out 可写快照；NULL直接返回，非NULL先清零；有历史值时可携带valid=false的旧温度。
 * @return  无返回值。
 * @note    仅前台；自动值需fresh且年龄小于15秒，手动值不随年龄失效；调用方先判断valid。
 */
void TemperatureService_GetSnapshot(uint32_t now_ms, TemperatureSnapshot *out)
{
    if (out == NULL) { return; }
    memset(out, 0, sizeof *out);
    out->manual = service.manual;
    out->version = service.version;
    const bool local_valid = service.enabled && service.have_value
        && (service.manual || (service.fresh
            && (uint32_t)(now_ms - service.valid_ms) < TEMP_STALE_MS));
    out->valid = local_valid;
    if (service.have_value)
    {
        out->age_ms = now_ms - service.valid_ms;
        out->temperature_milli_c = service.sample.temperature_milli_c;
        out->sound_speed_mm_s = service.sound_speed_mm_s;
    }
    if (local_valid) { return; }
    /* B板本地DHT被UID白名单禁用；只显示A板共享值，不伪造本地样本。 */
    if (!service.enabled && service.remote_valid &&
        (uint32_t)(now_ms - service.remote_ms) < TEMP_STALE_MS)
    {
        out->manual = service.remote_manual;
        out->version = service.remote_version;
        out->valid = true;
        out->age_ms = now_ms - service.remote_ms;
        out->temperature_milli_c = service.remote_temperature_milli_c;
        out->sound_speed_mm_s = service.remote_speed_mm_s;
    }
}

bool TemperatureService_SetRemote(uint32_t now_ms, int32_t temperature_milli_c,
                                  uint32_t sound_speed_mm_s, uint32_t version,
                                  bool manual)
{
    uint32_t expected_speed;
    if(version==0U ||
       temperature_milli_c<TEMP_MIN_MILLI_C || temperature_milli_c>TEMP_MAX_MILLI_C ||
       !SoundSpeed_Calculate(temperature_milli_c,&expected_speed) ||
       sound_speed_mm_s!=expected_speed)
    { return false; }
    /* 缓存仍新鲜时拒绝旧版本；A重启后旧缓存超龄才接受新版本。 */
    if(service.remote_valid &&
       (uint32_t)(now_ms-service.remote_ms)<TEMP_STALE_MS &&
       version<service.remote_version)
    { return false; }
    service.remote_valid=true;
    service.remote_manual=manual;
    service.remote_temperature_milli_c=temperature_milli_c;
    service.remote_speed_mm_s=sound_speed_mm_s;
    service.remote_version=version;
    service.remote_ms=now_ms;
    service.last="REMOTE";
    return true;
}

/**
 * @brief   发布手动温度与声速并递增版本。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   temperature_milli_c 毫摄氏度手动温度，超配置范围拒绝。
 * @return  成功返回true；未启用、启动低电平中、版本耗尽或温度无效时false且不改服务状态。
 * @note    仅前台，不要求传感器初始化成功；湿度清零不是测量值，手动值持续至AUTO或重新初始化。
 */
bool TemperatureService_SetManual(uint32_t now_ms, int32_t temperature_milli_c)
{
    uint32_t sound_speed_mm_s;
    if (!service.enabled || service.waiting || service.version == UINT32_MAX
        || !SoundSpeed_Calculate(temperature_milli_c, &sound_speed_mm_s))
    { return false; }
    service.manual = true;
    service.have_value = service.fresh = true;
    service.sample.temperature_milli_c = temperature_milli_c;
    service.sample.relative_humidity_milli_pct = 0U; /* Not a manual input. */
    service.sound_speed_mm_s = sound_speed_mm_s;
    service.valid_ms = now_ms;
    ++service.version;
    service.last = "SET";
    return true;
}

/**
 * @brief   切回自动模式并撤销手动值及预热状态。
 * @return  启用且未持有低电平返回true；未启用或忙时false。
 * @note    仅前台；已在自动模式则不改状态，真正切换后重新预热才发布自动值。
 */
bool TemperatureService_UseAuto(void)
{
    if (!service.enabled || service.waiting) { return false; }
    if (!service.manual) { return true; }
    service.manual = false;
    service.have_value = service.fresh = service.primed = false;
    service.last = "AUTO_WAIT";
    return true;
}

/**
 * @brief   归类BSP失败并撤销新鲜与预热状态。
 * @param   result 非成功BSP状态；未知状态归入初始化错误。
 * @return  无返回值。
 * @note    前台调用，保留历史样本及版本，但快照有效性被撤销。
 */
static void bsp_failure(BSP_Dht11Status result)
{
    uint32_t *counter;
    service.primed = false;
    service.fresh = false;
    switch (result)
    {
    case BSP_DHT11_LINE_BUSY:
        counter = &service.stats.bus_errors; service.last = "BUS"; break;
    case BSP_DHT11_START_WINDOW:
        counter = &service.stats.start_errors; service.last = "START"; break;
    case BSP_DHT11_TIMEOUT:
        counter = &service.stats.timeouts; service.last = "TIMEOUT"; break;
    case BSP_DHT11_PULSE_ERROR:
        counter = &service.stats.pulse_errors; service.last = "PULSE"; break;
    case BSP_DHT11_CLOCK_ERROR:
        counter = &service.stats.clock_errors; service.last = "CLOCK"; break;
    default:
        /* ARGUMENT is an internal invariant failure, never a sensor value. */
        counter = &service.stats.init_errors; service.last = "INIT"; break;
    }
    increment(counter);
}

/**
 * @brief   解码合格帧并预热或发布温度及声速。
 * @param   frame BSP采集的只读5字节帧，不可为NULL，仅调用期间有效。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @return  无返回值。
 * @note    仅前台；首个连续合格帧只预热，后续才更新版本；失败撤销fresh与预热，版本耗尽不发布。
 */
static void accept_frame(const uint8_t frame[5], uint32_t now_ms)
{
    Dht11Sample sample;
    uint32_t sound_speed_mm_s;
    Dht11FrameStatus result = Dht11Frame_Decode(frame, &sample);

    if (result != DHT11_FRAME_OK)
    {
        service.primed = false;
        service.fresh = false;
        if (result == DHT11_FRAME_CHECKSUM)
        {
            increment(&service.stats.checksum_errors); service.last = "CRC";
        }
        else
        {
            increment(&service.stats.range_errors); service.last = "RANGE";
        }
        return;
    }
    if (!SoundSpeed_Calculate(sample.temperature_milli_c, &sound_speed_mm_s))
    {
        service.primed = false;
        service.fresh = false;
        increment(&service.stats.range_errors);
        service.last = "RANGE";
        return;
    }
    if (!service.primed)
    {
        /* DHT11 may initially return its previous internal measurement. */
        service.primed = true;
        service.fresh = false;
        increment(&service.stats.primes);
        service.last = "PRIMING";
        return;
    }
    if (service.version == UINT32_MAX)
    {
        service.fresh = false;
        service.last = "VERSION";
        return;
    }
    service.sample = sample;
    service.sound_speed_mm_s = sound_speed_mm_s;
    service.valid_ms = now_ms;
    ++service.version;
    increment(&service.stats.successes);
    service.have_value = true;
    service.fresh = true;
    service.last = "OK";
}

/**
 * @brief   发送温度日志或累计本地格式错误。
 * @param   line 只读日志字节，不可为NULL；回调同步消费或复制。
 * @param   length snprintf长度，大于0且小于TEMP_LINE_CAPACITY才发送。
 * @return  无返回值。
 * @note    只从已确认service.log非NULL的前台路径调用；回调无返回值，传输错误不在此统计。
 */
static void emit_line(const char *line, int length)
{
    if (length > 0 && length < TEMP_LINE_CAPACITY)
    {
        service.log(line, length);
    }
    else
    {
        /* The callback is void: UART transport errors belong to the caller.
         * FORMAT counts only local snprintf failures/truncation. */
        increment(&service.stats.format_errors);
    }
}

/**
 * @brief   按至少一秒间隔输出状态与统计两行。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @return  无返回值。
 * @note    仅前台且总线已释放时调用，不补发错过周期；HOLD_LAST保留数值但不新鲜。
 */
static void log_status(uint32_t now_ms)
{
    char line[TEMP_LINE_CAPACITY];
    int length;
    const char *state;
    uint32_t age_ms = now_ms - service.valid_ms;

    if (service.log == NULL
        || (uint32_t)(now_ms - service.last_log_ms) < TEMP_LOG_PERIOD_MS)
    {
        return;
    }
    /* One status and one statistics line per elapsed second, without catchup.
     * Call only with the bus released so blocking UART cannot extend low. */
    service.last_log_ms = now_ms;
    if (!service.enabled && service.remote_valid)
    {
        const uint32_t remote_age=now_ms-service.remote_ms;
        state=remote_age>=TEMP_STALE_MS?"STALE":"REMOTE";
        length=snprintf(line,sizeof line,
            "$TEMP,STATE=%s,VERSION=%" PRIu32 ",T_MILLI_C=%" PRId32
            ",C_MM_S=%" PRIu32 ",AGE_MS=%" PRIu32
            ",FRESH=%u,LAST=REMOTE,MODE=%s,SOURCE=BOARD_A\r\n",
            state,service.remote_version,service.remote_temperature_milli_c,
            service.remote_speed_mm_s,remote_age,
            (unsigned)(remote_age<TEMP_STALE_MS),
            service.remote_manual?"MANUAL":"AUTO");
        emit_line(line,length);
        return;
    }
    if (!service.enabled) { state = "DISABLED"; }
    else if (service.manual) { state = "MANUAL"; }
    else if (!service.have_value) { state = "UNREADY"; }
    else if (age_ms >= TEMP_STALE_MS) { state = "STALE"; }
    else if (!service.fresh) { state = "HOLD_LAST"; }
    else { state = "OK"; }

    if (service.manual)
    {
        length = snprintf(line, sizeof line,
            "$TEMP,STATE=MANUAL,VERSION=%" PRIu32 ",T_MILLI_C=%" PRId32
            ",C_MM_S=%" PRIu32 ",AGE_MS=%" PRIu32
            ",FRESH=1,LAST=SET,MODE=MANUAL,SOURCE=MANUAL\r\n",
            service.version, service.sample.temperature_milli_c,
            service.sound_speed_mm_s, age_ms);
    }
    else if (service.have_value)
    {
        length = snprintf(line, sizeof line,
            "$TEMP,STATE=%s,VERSION=%" PRIu32 ",T_MILLI_C=%" PRId32
            ",RH_MILLI_PCT=%" PRIu32 ",C_MM_S=%" PRIu32
            ",AGE_MS=%" PRIu32 ",FRESH=%u,LAST=%s\r\n",
            state, service.version, service.sample.temperature_milli_c,
            service.sample.relative_humidity_milli_pct,
            service.sound_speed_mm_s, age_ms,
            (unsigned)(service.fresh && age_ms < TEMP_STALE_MS), service.last);
    }
    else
    {
        length = snprintf(line, sizeof line,
            "$TEMP,STATE=%s,VERSION=%" PRIu32 ",FRESH=0,LAST=%s\r\n",
            state, service.version, service.last);
    }
    emit_line(line, length);
    length = snprintf(line, sizeof line,
        "$TEMP_STATS,ATT=%" PRIu32 ",OK=%" PRIu32 ",PRIME=%" PRIu32
        ",INIT=%" PRIu32 ",BUS=%" PRIu32 ",START=%" PRIu32
        ",TIMEOUT=%" PRIu32 ",PULSE=%" PRIu32 ",CLOCK=%" PRIu32
        ",CRC=%" PRIu32 ",RANGE=%" PRIu32 ",FORMAT=%" PRIu32 "\r\n",
        service.stats.attempts, service.stats.successes, service.stats.primes,
        service.stats.init_errors, service.stats.bus_errors,
        service.stats.start_errors, service.stats.timeouts,
        service.stats.pulse_errors, service.stats.clock_errors,
        service.stats.checksum_errors, service.stats.range_errors,
        service.stats.format_errors);
    emit_line(line, length);
}

/**
 * @brief   复位服务并按UID白名单决定是否初始化传感器BSP。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   uid_words 只读3字芯片UID；NULL禁用，调用内比较，不保存指针。
 * @param   log 日志回调，NULL禁用服务；须返回前消费或复制字节。
 * @return  无返回值。
 * @note    应用前台初始化；重入初始化先中止本服务持有的低电平，BSP初始化失败仅计一次且不自动重试初始化。
 */
void TemperatureService_Init(uint32_t now_ms, const uint32_t uid_words[3],
                             TemperatureLog log)
{
    if (service.waiting) { BSP_DHT11_Abort(); }
    memset(&service, 0, sizeof service);
    service.log = log;
    service.init_ms = now_ms;
    service.last_log_ms = now_ms;
    service.last = "DISABLED";
    service.enabled = log != NULL && uid_words != NULL
        && uid_words[0] == TEMP_TEST_UID_W0
        && uid_words[1] == TEMP_TEST_UID_W1
        && uid_words[2] == TEMP_TEST_UID_W2;
    if (service.enabled)
    {
        BSP_Dht11Status result = BSP_DHT11_Init();
        service.ready = result == BSP_DHT11_OK;
        service.last = "INIT";
        /* Init is a one-shot prerequisite. Its failure is counted once as
         * INIT; CLOCK diagnoses failures during subsequent acquisition. */
        if (!service.ready) { increment(&service.stats.init_errors); }
    }
}

/**
 * @brief   推进启动低电平、限时采帧、发布和日志状态机。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @return  无返回值。
 * @note    App_Process重复调度，非精确周期；至少20毫秒后捕获，超过30毫秒中止；上电至少1秒后首次尝试，后续至少5秒间隔。
 */
void TemperatureService_Process(uint32_t now_ms)
{
    if (service.waiting)
    {
        uint8_t frame[5];
        BSP_Dht11Status result;
        /* No formatting or UART work is permitted until capture releases low. */
        if ((uint32_t)(now_ms - service.low_start_ms) < TEMP_START_LOW_MS)
        {
            return;
        }
        /* HAL tick also bounds long scheduler stalls across a full DWT wrap.
         * DWT performs the precise microsecond check for ordinary attempts. */
        if ((uint32_t)(now_ms - service.low_start_ms) > TEMP_START_MAX_US / 1000U)
        {
            BSP_DHT11_Abort();
            result = BSP_DHT11_START_WINDOW;
        }
        else { result = BSP_DHT11_Capture(frame); }
        service.waiting = false;
        if (result == BSP_DHT11_OK) { accept_frame(frame, now_ms); }
        else { bsp_failure(result); }
        log_status(now_ms);
        return;
    }

    if (service.enabled && service.ready && !service.manual
        && ((!service.attempted
             && (uint32_t)(now_ms - service.init_ms) >= TEMP_POWERUP_MS)
            || (service.attempted
                && (uint32_t)(now_ms - service.last_attempt_ms) >= TEMP_PERIOD_MS)))
    {
        BSP_Dht11Status result;
        if (service.attempted
            && (uint32_t)(now_ms - service.last_attempt_ms) >= TEMP_STALE_MS)
        {
            service.primed = false;
            service.fresh = false;
        }
        service.attempted = true;
        service.last_attempt_ms = now_ms;
        increment(&service.stats.attempts);
        result = BSP_DHT11_Start();
        if (result == BSP_DHT11_OK)
        {
            service.waiting = true;
            service.low_start_ms = now_ms;
            return;
        }
        bsp_failure(result);
    }
    log_status(now_ms);
}
