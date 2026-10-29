/**
 * @file        domain_wire.c
 * @brief       共同域REQ/ACK编解码实现。
 * @details     固定24字节大端；验证magic/版本/类型/角色/状态/令牌和非零保留。
 * @note        纯字节编解码，无网络/HAL/内存分配。
 */
#include "domain_wire.h"
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

DomainWireStatus DomainWire_Encode(const DomainMessage *message,
                                    uint8_t *data, uint16_t length)
{
    if(message==NULL || data==NULL || length<DOMAIN_WIRE_BYTES)
        return DOMAIN_WIRE_INVALID_LENGTH;
    if((message->type!=DOMAIN_MESSAGE_REQUEST && message->type!=DOMAIN_MESSAGE_ACK) ||
       (message->role!=BOARD_ROLE_A && message->role!=BOARD_ROLE_B) ||
       (message->state!=DOMAIN_LOCAL_INVALID && message->state!=DOMAIN_LOCAL_VALID) ||
       message->token==0U || message->boot_nonce==0U || message->sequence==0U)
        return DOMAIN_WIRE_INVALID_FIELD;
    data[0]='D';data[1]='O';data[2]='M';data[3]='N';
    data[4]=DOMAIN_WIRE_VERSION;data[5]=(uint8_t)message->type;
    data[6]=(uint8_t)message->role;data[7]=(uint8_t)message->state;
    data[8]=data[9]=data[10]=data[11]=0U;
    PutU32(data+12,message->token);PutU32(data+16,message->boot_nonce);
    PutU32(data+20,message->sequence);
    return DOMAIN_WIRE_OK;
}

DomainWireStatus DomainWire_Decode(const uint8_t *data, uint16_t length,
                                    DomainMessage *message)
{
    if(data==NULL || message==NULL)return DOMAIN_WIRE_INVALID_ARGUMENT;
    memset(message,0,sizeof *message);
    if(length!=DOMAIN_WIRE_BYTES)return DOMAIN_WIRE_INVALID_LENGTH;
    if(data[0]!='D' || data[1]!='O' || data[2]!='M' || data[3]!='N')
        return DOMAIN_WIRE_INVALID_MAGIC;
    if(data[4]!=DOMAIN_WIRE_VERSION)return DOMAIN_WIRE_INVALID_VERSION;
    if(data[5]<DOMAIN_MESSAGE_REQUEST || data[5]>DOMAIN_MESSAGE_ACK ||
       data[6]<BOARD_ROLE_A || data[6]>BOARD_ROLE_B ||
       data[7]>DOMAIN_LOCAL_VALID || data[8]|data[9]|data[10]|data[11])
        return DOMAIN_WIRE_INVALID_FIELD;
    message->type=(DomainMessageType)data[5];
    message->role=(BoardRole)data[6];
    message->state=(DomainLocalState)data[7];
    message->token=GetU32(data+12);
    message->boot_nonce=GetU32(data+16);
    message->sequence=GetU32(data+20);
    if(message->token==0U || message->boot_nonce==0U || message->sequence==0U)
        return DOMAIN_WIRE_INVALID_FIELD;
    return DOMAIN_WIRE_OK;
}
