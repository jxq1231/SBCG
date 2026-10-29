/**
 * @file        udp_heartbeat.h
 * @brief       通过LwIP RAW UDP周期发送板端心跳。
 * @details     对外定义本模块的接口与回调约定。将序号、毫秒计数与三个UID字序列化为28字节大端报文，统计协议栈提交、链路跳过和遗漏时隙。
 * @note        MX_LWIP_Init后在裸机前台调用；发送成功只表示协议栈接受，不证明对端收到。
 */

#ifndef SBCG_UDP_HEARTBEAT_H
#define SBCG_UDP_HEARTBEAT_H

#include <stdint.h>

/**
 * @brief   向应用提交一行观测日志的回调类型。
 * @param   line 只读日志字符缓冲，不可为NULL；仅本次调用有效，需同步消费或复制。
 * @param   length 有效字符数，单位字节，不包含结尾零字符。
 * @return  无返回值。
 * @note    模块保存函数指针供前台使用；不得保留调用方局部缓冲指针。
 */
typedef void (*UDPHeartbeat_Log)(const char *line, int length);
/* Main context only, after MX_LWIP_Init. uid_words is copied. */
/**
 * @brief   删除旧UDP控制块并复制UID、复位心跳时隙与计数。
 * @param   now_ms 初始化毫秒计数，作为后续时隙起点。
 * @param   uid_words 不可为NULL，指向至少3个UID字；内容复制到模块，不保存指针。
 * @param   log 日志回调，可为NULL；后续处理期间须有效。
 * @return  无返回值。
 * @note    MX_LWIP_Init之后前台调用；不会立即发送。
 */
void UDPHeartbeat_Init(uint32_t now_ms, const uint32_t uid_words[3], UDPHeartbeat_Log log);
/**
 * @brief   经过心跳时隙时至多发送一包并输出统计。
 * @param   now_ms 前台当前毫秒计数，按无符号差值处理回绕。
 * @return  无返回值。
 * @note    错过多时隙时序号递增相应数量且不补发；链路断开仅增加跳过计数。
 */
void UDPHeartbeat_Process(uint32_t now_ms);

#endif
