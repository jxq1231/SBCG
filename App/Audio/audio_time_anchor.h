/**
 * @file        audio_time_anchor.h
 * @brief       采集音频传输进度与本地PTP时钟的夹读锚点。
 * @details     声明本模块接口、数据类型或配置常量。至少一秒间隔先读游标、读一次PTP、再读游标，检查会话/边界/顺序/宽度后交接单槽观测。
 * @note        依赖音频与本地PTP初始化；SYNC=0，不代表跨板同步或PCM声学时刻。
 */
#ifndef SBCG_AUDIO_TIME_ANCHOR_H
#define SBCG_AUDIO_TIME_ANCHOR_H
#include <stdbool.h>
#include <stdint.h>
#include "audio_time_map.h"

#define AUDIO_ANCHOR_MAX_WIDTH_FRAMES 4U
/**
 * @brief   前台日志输出回调类型。
 * @param   line 只读日志字节，仅回调期间有效；返回前须消费或复制，不可保留指针。
 * @param   length 有效字节数，不含NUL；当前发送路径仅传正数。
 * @return  无返回值；传输错误由应用或BSP统计。
 * @note    当前应用接入SendLine，同步发送或复制入有界队列，不从IRQ调用。
 */
typedef void (*AudioTimeAnchorLog)(const char *line, int length);
/**
 * @brief   一次前台锚点尝试的交接副本，不持有DMA缓冲指针。
 * @note    sequence为尝试序号，captured_ms为调用时毫秒；anchor.valid=false表示本次尝试被拒绝。
 */
typedef struct
{
    uint32_t sequence;
    uint32_t captured_ms;
    AudioTimeMapAnchor anchor;
} AudioTimeAnchorObservation;
/* Foreground, after audio and local PTP initialization. No peripheral setup. */
/**
 * @brief   复位锚点历史、故障锁存和单槽状态。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   log 日志回调，可为NULL以关闭对应前台处理；非空回调须返回前消费或复制字节，不保留临时缓冲指针。
 * @return  无返回值。
 * @note    应用前台初始化调用，音频与PTP应已准备好；不初始化外设。
 */
void AudioTimeAnchor_Init(uint32_t now_ms, AudioTimeAnchorLog log);
/**
 * @brief   按至少一秒间隔采锚、检查单调性并发布观测。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   processing_allowed 本次是否允许前台处理；DHT11启动拉低期间应为false。
 * @return  无返回值。
 * @note    App_Process前台调用；同会话时钟不递增会锁存拒绝，需新会话成功或重新初始化恢复；不标记跨板同步。
 */
void AudioTimeAnchor_Process(uint32_t now_ms, bool processing_allowed);
/* Foreground-only single-slot handoff. Each attempt replaces the slot; gaps
   remain visible in sequence. Failed attempts contain a cleared invalid anchor.
   NULL does not consume; empty reads clear a non-NULL output and return false. */
/**
 * @brief   消费尚未读取的最新一次锚点尝试。
 * @param   observation 调用方可写输出；NULL不消费，非NULL在无待取数据时清零。
 * @return  取到观测返回true，无数据或NULL返回false。
 * @note    仅前台单消费者；新尝试覆盖单槽，失败尝试的anchor.valid=false，序号暴露遗漏。
 */
bool AudioTimeAnchor_TakeObservation(AudioTimeAnchorObservation *observation);
#endif
