/**
 * @file        clap_wire.c
 * @brief       CLP1/CLA1逐字节大端编解码及网络入口字段校验。
 * @details     布局固定为80/24字节；失败保留调用方输出，不依赖结构体布局或本机端序。
 * @note        无状态纯函数；端点资格和RSC2代际由调用方与ClapRanging共同核对。
 */
#include "clap_wire.h"
#include <stddef.h>
#include <string.h>

/** @brief 写16位大端无符号值；p指向至少2字节可写空间。 */
static void Put16(uint8_t *p,uint16_t value)
{ p[0]=(uint8_t)(value>>8);p[1]=(uint8_t)value; }
/** @brief 读16位大端值；p指向至少2字节只读空间。 */
static uint16_t Get16(const uint8_t *p)
{ return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }
/** @brief 写32位大端无符号值；p指向至少4字节可写空间。 */
static void Put32(uint8_t *p,uint32_t value)
{
    p[0]=(uint8_t)(value>>24);p[1]=(uint8_t)(value>>16);
    p[2]=(uint8_t)(value>>8);p[3]=(uint8_t)value;
}
/** @brief 读32位大端无符号值；p指向至少4字节只读空间。 */
static uint32_t Get32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
/** @brief 写64位大端值；p指向至少8字节可写空间。 */
static void Put64(uint8_t *p,uint64_t value)
{ Put32(p,(uint32_t)(value>>32));Put32(p+4,(uint32_t)value); }
/** @brief 读64位大端值；p指向至少8字节只读空间。 */
static uint64_t Get64(const uint8_t *p)
{ return ((uint64_t)Get32(p)<<32)|Get32(p+4); }
/** @brief 校验待编码/已解码结果的字段范围及TDOA方向一致性；m不可NULL。 */
static bool ResultValid(const ClapResultMessage *m)
{
    return m->domain!=0U && m->authority_epoch!=0U && m->id!=0U &&
        m->a_session!=0U && m->a_sequence!=0U && m->b_session!=0U && m->b_sequence!=0U &&
        m->source_common_ns!=0U && m->quality<=100U &&
        m->distance_mm>=0 && m->distance_mm<=(int32_t)CLAP_RANGING_MAX_DISTANCE_MM &&
        m->delta_ns>=-INT64_C(100000000) && m->delta_ns<=INT64_C(100000000) &&
        m->sound_speed_mm_s>0U && m->sound_speed_mm_s<=1000000U &&
        m->temperature_version!=0U &&
        m->calibration_offset_mm>=-CALIBRATION_MAX_ABS_OFFSET_MM &&
        m->calibration_offset_mm<=CALIBRATION_MAX_ABS_OFFSET_MM &&
        m->direction==(m->delta_ns>0?SOUND_DIRECTION_A:
            (m->delta_ns<0?SOUND_DIRECTION_B:SOUND_DIRECTION_UNKNOWN));
}

/** @brief 编码完整CLP1；完成所有校验后才写输出。 */
bool ClapWire_EncodeResult(const ClapResultMessage *m,uint8_t *data,uint16_t length)
{
    if(m==NULL || data==NULL || length!=CLAP_RESULT_WIRE_BYTES || !ResultValid(m))return false;
    memcpy(data,"CLP1",4U);data[4]=1U;data[5]=0U;data[6]=m->direction;data[7]=m->quality;
    Put32(data+8,m->domain);Put32(data+12,m->authority_epoch);Put32(data+16,m->id);
    Put32(data+20,m->a_session);Put32(data+24,m->a_sequence);
    Put32(data+28,m->b_session);Put32(data+32,m->b_sequence);
    Put64(data+36,m->source_common_ns);Put64(data+44,(uint64_t)m->delta_ns);
    Put32(data+52,(uint32_t)m->distance_mm);Put32(data+56,m->sound_speed_mm_s);
    Put32(data+60,m->temperature_version);Put32(data+64,(uint32_t)m->temperature_milli_c);
    Put16(data+68,m->calibration_version);Put16(data+70,(uint16_t)m->calibration_offset_mm);
    memset(data+72,0,8U);
    return true;
}

/** @brief 解码并完整校验CLP1；有符号补码转换不依赖超范围强制类型转换。 */
bool ClapWire_DecodeResult(const uint8_t *data,uint16_t length,ClapResultMessage *out)
{
    if(data==NULL || out==NULL || length!=CLAP_RESULT_WIRE_BYTES ||
       memcmp(data,"CLP1",4U)!=0 || data[4]!=1U || data[5]!=0U || Get64(data+72)!=0U)
    { return false; }
    ClapResultMessage m={0};
    m.direction=data[6];m.quality=data[7];m.domain=Get32(data+8);
    m.authority_epoch=Get32(data+12);m.id=Get32(data+16);
    m.a_session=Get32(data+20);m.a_sequence=Get32(data+24);
    m.b_session=Get32(data+28);m.b_sequence=Get32(data+32);
    m.source_common_ns=Get64(data+36);
    const uint64_t delta_bits=Get64(data+44);
    m.delta_ns=delta_bits<=INT64_MAX?(int64_t)delta_bits:-1-(int64_t)(~delta_bits);
    const uint32_t distance_bits=Get32(data+52);
    m.distance_mm=distance_bits<=INT32_MAX?(int32_t)distance_bits:-1-(int32_t)(~distance_bits);
    m.sound_speed_mm_s=Get32(data+56);m.temperature_version=Get32(data+60);
    const uint32_t temperature_bits=Get32(data+64);
    m.temperature_milli_c=temperature_bits<=INT32_MAX?(int32_t)temperature_bits:-1-(int32_t)(~temperature_bits);
    m.calibration_version=Get16(data+68);
    const uint16_t offset_bits=Get16(data+70);
    m.calibration_offset_mm=offset_bits<=INT16_MAX?(int16_t)offset_bits:
        (int16_t)(-1-(int16_t)(uint16_t)~offset_bits);
    if(!ResultValid(&m))return false;
    *out=m;
    return true;
}

/** @brief 编码四字段精确ACK；无版本和保留字段，magic已区分CLA1。 */
bool ClapWire_EncodeAck(const ClapAckMessage *ack,uint8_t *data,uint16_t length)
{
    if(ack==NULL || data==NULL || length!=CLAP_ACK_WIRE_BYTES || ack->domain==0U ||
       ack->authority_epoch==0U || ack->id==0U || ack->source_common_ns==0U)return false;
    memcpy(data,"CLA1",4U);Put32(data+4,ack->domain);Put32(data+8,ack->authority_epoch);
    Put32(data+12,ack->id);Put64(data+16,ack->source_common_ns);
    return true;
}

/** @brief 解码并校验CLA1非零键；失败不修改out。 */
bool ClapWire_DecodeAck(const uint8_t *data,uint16_t length,ClapAckMessage *out)
{
    if(data==NULL || out==NULL || length!=CLAP_ACK_WIRE_BYTES || memcmp(data,"CLA1",4U)!=0)
    { return false; }
    const ClapAckMessage ack={Get32(data+4),Get32(data+8),Get32(data+12),Get64(data+16)};
    if(ack.domain==0U || ack.authority_epoch==0U || ack.id==0U || ack.source_common_ns==0U)return false;
    *out=ack;
    return true;
}
