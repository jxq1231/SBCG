/**
 * @file ptp_wire.h
 * @brief 固定 A/B 单播 PTPv2 两步交换的纯字节编解码接口。
 * @details 仅支持域0、端口1和零修正字段；报文是标准头及时间戳布局，不声明完整PTP协议栈。
 * @note 不持有输入指针，不访问硬件；所有多字节线上整数为网络大端序。
 */
#ifndef SBCG_PTP_WIRE_H
#define SBCG_PTP_WIRE_H
#include <stddef.h>
#include <stdint.h>
/** @brief 当前增量支持的四种PTPv2消息类型。 */
typedef enum { PTP_WIRE_SYNC=0, PTP_WIRE_DELAY_REQ=1,
               PTP_WIRE_FOLLOW_UP=8, PTP_WIRE_DELAY_RESP=9 } PtpWireType;
/** @brief 解码结果；非零修正单独上报，不能当成未修正时间使用。 */
typedef enum { PTP_WIRE_OK, PTP_WIRE_INVALID, PTP_WIRE_CORRECTION } PtpWireStatus;
/** @brief CPU消息副本；source/requester为8字节时钟身份加2字节端口身份，非线上结构体。 */
typedef struct
{
    PtpWireType type;
    uint16_t sequence;
    uint8_t source[10], requester[10];
    uint64_t timestamp_ns;
} PtpWireMessage;
/**
 * @brief 由登记MAC插入FF:FE构建EUI64及端口1。
 * @param mac 调用期间有效的6字节只读MAC，不可为NULL。
 * @param identity 调用者提供的10字节输出，不可为NULL。
 * @return 无返回值。
 * @note 不使用IPv6的U/L位翻转，不保留输入存储。
 */
void PtpWire_Identity(const uint8_t mac[6],uint8_t identity[10]);
/**
 * @brief 将CPU消息编码为固定域0的单播PTPv2报文。
 * @param message 调用期间有效的只读消息，不可为NULL；事件消息时间戳须为0。
 * @param data 至少54字节可写输出，不可为NULL。
 * @return 成功返回44或54字节，参数/时间范围错误返回0。
 * @note 不保存指针；Sync设置twoStep，全部消息修正字段为0。
 */
size_t PtpWire_Encode(const PtpWireMessage *message,uint8_t data[54]);
/**
 * @brief 严格解码当前固定A/B配置支持的PTPv2消息。
 * @param data length字节只读输入，可为NULL并拒绝。
 * @param length 实际UDP负载长度，单位字节，必须等于消息长度。
 * @param message 调用者拥有的输出，可为NULL并拒绝；失败保留原值。
 * @return OK为成功，CORRECTION表示不支持非零修正，其余为非法报文。
 * @note 秒高16位必须为0，纳秒小于十亿；不验证对端身份，交由传输层核对。
 */
PtpWireStatus PtpWire_Decode(const uint8_t *data,size_t length,PtpWireMessage *message);
#endif
