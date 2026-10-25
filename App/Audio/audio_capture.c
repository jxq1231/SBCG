/**
 * @file        audio_capture.c
 * @brief       消费音频半缓冲并输出电平、帧位置及归还验证后的声音候选和有界扫频匹配。
 * @details     前台经BSP领取一个已完成的8192帧双声道PCM16块，计算统计并在归还验证成功后发布；按毫秒差节流输出观测日志。
 * @note        依赖已生成的SAI/DMA及音频BSP；处理门控只暂停前台消费，不停止DMA。扫频只在原认领中复制，End通过后分批处理副本。
 */
#include "audio_capture.h"
#ifdef SBCG_USE_SOUND_DETECTOR
#include "Detector/sound_detector_service.h"
#include "BSP/bsp_console.h"
#endif
#ifdef SBCG_USE_CHIRP
#include "Detector/chirp_service.h"
#include "BSP/bsp_console.h"
#endif
#include "audio_statistics.h"
#include "BSP/bsp_audio.h"
#include "BSP/bsp_console.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#define AUDIO_LOG_PERIOD_MS 1000U
static AudioCapture_Log log_line;
static uint32_t initialized_ms, last_log_ms, sequence, level_ms;
static uint32_t previous_callbacks, previous_callback_ms;
static bool have_rate_snapshot, level_valid;
static AudioStatistics levels;
/* Metadata only; the stored DMA pointer is never dereferenced after End. */
static BSP_AudioBlock level_block;
static AudioCaptureProfile max_profile;

/* newlib-nano and native MinGW differ in long-long printf support. Keep
   the observed-frame counter portable without enabling larger printf libs. */
/**
 * @brief   将64位无符号数转成十进制文本。
 * @param   value 待格式化数值。
 * @param   output 调用方提供的至少21字节可写缓冲，不可为NULL；写入NUL结尾文本。
 * @return  无返回值。
 * @note    仅前台使用；避免依赖精简printf的64位整数支持。
 */
