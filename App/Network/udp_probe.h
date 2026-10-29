/**
 * @file        udp_probe.h
 * @brief       接收并校验PC探测请求，返回对应UDP应答。
 * @details     对外定义本模块的接口与回调约定。限制对端IP和端口，检查28字节v1封装及UID，交由PtpRx关联接收时间，再发送type=3应答。
 * @note        RAW接收回调在NO_SYS主循环输入链中同步执行；每个接收pbuf由回调释放。
 */

#ifndef SBCG_UDP_PROBE_H
#define SBCG_UDP_PROBE_H
#include <stdint.h>

/**
 * @brief   向应用提交一行观测日志的回调类型。
 * @param   line 只读日志字符缓冲，不可为NULL；仅本次调用有效，需同步消费或复制。
 * @param   length 有效字符数，单位字节，不包含结尾零字符。
 * @return  无返回值。
 * @note    模块保存函数指针供前台使用；不得保留调用方局部缓冲指针。
 */
typedef void (*UDPProbe_Log)(const char *line, int length);
/* Main context only, after MX_LWIP_Init. The log callback must stay valid. */
/**
 * @brief   清理旧接收控制块，复制UID并复位探测统计。
 * @param   now_ms 初始化毫秒计数；允许首次Process立即尝试绑定。
 * @param   uid_words 至少3个只读UID字，不可为NULL；本函数复制内容。
 * @param   log 统计日志回调，可为NULL；后续使用期间须有效。
 * @return  无返回值。
 * @note    MX_LWIP_Init后由前台调用；实际udp_recv注册在Process进行。
 */
void UDPProbe_Init(uint32_t now_ms, const uint32_t uid_words[3], UDPProbe_Log log);
/**
 * @brief   链路可用时重试注册UDP接收，每秒最多输出一次统计。
 * @param   now_ms 前台当前毫秒计数，重试与日志按无符号差值节流。
 * @return  无返回值。
 * @note    主循环调度；UDP请求本身在后续LwIP输入链的Receive中处理。
 */
void UDPProbe_Process(uint32_t now_ms);
#endif
