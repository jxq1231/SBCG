/**
 * @file        audio_frame_index.c
 * @brief       维护观测到的DMA边界帧坐标与黏滞故障。
 * @details     半满/全满各累计8192帧，检查半区顺序、250毫秒间隔和64位溢出，不独立测量采样时钟。
 * @note        BSP前台初始化，DMA回调登记边界；调用方同步与IRQ共享的副本。
 */
#include "audio_frame_index.h"

/**
 * @brief   清零帧计数、故障和半区顺序，记录初始毫秒。
 * @param   index 调用方可写索引对象，不可为NULL。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @return  无返回值。
 * @note    BSP在启动采集时由前台调用，首个预期半区为0。
 */
void AudioFrameIndex_Init(AudioFrameIndex *index, uint32_t now_ms)
{
    *index = (AudioFrameIndex){.previous_ms = now_ms};
}

/**
 * @brief   登记DMA边界并累计帧数、锁存顺序或迟到等故障。
 * @param   index 同一会话可写索引，不可为NULL，由单一写入路径维护。
 * @param   half 已完成半区：半满为0，全满为1。
 * @param   now_ms 回调读取的HAL毫秒计数，与上次边界作无符号差。
 * @return  无返回值。
 * @note    DMA回调路径调用，不处理PCM；中断边界遗漏后计数不保证对应实际硬件进度。
 */
void AudioFrameIndex_Complete(AudioFrameIndex *index, uint8_t half, uint32_t now_ms)
{
    if (half != index->expected_half) { index->faults |= AUDIO_INDEX_ORDER; }
    if ((uint32_t)(now_ms - index->previous_ms) > AUDIO_INDEX_MAX_INTERVAL_MS)
    { index->faults |= AUDIO_INDEX_LATE; }
    if (index->frames > UINT64_MAX - AUDIO_INDEX_BLOCK_FRAMES)
    { index->faults |= AUDIO_INDEX_OVERFLOW; }
    else { index->frames += AUDIO_INDEX_BLOCK_FRAMES; }
    index->expected_half = half ^ 1U;
    index->previous_ms = now_ms;
}
