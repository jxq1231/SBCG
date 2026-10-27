/**
 * @file        ranging_status_wire.c
 * @brief       RST2/RAK2固定大端编解码实现。
 * @details     逐字节写入，不依赖结构体padding；解码先校验组合关系再写输出。
 * @note        纯函数，无网络/HAL/动态内存；不保存revision或接收历史。
 */
#include "ranging_status_wire.h"
#include <stddef.h>
#include <string.h>

static void PutU16(uint8_t *p,uint16_t value)
{ p[0]=(uint8_t)(value>>8);p[1]=(uint8_t)value; }
static uint16_t GetU16(const uint8_t *p)
{ return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }
static void PutU32(uint8_t *p,uint32_t value)
{ p[0]=(uint8_t)(value>>24);p[1]=(uint8_t)(value>>16);p[2]=(uint8_t)(value>>8);p[3]=(uint8_t)value; }
static uint32_t GetU32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void PutU64(uint8_t *p,uint64_t value)
{
    PutU32(p,(uint32_t)(value>>32));PutU32(p+4,(uint32_t)value);
}
static uint64_t GetU64(const uint8_t *p)
{ return ((uint64_t)GetU32(p)<<32)|GetU32(p+4); }
static void PutI32(uint8_t *p,int32_t value){PutU32(p,(uint32_t)value);}
static int32_t GetI32(const uint8_t *p){return (int32_t)GetU32(p);}
static void PutI64(uint8_t *p,int64_t value){PutU64(p,(uint64_t)value);}
static int64_t GetI64(const uint8_t *p){return (int64_t)GetU64(p);}

/** @brief 校验标志组合、状态范围、截止时间顺序和空值语义。 */
static bool MessageValid(const RangingStatusMessage *m)
{
    if(m==NULL || m->state>RANGING_STATE_FAULT || m->direction>2U ||
       m->quality>100U || (m->flags & (uint16_t)~RANGING_STATUS_FLAG_KNOWN)!=0U ||
       m->evidence_kind>RANGING_EVIDENCE_CODED || m->domain==0U ||
       m->sound_speed_mm_s==0U)
    { return false; }
    const bool has=(m->flags&RANGING_STATUS_FLAG_HAS_VALUE)!=0U;
    const bool fresh=(m->flags&RANGING_STATUS_FLAG_FRESH)!=0U;
    const bool identity=(m->flags&RANGING_STATUS_FLAG_IDENTITY_VERIFIED)!=0U;
    const bool formal=(m->flags&RANGING_STATUS_FLAG_FORMAL)!=0U;
    if((fresh && !has) || (identity && !has) ||
       (formal && (!fresh || !identity)) ||
       (identity && m->evidence_kind==RANGING_EVIDENCE_PROXY))
    { return false; }
    if((m->state==RANGING_STATE_HOLD || m->state==RANGING_STATE_REACQUIRE) &&
       (!has || fresh || formal))
    { return false; }
    if((m->state==RANGING_STATE_ACQUIRING || m->state==RANGING_STATE_STALE ||
        m->state==RANGING_STATE_FAULT) && has)
    { return false; }
    if(!has)
    {
        if(m->distance_mm!=0 || m->filtered_delta_ns!=0 || m->measurement_id!=0U)
        { return false; }
        return true;
    }
    if(m->measurement_id==0U || m->source_common_ns==0U ||
       m->display_deadline_ns<=m->source_common_ns ||
       m->distance_mm<0 || m->distance_mm>10000)
    { return false; }
    if(fresh && (m->fresh_deadline_ns<=m->source_common_ns ||
                 m->fresh_deadline_ns>m->display_deadline_ns))
    { return false; }
    if(formal && m->reason==0xFFFFU)
    { return false; }
    return true;
}

RangingStatusWireStatus RangingStatusWire_Encode(const RangingStatusMessage *message,
                                                  uint8_t *data, uint16_t length)
{
    if(message==NULL || data==NULL)return RANGING_STATUS_WIRE_INVALID_ARGUMENT;
    if(length!=RANGING_STATUS_WIRE_BYTES)return RANGING_STATUS_WIRE_INVALID_LENGTH;
    if(!MessageValid(message))return RANGING_STATUS_WIRE_INVALID_FIELD;
    data[0]='R';data[1]='S';data[2]='T';data[3]='2';
    data[4]=RANGING_STATUS_WIRE_VERSION;
    data[5]=message->state;data[6]=message->direction;data[7]=message->quality;
    PutU16(data+8,message->flags);PutU16(data+10,RANGING_STATUS_WIRE_BYTES);
    PutU32(data+12,message->domain);PutU32(data+16,message->authority_clock_epoch);
    PutU32(data+20,message->state_revision);PutU32(data+24,message->measurement_id);
    PutU32(data+28,message->temperature_version);PutU32(data+32,message->sound_speed_mm_s);
    PutI32(data+36,message->distance_mm);
    PutU16(data+40,message->calibration_version);PutU16(data+42,(uint16_t)message->calibration_offset_mm);
    PutU16(data+44,message->reason);PutU16(data+46,0U);
    PutU64(data+48,message->source_common_ns);PutU64(data+56,message->fresh_deadline_ns);
    PutU64(data+64,message->display_deadline_ns);PutI64(data+72,message->filtered_delta_ns);
    PutU32(data+80,message->source_key_crc32);PutU16(data+84,message->detection_version);
    data[86]=message->evidence_kind;data[87]=0U;
    return RANGING_STATUS_WIRE_OK;
}

