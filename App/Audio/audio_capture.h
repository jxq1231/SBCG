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
/**
 * @brief   尝试消费一个DMA半块并按至少一秒间隔输出音频状态。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   processing_allowed 本次是否允许前台处理；DHT11启动拉低期间应为false。
 * @return  无返回值。
 * @note    App_Process重复调度，非精确周期；Begin成功后必调用End，End通过才提交统计和候选，失败清检测历史；之后不再解引用DMA指针。
 */
void AudioCapture_Process(uint32_t now_ms, bool processing_allowed);
#endif
