/**
 * @file        audio_time_map_monitor.h
 * @brief       用三个顺序锚点检查本地音频时间映射。
 * @details     声明本模块接口、数据类型或配置常量。消费单槽观测，检查连续性与新鲜度；三个不重叠观测以两端估计中间进度，输出模型时间和残差。
 * @note        仅前台门控放行时执行，不是事件检测或PCM时间戳查询服务；SYNC=0。
 */
#ifndef SBCG_AUDIO_TIME_MAP_MONITOR_H
#define SBCG_AUDIO_TIME_MAP_MONITOR_H
#include <stdbool.h>
#include <stdint.h>
/**
 * @brief   前台日志输出回调类型。
 * @param   line 只读日志字节，仅回调期间有效；返回前须消费或复制，不可保留指针。
 * @param   length 有效字节数，不含NUL；当前发送路径仅传正数。
 * @return  无返回值；传输错误由应用或BSP统计。
 * @note    当前应用接入SendLine，同步发送或复制入有界队列，不从IRQ调用。
 */
typedef void (*AudioTimeMapLog)(const char *line, int length);
/* Foreground only. A diagnostic three-anchor holdout observer, not an event
   detector or PCM timestamp query service. No hardware configuration. */
/**
 * @brief   复位诊断窗口、连续性记录及输出序号。
 * @param   log 日志回调，可为NULL以关闭对应前台处理；非空回调须返回前消费或复制字节，不保留临时缓冲指针。
 * @return  无返回值。
 * @note    应用前台初始化，不消费已有锚点单槽、不改变外设。
 */
void AudioTimeMapMonitor_Init(AudioTimeMapLog log);
/**
 * @brief   消费一个锚点观测并在三点齐备时留中检验。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   processing_allowed 本次是否允许前台处理；DHT11启动拉低期间应为false。
 * @return  无返回值。
 * @note    App_Process前台调度；超过250毫秒判陈旧，失败清窗；序号或会话变化时可用当前有效观测开启新窗。
 */
void AudioTimeMapMonitor_Process(uint32_t now_ms, bool processing_allowed);
#endif