RangingStatusWireStatus RangingStatusWire_Decode(const uint8_t *data, uint16_t length,
                                                  RangingStatusMessage *out)
{
    if(data==NULL || out==NULL)return RANGING_STATUS_WIRE_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    if(length!=RANGING_STATUS_WIRE_BYTES)return RANGING_STATUS_WIRE_INVALID_LENGTH;
    if(data[0]!='R'||data[1]!='S'||data[2]!='T'||data[3]!='2')
        return RANGING_STATUS_WIRE_INVALID_MAGIC;
    if(data[4]!=RANGING_STATUS_WIRE_VERSION)return RANGING_STATUS_WIRE_INVALID_VERSION;
    if(GetU16(data+10)!=RANGING_STATUS_WIRE_BYTES || GetU16(data+46)!=0U || data[87]!=0U)
        return RANGING_STATUS_WIRE_INVALID_FIELD;
    RangingStatusMessage message={0};
    message.state=data[5];message.direction=data[6];message.quality=data[7];
    message.flags=GetU16(data+8);message.domain=GetU32(data+12);
    message.authority_clock_epoch=GetU32(data+16);message.state_revision=GetU32(data+20);
    message.measurement_id=GetU32(data+24);message.temperature_version=GetU32(data+28);
    message.sound_speed_mm_s=GetU32(data+32);message.distance_mm=GetI32(data+36);
    message.calibration_version=GetU16(data+40);
    message.calibration_offset_mm=(int16_t)GetU16(data+42);
    message.reason=GetU16(data+44);
    message.source_common_ns=GetU64(data+48);message.fresh_deadline_ns=GetU64(data+56);
    message.display_deadline_ns=GetU64(data+64);message.filtered_delta_ns=GetI64(data+72);
    message.source_key_crc32=GetU32(data+80);message.detection_version=GetU16(data+84);
    message.evidence_kind=data[86];
    if(!MessageValid(&message))return RANGING_STATUS_WIRE_INVALID_FIELD;
    *out=message;
    return RANGING_STATUS_WIRE_OK;
}

static bool AckValid(const RangingStatusAck *ack)
{
    return ack!=NULL && ack->status<=RANGING_STATUS_ACK_FAULT &&
           ack->domain!=0U && ack->state_revision!=0U &&
           !(ack->status==RANGING_STATUS_ACK_OK && ack->measurement_id==0U);
}

RangingStatusWireStatus RangingStatusAck_Encode(const RangingStatusAck *ack,
                                                 uint8_t *data, uint16_t length)
{
    if(ack==NULL || data==NULL)return RANGING_STATUS_WIRE_INVALID_ARGUMENT;
    if(length!=RANGING_STATUS_ACK_BYTES)return RANGING_STATUS_WIRE_INVALID_LENGTH;
    if(!AckValid(ack))return RANGING_STATUS_WIRE_INVALID_FIELD;
    data[0]='R';data[1]='A';data[2]='K';data[3]='2';
    data[4]=RANGING_STATUS_WIRE_VERSION;data[5]=ack->status;
    PutU16(data+6,0U);PutU32(data+8,ack->domain);
    PutU32(data+12,ack->authority_clock_epoch);
    PutU32(data+16,ack->state_revision);PutU32(data+20,ack->measurement_id);
    return RANGING_STATUS_WIRE_OK;
}

RangingStatusWireStatus RangingStatusAck_Decode(const uint8_t *data, uint16_t length,
                                                 RangingStatusAck *out)
{
    if(data==NULL || out==NULL)return RANGING_STATUS_WIRE_INVALID_ARGUMENT;
    memset(out,0,sizeof *out);
    if(length!=RANGING_STATUS_ACK_BYTES)return RANGING_STATUS_WIRE_INVALID_LENGTH;
    if(data[0]!='R'||data[1]!='A'||data[2]!='K'||data[3]!='2')
        return RANGING_STATUS_WIRE_INVALID_MAGIC;
    if(data[4]!=RANGING_STATUS_WIRE_VERSION)return RANGING_STATUS_WIRE_INVALID_VERSION;
    if(GetU16(data+6)!=0U)return RANGING_STATUS_WIRE_INVALID_FIELD;
    RangingStatusAck ack={.status=data[5],.domain=GetU32(data+8),
        .authority_clock_epoch=GetU32(data+12),.state_revision=GetU32(data+16),
        .measurement_id=GetU32(data+20)};
    if(!AckValid(&ack))return RANGING_STATUS_WIRE_INVALID_FIELD;
    *out=ack;
    return RANGING_STATUS_WIRE_OK;
}
