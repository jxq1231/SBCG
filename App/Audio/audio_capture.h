/**
 * @file        audio_capture.h
 * @brief       消费音频DMA半缓冲并输出电平、帧位置及已验证声音候选。
 * @details     声明本模块接口、数据类型或配置常量。经BSP领取8192帧双声道PCM16块，计算峰值/RMS并在归还验证成功后发布；状态日志按至少一秒间隔输出。
 * @note        依赖生成的SAI/DMA及音频BSP；前台门控不停止DMA。
 */
#ifndef SBCG_AUDIO_CAPTURE_H
#define SBCG_AUDIO_CAPTURE_H
#include <stdbool.h>
#include <stdint.h>
/**
 * @brief   前台日志输出回调类型。
 * @param   line 只读日志字节，仅回调期间有效；返回前须消费或复制，不可保留指针。
 * @param   length 有效字节数，不含NUL；当前发送路径仅传正数。
 * @return  无返回值；传输错误由应用或BSP统计。
 * @note    当前应用接入SendLine，同步发送或复制入有界队列，不从IRQ调用。
 */
typedef void (*AudioCapture_Log)(const char *line, int length);
/**
 * @brief   复位前台统计并通过BSP停止旧采集、尝试启动新采集。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   log 日志回调，可为NULL以关闭对应前台处理；非空回调须返回前消费或复制字节，不保留临时缓冲指针。
 * @return  无返回值。
 * @note    生成外设初始化后在前台调用；日志为空或停止失败时不启动，结果见AUDIO_INIT。
 */
void AudioCapture_Init(uint32_t now_ms, AudioCapture_Log log);
#ifdef SBCG_USE_MODE_OBSERVATION
/** @brief 当前模式允许分类和扫频匹配的首个完整PCM半块位置。 */
typedef struct {
    uint64_t first_frame;
    uint32_t session;
    bool controlled, sound_enabled, chirp_enabled;
} AudioCaptureSoundBoundary;

/**
 * @brief 判断同一会话的帧是否越过边界；更晚启动的会话全部属于新模式。
 * @param boundary 非NULL只读模式边界。
 * @param session 不复用的本次开机内采集会话。
 * @param first_frame 半块首帧或已验证TOA帧，单位为双声道帧。
 * @return true允许进入声音业务，未登记模式时保持兼容。
 */
static inline bool AudioCapture_SoundFrameAllowed(const AudioCaptureSoundBoundary *boundary,
                                                  uint32_t session, uint64_t first_frame)
{
    return !boundary->controlled ||
        (boundary->sound_enabled && (session > boundary->session ||
         (session != 0U && session == boundary->session && first_frame >= boundary->first_frame)));
}

/**
 * @brief 应用新模式采样边界并丢弃旧分类、TOA前级和扫频历史。
 * @param boundary 非NULL只读边界，调用时复制，不保留指针。
 * @return 无返回值。
 * @note 仅前台且无PCM借用时调用，公共统计和原始PCM扇出继续。
 */
void AudioCapture_SetSoundBoundary(const AudioCaptureSoundBoundary *boundary);
#endif
/**
 * @brief   尝试消费一个DMA半块并按至少一秒间隔输出音频状态。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   processing_allowed 本次是否允许前台处理；DHT11启动拉低期间应为false。
 * @return  无返回值。
 * @note    App_Process重复调度，非精确周期；Begin成功后必调用End，End通过才提交统计和候选，失败清检测历史；之后不再解引用DMA指针。
 */
void AudioCapture_Process(uint32_t now_ms, bool processing_allowed);
/**
 * @brief   复制最大一轮AudioCapture内部耗时分解，仅测量，不改变业务。
 * @param   out 非NULL调用方输出；NULL不写。
 * @return  无返回值。
 * @note    HAL毫秒墙钟最大值；各字段为同一次最大轮内的分段，other为总时长减去已计分段；
 *          1ms分辨率且含IRQ抢占，不是纯CPU或最坏执行时间。
 */
typedef struct {
    uint32_t total_ms;
    uint32_t io_ms;
    uint32_t stats_ms;
    uint32_t sound_stage_ms;
    uint32_t sound_process_ms;
    uint32_t chirp_stage_ms;
    uint32_t chirp_complete_ms;
    uint32_t chirp_process_ms;
    uint32_t log_ms;
    uint32_t other_ms;
} AudioCaptureProfile;
/**
 * @brief   复制AudioCapture最大一轮内部耗时快照。
 * @param   out 非NULL调用方输出；NULL不写。
 * @return  无返回值。
 * @note    仅诊断；不授予PCM或DMA访问权，不清零累计最大值。
 */
void AudioCapture_GetProfile(AudioCaptureProfile *out);
#endif
