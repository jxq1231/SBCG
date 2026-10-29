/**
 * @file        audio_frame_index.h
 * @brief       维护观测到的DMA边界帧坐标与黏滞故障。
 * @details     声明本模块接口、数据类型或配置常量。半满/全满各累计8192帧，检查半区顺序、250毫秒间隔和64位溢出，不独立测量采样时钟。
 * @note        BSP前台初始化，DMA回调登记边界；调用方同步与IRQ共享的副本。
 */
#ifndef SBCG_AUDIO_FRAME_INDEX_H
#define SBCG_AUDIO_FRAME_INDEX_H
#include <stdint.h>

#define AUDIO_INDEX_BLOCK_FRAMES 8192U
#define AUDIO_INDEX_MAX_INTERVAL_MS 250U
#define AUDIO_INDEX_ORDER 1U
#define AUDIO_INDEX_LATE 2U
#define AUDIO_INDEX_OVERFLOW 4U
#define AUDIO_INDEX_HAL_ERROR 8U

/* Counts observed DMA boundaries, not an independently measured sample clock.
   Single writer; callers must synchronize copies shared with IRQ context. */
/**
 * @brief   单会话的观测帧数与边界健康状态。
 * @note    frames只按已收到的DMA回调累加；faults为黏滞位，重新初始化才清零。
 */
typedef struct
{
    uint64_t frames;
    uint32_t faults, previous_ms;
    uint8_t expected_half;
} AudioFrameIndex;
/**
 * @brief   清零帧计数、故障和半区顺序，记录初始毫秒。
 * @param   index 调用方可写索引对象，不可为NULL。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @return  无返回值。
 * @note    BSP在启动采集时由前台调用，首个预期半区为0。
 */
void AudioFrameIndex_Init(AudioFrameIndex *index, uint32_t now_ms);
/**
 * @brief   登记DMA边界并累计帧数、锁存顺序或迟到等故障。
 * @param   index 同一会话可写索引，不可为NULL，由单一写入路径维护。
 * @param   half 已完成半区：半满为0，全满为1。
 * @param   now_ms 回调读取的HAL毫秒计数，与上次边界作无符号差。
 * @return  无返回值。
 * @note    DMA回调路径调用，不处理PCM；中断边界遗漏后计数不保证对应实际硬件进度。
 */
void AudioFrameIndex_Complete(AudioFrameIndex *index, uint8_t half, uint32_t now_ms);
#endif
