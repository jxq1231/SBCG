/**
 * @file        mode_wire.c
 * @brief       按固定大端序布局编码和校验双板MDP1模式控制消息。
 * @details     校验操作与发送角色以及各标识字段的组合，成功后才写输出。
 * @note        无状态纯前台逻辑，发送与接收的对端资格由UDP服务检查。
 */
#include "mode_wire.h"
#include <string.h>

/**
 * @brief 校验一条CPU消息的操作、角色与标识字段组合。
 * @param message 可为NULL的只读消息，仅本次借用。
 * @return 合法组合为true。
 */
static bool ValidMessage(const ModeControlMessage *message)
{
    if(message==NULL || message->b_boot==0U ||
       (message->role!=BOARD_ROLE_A && message->role!=BOARD_ROLE_B))
        return false;

    if(message->op==MODE_CONTROL_HELLO)
        return message->role==BOARD_ROLE_B && message->mode==MODE_NONE &&
               message->mode_seq==0U && message->probe_id==0U;

    if(message->mode<MODE_RANGE || message->mode>MODE_CALIBRATION ||
       message->a_boot==0U || message->mode_seq==0U)
        return false;

    switch(message->op)
    {
    case MODE_CONTROL_PREPARE:
    case MODE_CONTROL_COMMIT:
        return message->role==BOARD_ROLE_A && message->probe_id==0U;
    case MODE_CONTROL_READY:
    case MODE_CONTROL_COMMITTED:
        return message->role==BOARD_ROLE_B && message->probe_id==0U;
    case MODE_CONTROL_PING:
    case MODE_CONTROL_PONG:
        return message->probe_id!=0U;
    default:
        return false;
    }
}

/**
 * @brief 将64位无符号标识逐字节写入大端缓冲。
 * @param data 非NULL，指向调用方持有的八字节可写字段。
 * @param value 待写的无单位标识。
 * @return 无返回值。
 */
static void Put64(uint8_t *data, uint64_t value)
{
    for(unsigned i=0U;i<8U;++i)
        data[i]=(uint8_t)(value>>(8U*(7U-i)));
}

/**
 * @brief 从大端缓冲逐字节读取64位无符号标识。
 * @param data 非NULL，指向借用的八字节只读字段。
 * @return 无单位的无符号标识。
 */
static uint64_t Get64(const uint8_t *data)
{
    uint64_t value=0U;
    for(unsigned i=0U;i<8U;++i)
        value=(value<<8U)|data[i];
    return value;
}

/**
 * @brief 校验消息后写入MDP1头及四个64位标识字段。
 * @param message 非NULL只读消息，仅本次借用。
 * @param data 非NULL可写缓冲，由调用方持有。
 * @param length 缓冲长度，单位字节，必须为40。
 * @return 成功为true，失败保持输出缓冲原样。
 */
bool ModeControlWire_Encode(const ModeControlMessage *message, uint8_t *data, uint16_t length)
{
    if(data==NULL || length!=MODE_CONTROL_WIRE_BYTES || !ValidMessage(message))
        return false;

    memcpy(data,"MDP1",4U);
    data[4]=1U;
    data[5]=(uint8_t)message->op;
    data[6]=(uint8_t)message->mode;
    data[7]=(uint8_t)message->role;
    Put64(data+8U,message->a_boot);
    Put64(data+16U,message->b_boot);
    Put64(data+24U,message->mode_seq);
    Put64(data+32U,message->probe_id);
    return true;
}

/**
 * @brief 在局部副本完成MDP1的长度、版本和字段检查。
 * @param data 非NULL只读字节缓冲，仅本次借用。
 * @param length 报文长度，单位字节，必须为40。
 * @param out 非NULL输出，由调用方持有。
 * @return 成功为true，失败保持out原样。
 */
bool ModeControlWire_Decode(const uint8_t *data, uint16_t length, ModeControlMessage *out)
{
    if(data==NULL || out==NULL || length!=MODE_CONTROL_WIRE_BYTES ||
       memcmp(data,"MDP1",4U)!=0 || data[4]!=1U)
        return false;

    const ModeControlMessage message={
        .op=(ModeControlOp)data[5],
        .mode=(ModeKind)data[6],
        .role=(BoardRole)data[7],
        .a_boot=Get64(data+8U),
        .b_boot=Get64(data+16U),
        .mode_seq=Get64(data+24U),
        .probe_id=Get64(data+32U)
    };
    if(!ValidMessage(&message))return false;
    *out=message;
    return true;
}
