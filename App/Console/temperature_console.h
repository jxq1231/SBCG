/**
 * @file        temperature_console.h
 * @brief       解析串口温度命令并返回环境快照。
 * @details     声明本模块接口、数据类型或配置常量。每次至多读取64字节，按LF分帧且回复间隔至少100毫秒，支持PING、TEMP?、TEMP SET和TEMP AUTO。
 * @note        仅前台；依赖控制台接收BSP和温度服务，DHT11启动低电平期间暂停，非法/超长/接收间隙帧丢弃至LF。
 */
#ifndef SBCG_TEMPERATURE_CONSOLE_H
#define SBCG_TEMPERATURE_CONSOLE_H
#include <stdint.h>
/**
 * @brief   前台日志输出回调类型。
 * @param   line 只读日志字节，仅回调期间有效；返回前须消费或复制，不可保留指针。
 * @param   length 有效字节数，不含NUL；当前发送路径仅传正数。
 * @return  无返回值；传输错误由应用或BSP统计。
 * @note    当前应用接入SendLine，同步发送或复制入有界队列，不从IRQ调用。
 */
typedef void (*TemperatureConsole_Log)(const char *line, int length);
/* Main context: bounded work, no processing during the sensor start pulse. */
/**
 * @brief   复位组帧、丢弃和回复节流状态。
 * @param   log 日志回调，可为NULL以关闭对应前台处理；非空回调须返回前消费或复制字节，不保留临时缓冲指针。
 * @return  无返回值。
 * @note    应用前台初始化，不初始化串口接收硬件。
 */
void TemperatureConsole_Init(TemperatureConsole_Log log);
/**
 * @brief   放行时读取有限字节并执行至多一条非空命令。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @return  无返回值。
 * @note    App_Process前台调度，每次最多64字节，回复间隔至少100毫秒；接收间隙/非法字符/超长丢弃至LF，接受末尾CRLF。
 */
void TemperatureConsole_Process(uint32_t now_ms);
#endif
