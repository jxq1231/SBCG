/**
 * @file        temperature_wire.c
 * @brief       A板共享温度到B板的20字节UDP线协议实现。
 * @details     显式大端编解码，校验温度范围及固定整数声速公式。
 * @note        纯字节处理，不访问网络、HAL或PTP时间。
 */
#include "temperature_wire.h"
#include "Temperature/sound_speed.h"
#include "Temperature/temperature_config.h"
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

bool TemperatureWire_Decode(const uint8_t *data, uint16_t length,
                            TemperatureWireMessage *message)
{
    if(data==NULL || message==NULL)return false;
    memset(message,0,sizeof *message);
    if(length!=TEMPERATURE_WIRE_BYTES)return false;
    if(data[0]!='T' || data[1]!='E' || data[2]!='M' || data[3]!='P')return false;
    if(data[4]!=TEMPERATURE_WIRE_VERSION || data[5]!=1U ||
       data[6]>(uint8_t)TEMPERATURE_SOURCE_MANUAL || data[7]!=0U)return false;
    const int32_t temperature=(int32_t)GetU32(data+8);
    const uint32_t speed=GetU32(data+12);
    const uint32_t version=GetU32(data+16);
    uint32_t expected_speed;
    if(version==0U || temperature<TEMP_MIN_MILLI_C || temperature>TEMP_MAX_MILLI_C ||
       !SoundSpeed_Calculate(temperature,&expected_speed) || speed!=expected_speed)
    { return false; }
    message->valid=true;
    message->source=data[6]==0U?TEMPERATURE_SOURCE_AUTO:TEMPERATURE_SOURCE_MANUAL;
    message->temperature_milli_c=temperature;
    message->sound_speed_mm_s=speed;
    message->version=version;
    return true;
}

bool TemperatureWire_Encode(const TemperatureWireMessage *message,
                            uint8_t *data, uint16_t length)
{
    if(message==NULL || data==NULL || length<TEMPERATURE_WIRE_BYTES ||
       !message->valid || message->version==0U ||
       (message->source!=TEMPERATURE_SOURCE_AUTO &&
        message->source!=TEMPERATURE_SOURCE_MANUAL) ||
       message->temperature_milli_c<TEMP_MIN_MILLI_C ||
       message->temperature_milli_c>TEMP_MAX_MILLI_C)
    { return false; }
    uint32_t expected_speed;
    if(!SoundSpeed_Calculate(message->temperature_milli_c,&expected_speed) ||
       message->sound_speed_mm_s!=expected_speed)
    { return false; }
    data[0]='T';data[1]='E';data[2]='M';data[3]='P';
    data[4]=TEMPERATURE_WIRE_VERSION;data[5]=1U;
    data[6]=(uint8_t)message->source;data[7]=0U;
    PutU32(data+8,(uint32_t)message->temperature_milli_c);
    PutU32(data+12,message->sound_speed_mm_s);
    PutU32(data+16,message->version);
    return true;
}
