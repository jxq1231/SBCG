/**
 * @file        codec_monitor.h
 * @brief       轮询编解码器标识并输出诊断。
 * @details     声明本模块接口、数据类型或配置常量。前台至少两秒间隔通过BSP读取芯片ID，累计成功和失败次数并输出日志。
 * @note        DHT11启动拉低期间应禁止I2C诊断；不在此配置音频采集通路。
 */
#ifndef SBCG_CODEC_MONITOR_H
#define SBCG_CODEC_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   前台日志输出回调类型。
 * @param   line 只读日志字节，仅回调期间有效；返回前须消费或复制，不可保留指针。
 * @param   length 有效字节数，不含NUL；当前发送路径仅传正数。
 * @return  无返回值；传输错误由应用或BSP统计。
 * @note    当前应用接入SendLine，同步发送或复制入有界队列，不从IRQ调用。
 */
typedef void (*CodecMonitor_Log)(const char *line, int length);
/**
 * @brief   复位编解码器ID轮询状态与日志接口。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   log 日志回调，可为NULL以关闭对应前台处理；非空回调须返回前消费或复制字节，不保留临时缓冲指针。
 * @return  无返回值。
 * @note    应用前台初始化，仅保存回调与计数，不访问I2C。
 */
void CodecMonitor_Init(uint32_t now_ms, CodecMonitor_Log log);
/* io_allowed must be false throughout the DHT11 start-low interval. */
/**
 * @brief   放行且距上次至少两秒时读取芯片ID并输出诊断。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   io_allowed 是否允许I2C诊断；DHT11启动拉低期间必须为false。
 * @return  无返回值。
 * @note    由App_Process前台调度，错过周期不补发；ID读取成功不代表音频采集验收。
 */
void CodecMonitor_Process(uint32_t now_ms, bool io_allowed);

#endif
