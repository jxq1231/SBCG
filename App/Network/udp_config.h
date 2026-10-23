/**
 * @file        udp_config.h
 * @brief       定义应用UDP协议地址、端口、周期和报文长度。
 * @details     对外定义本模块的协议配置宏。心跳目的地址为192.168.10.100，探测及时间交换分别使用独立板端和PC端端口；长度单位为字节。
 * @note        这些宏定义应用协议，不修改CubeMX生成的本机IP地址或ETH硬件配置。
 */

#ifndef SBCG_UDP_CONFIG_H
#define SBCG_UDP_CONFIG_H

/**
 * @brief 应用对端IPv4及UDP线协议常量。
 * @note 端口为主机侧常量；报文字段通过显式字节写入采用网络大端序，
 *       不以C结构体内存布局直接发送。28字节v1含8字节头、序号、毫秒计数、12字节UID；
 *       时间交换另加8字节会话形成36字节键，报告或提交再加16字节时间字段。
 */
/* Application destination, not the CubeMX-generated address of this board. */
#define UDP_HEARTBEAT_DEST_A 192U
#define UDP_HEARTBEAT_DEST_B 168U
#define UDP_HEARTBEAT_DEST_C 10U
#define UDP_HEARTBEAT_DEST_D 100U
#define UDP_HEARTBEAT_LOCAL_PORT 5005U
#define UDP_HEARTBEAT_DEST_PORT 5005U
#define UDP_HEARTBEAT_PERIOD_MS 1000U
#define UDP_HEARTBEAT_PACKET_BYTES 28U
#define UDP_PROBE_BOARD_PORT 5006U
#define UDP_PROBE_PC_PORT 6006U
#define UDP_TIME_EXCHANGE_BOARD_PORT 5007U
#define UDP_TIME_EXCHANGE_PC_PORT 6007U

#define UDP_TIME_EXCHANGE_REPORT_BYTES 52U
#define UDP_TIME_EXCHANGE_SUBMIT_BYTES 52U

#endif
