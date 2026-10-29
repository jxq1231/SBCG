/**
 * @file ptp_wire.c
 * @brief 显式读写固定A/B两步PTPv2的标准头和时间戳字段。
 * @details 校验版本、域、长度、标志、保留字段、端口和32位秒范围；拒绝非零correctionField。
 * @note 不解释Announce、BMCA、扩展TLV或透明时钟修正，不声明完整PTP互操作性。
 */
#include "ptp_wire.h"
#include <string.h>
/** @brief 读取2字节大端整数。 @param p 非NULL只读2字节。 @return 主机整数。 */
static uint16_t Get16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]<<8U|p[1]); }
/** @brief 读取4字节大端整数。 @param p 非NULL只读4字节。 @return 主机整数。 */
static uint32_t Get32(const uint8_t *p) { return (uint32_t)p[0]<<24U|(uint32_t)p[1]<<16U|(uint32_t)p[2]<<8U|p[3]; }
/** @brief 写2字节大端整数。 @param p 非NULL可写2字节。 @param v 输入整数。 @return 无返回值。 */
static void Put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)(v>>8U);p[1]=(uint8_t)v; }
/** @brief 写4字节大端整数。 @param p 非NULL可写4字节。 @param v 输入整数。 @return 无返回值。 */
static void Put32(uint8_t *p,uint32_t v) { p[0]=(uint8_t)(v>>24U);p[1]=(uint8_t)(v>>16U);p[2]=(uint8_t)(v>>8U);p[3]=(uint8_t)v; }
/** @brief 确定支持消息的长度。 @param type 消息类型。 @return 44、54或不支持时0。 */
static size_t Length(PtpWireType type)
{ return type==PTP_WIRE_DELAY_RESP?54U:(type==PTP_WIRE_SYNC||type==PTP_WIRE_DELAY_REQ||type==PTP_WIRE_FOLLOW_UP?44U:0U); }
/** @brief 确定旧版control字段。 @param type 支持的消息类型。 @return 标准control值。 */
static uint8_t Control(PtpWireType type)
{ return type==PTP_WIRE_SYNC?0U:type==PTP_WIRE_DELAY_REQ?1U:type==PTP_WIRE_FOLLOW_UP?2U:3U; }
/** @brief 确定消息周期字段。 @param type 支持的消息类型。 @return Sync/FU为-2，其余127。 */
static uint8_t Interval(PtpWireType type)
{ return type==PTP_WIRE_SYNC||type==PTP_WIRE_FOLLOW_UP?254U:127U; }
/** @brief 检查保留字节是否全零。 @param p 非NULL输入。 @param n 字节数。 @return 全零为1。 */
static int Zero(const uint8_t *p,size_t n)
{ for(size_t i=0;i<n;++i) { if(p[i]!=0U) { return 0; } } return 1; }
void PtpWire_Identity(const uint8_t mac[6],uint8_t identity[10])
{
    memcpy(identity,mac,3);identity[3]=255;identity[4]=254;
    memcpy(identity+5,mac+3,3);identity[8]=0;identity[9]=1;
}
size_t PtpWire_Encode(const PtpWireMessage *m,uint8_t data[54])
{
    if(m==NULL||data==NULL) { return 0; }
    const size_t length=Length(m->type);
    if(length==0U||m->timestamp_ns>UINT64_C(4294967295999999999)||
       ((m->type==PTP_WIRE_SYNC||m->type==PTP_WIRE_DELAY_REQ)&&m->timestamp_ns!=0U)) { return 0; }
    memset(data,0,54);data[0]=(uint8_t)m->type;data[1]=2;Put16(data+2,(uint16_t)length);
    data[6]=(uint8_t)(m->type==PTP_WIRE_SYNC?6U:4U);
    memcpy(data+20,m->source,10);Put16(data+30,m->sequence);
    data[32]=Control(m->type);data[33]=Interval(m->type);
    Put32(data+36,(uint32_t)(m->timestamp_ns/UINT64_C(1000000000)));
    Put32(data+40,(uint32_t)(m->timestamp_ns%UINT64_C(1000000000)));
    if(length==54U) { memcpy(data+44,m->requester,10); }
    return length;
}
PtpWireStatus PtpWire_Decode(const uint8_t *data,size_t length,PtpWireMessage *m)
{
    if(data==NULL||m==NULL||length<44U) { return PTP_WIRE_INVALID; }
    const PtpWireType type=(PtpWireType)data[0];const size_t required=Length(type);
    if(required==0U||length!=required||Get16(data+2)!=length||data[1]!=2U||data[4]!=0U||data[5]!=0U||
       data[6]!=(type==PTP_WIRE_SYNC?6U:4U)||data[7]!=0U||!Zero(data+16,4)||
       Get16(data+28)!=1U||data[32]!=Control(type)||data[33]!=Interval(type)||!Zero(data+34,2)||Get32(data+40)>=1000000000U||
       ((type==PTP_WIRE_SYNC||type==PTP_WIRE_DELAY_REQ)&&!Zero(data+34,10))||
       (type==PTP_WIRE_DELAY_RESP&&Get16(data+52)!=1U)) { return PTP_WIRE_INVALID; }
    if(!Zero(data+8,8)) { return PTP_WIRE_CORRECTION; }
    PtpWireMessage decoded={0};decoded.type=type;decoded.sequence=Get16(data+30);
    memcpy(decoded.source,data+20,10);if(required==54U) { memcpy(decoded.requester,data+44,10); }
    decoded.timestamp_ns=(uint64_t)Get32(data+36)*UINT64_C(1000000000)+Get32(data+40);
    *m=decoded;return PTP_WIRE_OK;
}
