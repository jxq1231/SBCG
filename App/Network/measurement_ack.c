/**
 * @file        measurement_ack.c
 * @brief       MACK确认报文编解码。
 * @details     固定16字节大端；只接受OK/INVALID状态及非零ID/令牌。
 * @note        纯字节编解码，无网络/HAL/内存分配。
 */
#include "measurement_ack.h"
#include <stddef.h>
#include <string.h>

static void PutU32(uint8_t *p,uint32_t value)
{
    p[0]=(uint8_t)(value>>24);p[1]=(uint8_t)(value>>16);
    p[2]=(uint8_t)(value>>8);p[3]=(uint8_t)value;
}
static uint32_t GetU32(const uint8_t *p)
{
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

MeasurementAckWireStatus MeasurementAck_Encode(const MeasurementAck *ack,
                                                 uint8_t *data, uint16_t length)
{
    if(ack==NULL || data==NULL || length<MEASUREMENT_ACK_BYTES)
        return MEASUREMENT_ACK_WIRE_INVALID_LENGTH;
    if((ack->status!=MEASUREMENT_ACK_OK && ack->status!=MEASUREMENT_ACK_INVALID) ||
       ack->measurement_id==0U || ack->time_domain==0U)
        return MEASUREMENT_ACK_WIRE_INVALID_FIELD;
    data[0]='M';data[1]='A';data[2]='C';data[3]='K';
    data[4]=MEASUREMENT_ACK_VERSION;data[5]=(uint8_t)ack->status;
    data[6]=data[7]=0U;
    PutU32(data+8,ack->measurement_id);PutU32(data+12,ack->time_domain);
    return MEASUREMENT_ACK_WIRE_OK;
}

MeasurementAckWireStatus MeasurementAck_Decode(const uint8_t *data, uint16_t length,
                                                 MeasurementAck *ack)
{
    if(data==NULL || ack==NULL)return MEASUREMENT_ACK_WIRE_INVALID_ARGUMENT;
    memset(ack,0,sizeof *ack);
    if(length!=MEASUREMENT_ACK_BYTES)return MEASUREMENT_ACK_WIRE_INVALID_LENGTH;
    if(data[0]!='M' || data[1]!='A' || data[2]!='C' || data[3]!='K')
        return MEASUREMENT_ACK_WIRE_INVALID_MAGIC;
    if(data[4]!=MEASUREMENT_ACK_VERSION)return MEASUREMENT_ACK_WIRE_INVALID_VERSION;
    if(data[5]!=MEASUREMENT_ACK_OK && data[5]!=MEASUREMENT_ACK_INVALID)
        return MEASUREMENT_ACK_WIRE_INVALID_FIELD;
    if(data[6]!=0U || data[7]!=0U)return MEASUREMENT_ACK_WIRE_INVALID_FIELD;
    const uint32_t id=GetU32(data+8),domain=GetU32(data+12);
    if(id==0U || domain==0U)return MEASUREMENT_ACK_WIRE_INVALID_FIELD;
    ack->status=(MeasurementAckStatus)data[5];
    ack->measurement_id=id;
    ack->time_domain=domain;
    return MEASUREMENT_ACK_WIRE_OK;
}