static void Decimal64(uint64_t value, char output[21])
{
    char reverse[20];
    unsigned count = 0U;
    do { reverse[count++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U);
    for (unsigned i = 0; i < count; ++i) { output[i] = reverse[count - i - 1U]; }
    output[count] = '\0';
}

/**
 * @brief   复位前台统计并通过BSP停止旧采集、尝试启动新采集。
 * @param   now_ms 前台获取的当前HAL毫秒计数；无符号差用于处理常规32位回绕。
 * @param   log 日志回调，可为NULL以关闭对应前台处理；非空回调须在返回前消费或复制字节，不保留临时缓冲指针。
 * @return  无返回值。
 * @note    在生成外设初始化后、前台处理中调用；无日志或停止失败时不启动新采集，结果写入AUDIO_INIT日志。
 */
void AudioCapture_Init(uint32_t now_ms, AudioCapture_Log log)
{
    log_line = log;
    initialized_ms = last_log_ms = now_ms;
    sequence = level_ms = previous_callbacks = previous_callback_ms = 0U;
    have_rate_snapshot = level_valid = false;
    levels = (AudioStatistics){0};
    level_block = (BSP_AudioBlock){0};
    max_profile=(AudioCaptureProfile){0};
    const bool stopped = BSP_Audio_Stop();
    const bool started = stopped && log != NULL && BSP_Audio_Start();
#ifdef SBCG_USE_SOUND_DETECTOR
    (void)SoundDetectorService_Init(now_ms,&SoundDetectorService_DefaultConfig,0U,started ? log:NULL);
#endif
#ifdef SBCG_USE_CHIRP
    (void)ChirpService_Init(now_ms,0U,started ? log:NULL);
#endif
    if (log == NULL) { return; }
    BSP_AudioSnapshot snapshot;
    BSP_Audio_GetSnapshot(&snapshot);
    char line[224];
    const int length = snprintf(line, sizeof line,
        "$AUDIO_INIT,RESULT=%s,NOMINAL_HZ=%u,DERIVED_MILLI_HZ=%" PRIu32
        ",START_ERR=%" PRIu32 ",CODEC=%" PRIu32 ",I2C_ERR=%08" PRIX32
        ",REG=%04" PRIX16 ",WRITES=%" PRIu16 "\r\n",
        started ? "OK" : "ERROR", BSP_AUDIO_NOMINAL_HZ, snapshot.derived_milli_hz,
        snapshot.start_error, snapshot.codec_status, snapshot.codec_hal_error,
        snapshot.codec_register, snapshot.codec_writes);
    if (length > 0 && (size_t)length < sizeof line) { log(line, length); }
}

/**
 * @brief   尝试消费一个DMA半块并按至少一秒间隔输出音频状态。
 * @param   now_ms 前台获取的当前HAL毫秒计数；无符号差用于处理常规32位回绕。
 * @param   processing_allowed 是否允许本次前台处理；DHT11启动拉低期间应为false。
 * @return  无返回值。
 * @note    由App_Process调度，非精确周期；Begin成功后必调用End，End通过才提交电平及候选。
 *          扫频在借用内仅复制，失败清历史；归还后每块扫描一次8192帧、每轮至多8窗，不再解引用DMA指针。
 *          同时记录最大一轮AudioCapture内部分解，仅测量；日志仍按原1秒节流。
 */
/**
 * @brief 结束一次AudioCapture轮次的分段计时并只在更慢轮保存分解。
 * @param entry_ms 本轮入口tick；profile 本轮各分段，函数内补total/other。
 * @return 无返回值；仅前台写静态最大快照，不改变业务状态。
 */
static void ProfileFinish(uint32_t entry_ms, AudioCaptureProfile *profile)
{
    const uint32_t total_ms = BSP_Console_GetTickMs() - entry_ms;
    profile->total_ms = total_ms;
    const uint32_t known = profile->io_ms + profile->stats_ms + profile->sound_stage_ms
        + profile->sound_process_ms + profile->chirp_stage_ms + profile->chirp_complete_ms
        + profile->chirp_process_ms + profile->log_ms;
    profile->other_ms = total_ms > known ? total_ms - known : 0U;
    if (total_ms > max_profile.total_ms) { max_profile = *profile; }
}
void AudioCapture_Process(uint32_t now_ms, bool processing_allowed)
{
    if (log_line == NULL || !processing_allowed) { return; }
    const uint32_t profile_entry_ms = BSP_Console_GetTickMs();
    AudioCaptureProfile profile = {0};
    BSP_AudioBlock block;
    uint32_t mark = BSP_Console_GetTickMs();
    if (BSP_Audio_BeginBlock(&block))
    {
        profile.io_ms += BSP_Console_GetTickMs() - mark;
        AudioStatistics next;
        mark = BSP_Console_GetTickMs();
        const bool computed = AudioStatistics_Compute(block.samples, BSP_AUDIO_HALF_FRAMES, &next);
        profile.stats_ms = BSP_Console_GetTickMs() - mark;
#ifdef SBCG_USE_CHIRP
        /* 只复制选定声道，相关处理必须等真实End验证通过。 */
        mark = BSP_Console_GetTickMs();
        (void)ChirpService_Stage(BSP_Console_GetTickMs(),&block);
        profile.chirp_stage_ms = BSP_Console_GetTickMs() - mark;
#endif
#ifdef SBCG_USE_SOUND_DETECTOR
        /* 暂算不能代表DMA仍有效；候选只在真实End结果之后提交。 */
        /* Begin可能刚好跨DMA IRQ，入口now_ms可能早于completed_ms；取新鲜时钟。 */
        mark = BSP_Console_GetTickMs();
        (void)SoundDetectorService_Stage(BSP_Console_GetTickMs(),&block);
        profile.sound_stage_ms = BSP_Console_GetTickMs() - mark;
#endif
        mark = BSP_Console_GetTickMs();
        const bool accepted=BSP_Audio_EndBlock(&block, computed);
        profile.io_ms += BSP_Console_GetTickMs() - mark;
#ifdef SBCG_USE_CHIRP
        mark = BSP_Console_GetTickMs();
        (void)ChirpService_Complete(accepted);
        profile.chirp_complete_ms = BSP_Console_GetTickMs() - mark;
#endif
#ifdef SBCG_USE_SOUND_DETECTOR
        mark = BSP_Console_GetTickMs();
        (void)SoundDetectorService_Complete(accepted);
        profile.io_ms += BSP_Console_GetTickMs() - mark;
#endif
        if (accepted)
        { levels = next; level_ms = block.completed_ms; level_block = block; level_valid = true; }
        else { level_valid = false; }
    }
    else { profile.io_ms += BSP_Console_GetTickMs() - mark; }
#ifdef SBCG_USE_SOUND_DETECTOR
    /* 归还后才检查采集会话并限速输出；DHT门控已在函数入口处理。 */
    BSP_AudioSnapshot sound_snapshot;
    BSP_Audio_GetSnapshot(&sound_snapshot);
    /* 快照之后再读tick，避免新回调时间晚于旧入口时间而无符号下溢。 */
    now_ms=BSP_Console_GetTickMs();
    mark = BSP_Console_GetTickMs();
    SoundDetectorService_Process(now_ms,&sound_snapshot);
    profile.sound_process_ms = BSP_Console_GetTickMs() - mark;
#endif
#ifdef SBCG_USE_CHIRP
    BSP_AudioSnapshot chirp_snapshot;
    BSP_Audio_GetSnapshot(&chirp_snapshot);
    mark = BSP_Console_GetTickMs();
    ChirpService_Process(BSP_Console_GetTickMs(),&chirp_snapshot);
    profile.chirp_process_ms = BSP_Console_GetTickMs() - mark;
#endif
    if ((uint32_t)(now_ms - last_log_ms) < AUDIO_LOG_PERIOD_MS)
    { ProfileFinish(profile_entry_ms,&profile); return; }
    const uint32_t log_begin_ms = BSP_Console_GetTickMs();
    BSP_AudioSnapshot snapshot;
    BSP_Audio_GetSnapshot(&snapshot);
#if defined(SBCG_USE_SOUND_DETECTOR) || defined(SBCG_USE_CHIRP)
    /* 候选/状态发送也可能跨过DMA回调；音频摘要的第二次快照同样要后读时间。 */
    now_ms=BSP_Console_GetTickMs();
#endif
    last_log_ms = now_ms;
    const uint32_t age_ms = snapshot.callback_count == 0U ? now_ms - initialized_ms
                                                        : now_ms - snapshot.last_callback_ms;
    const char *name = !snapshot.running ? "ERROR" : age_ms >= AUDIO_LOG_PERIOD_MS ? "STALLED" : "RUN";
    const bool valid = snapshot.running && age_ms < AUDIO_LOG_PERIOD_MS && level_valid &&
                       level_block.session == snapshot.session &&
                       (uint32_t)(now_ms - level_ms) < AUDIO_LOG_PERIOD_MS;
    uint32_t count = 0U, elapsed_ms = 0U;
    if (have_rate_snapshot)
    {
        count = snapshot.callback_count - previous_callbacks;
        elapsed_ms = snapshot.last_callback_ms - previous_callback_ms;
    }
    else if (snapshot.callback_count > 1U)
    {
        count = snapshot.callback_count - 1U;
        elapsed_ms = snapshot.last_callback_ms - snapshot.first_callback_ms;
    }
    uint32_t rate_hz = 0U;
    if (elapsed_ms != 0U)
    {
        const uint64_t rate = (uint64_t)count * BSP_AUDIO_HALF_FRAMES * 1000U / elapsed_ms;
        if (rate <= UINT32_MAX) { rate_hz = (uint32_t)rate; }
    }
    previous_callbacks = snapshot.callback_count;
    previous_callback_ms = snapshot.last_callback_ms;
    have_rate_snapshot = snapshot.callback_count != 0U;
    char level_fields[64] = "";
    if (valid)
    {
        (void)snprintf(level_fields, sizeof level_fields,
            ",P0=%" PRIu32 ",P1=%" PRIu32 ",R0=%" PRIu32 ",R1=%" PRIu32,
            levels.channel[0].peak, levels.channel[1].peak,
            levels.channel[0].rms, levels.channel[1].rms);
    }
    char frame_text[21];
    Decimal64(snapshot.observed_frames, frame_text);
    char line[256];
    const int length = snprintf(line, sizeof line,
        "$AUDIO,SEQ=%" PRIu32 ",STATE=%s,FRAMES=%s,H=%" PRIu32
        ",F=%" PRIu32 ",OK=%" PRIu32 ",DROP=%" PRIu32 ",ERR=%" PRIu32
        ",HAL=%08" PRIX32 ",DMA=%08" PRIX32 ",RATE_HZ=%" PRIu32 ",VALID=%u%s\r\n",
        ++sequence, name, frame_text, snapshot.half_count, snapshot.full_count,
        snapshot.processed_blocks, snapshot.dropped_blocks, snapshot.error_count,
        snapshot.hal_error, snapshot.dma_error, rate_hz, valid ? 1U : 0U, level_fields);
    if (length > 0 && (size_t)length < sizeof line) { log_line(line, length); }
    const bool index_valid = snapshot.running && snapshot.session != 0U && snapshot.index_faults == 0U;
    const bool position_valid = valid && index_valid && level_block.index_valid;
    char first_text[21], gap_text[21];
    Decimal64(position_valid ? level_block.first_frame : 0U, first_text);
    Decimal64(position_valid ? level_block.gap_frames : 0U, gap_text);
    const int position_length = snprintf(line, sizeof line,
        "$AUDIO_POS,SEQ=%" PRIu32 ",SESSION=%" PRIu32 ",OBS_FRAMES=%s,FIRST=%s"
        ",COUNT=%" PRIu32 ",GAP=%s,INDEX_VALID=%u,CONTIG=%u,FAULTS=%08" PRIX32 ",VALID=%u\r\n",
        sequence, snapshot.session, frame_text, first_text,
        position_valid ? level_block.frame_count : 0U, gap_text,
        index_valid ? 1U : 0U, position_valid && level_block.contiguous ? 1U : 0U,
        snapshot.index_faults, position_valid ? 1U : 0U);
    if (position_length > 0 && (size_t)position_length < sizeof line)
    { log_line(line, position_length); }
    profile.log_ms = BSP_Console_GetTickMs() - log_begin_ms;
    ProfileFinish(profile_entry_ms,&profile);
}
/**
 * @brief 复制最大一轮AudioCapture内部耗时分解。
 * @param out 非NULL调用方输出；NULL不写。
 * @return 无返回值。
 * @note 仅诊断；不清最大累计值，不授予PCM访问权。
 */
void AudioCapture_GetProfile(AudioCaptureProfile *out)
{ if (out != NULL) { *out = max_profile; } }
