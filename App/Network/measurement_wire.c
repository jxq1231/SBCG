/**
 * @file        measurement_wire.c
 * @brief       A→B权威测量结果编解码。
 * @details     固定28字节大端；只接受已有效、ID非0、距离/声速/域在范围内的结果。
 * @note        纯字节编解码，无网络/HAL/内存分配。
 */
#include "measurement_wire.h"
#include "Measure/sound_pair.h"
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
static void PutI32(uint8_t *p,int32_t value){PutU32(p,(uint32_t)value);}
static int32_t GetI32(const uint8_t *p){return (int32_t)GetU32(p);}

MeasurementWireStatus MeasurementWire_Encode(const MeasurementResultSnapshot *result,
                                               uint8_t *data, uint16_t length)
{
    if(result==NULL || data==NULL || length<MEASUREMENT_WIRE_BYTES)
        return MEASUREMENT_WIRE_INVALID_LENGTH;
    if(result->status!=MEASUREMENT_VALID || !result->valid || result->id==0U ||
       result->distance_mm<0 || result->distance_mm>10000 ||
       result->quality>100U || result->sound_speed_mm_s==0U ||
       result->time_domain==0U)
        return MEASUREMENT_WIRE_INVALID_FIELD;
    data[0]='M';data[1]='R';data[2]='E';data[3]='S';
    data[4]=MEASUREMENT_WIRE_VERSION;data[5]=1U;
    data[6]=result->direction==SOUND_DIRECTION_A?1U:
            result->direction==SOUND_DIRECTION_B?2U:0U;
    data[7]=result->quality;
    PutU32(data+8,result->id);
    PutI32(data+12,result->distance_mm);
    PutU32(data+16,result->sound_speed_mm_s);
    PutU32(data+20,result->temperature_version);
    PutU32(data+24,result->time_domain);
    return MEASUREMENT_WIRE_OK;
}

MeasurementWireStatus MeasurementWire_Decode(const uint8_t *data, uint16_t length,
                                               MeasurementResultSnapshot *result)
{
    if(data==NULL || result==NULL)return MEASUREMENT_WIRE_INVALID_ARGUMENT;
    memset(result,0,sizeof *result);
    if(length!=MEASUREMENT_WIRE_BYTES)return MEASUREMENT_WIRE_INVALID_LENGTH;
    if(data[0]!='M' || data[1]!='R' || data[2]!='E' || data[3]!='S')
        return MEASUREMENT_WIRE_INVALID_MAGIC;
    if(data[4]!=MEASUREMENT_WIRE_VERSION)return MEASUREMENT_WIRE_INVALID_VERSION;
    const uint32_t id=GetU32(data+8);
    const int32_t distance=GetI32(data+12);
    const uint32_t speed=GetU32(data+16);
    const uint32_t version=GetU32(data+20);
    const uint32_t domain=GetU32(data+24);
    if(data[5]!=1U || id==0U || distance<0 || distance>10000 ||
       data[7]>100U || speed==0U || domain==0U || data[6]>2U)
        return MEASUREMENT_WIRE_INVALID_FIELD;
    result->status=MEASUREMENT_VALID;
    result->valid=true;
    result->id=id;
    result->distance_mm=distance;
    result->direction=data[6]==1U?SOUND_DIRECTION_A:
                        data[6]==2U?SOUND_DIRECTION_B:SOUND_DIRECTION_UNKNOWN;
    result->quality=data[7];
    result->sound_speed_mm_s=speed;
    result->temperature_version=version;
    result->temperature_valid=true;
    result->time_domain=domain;
    return MEASUREMENT_WIRE_OK;
}
