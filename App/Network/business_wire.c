/**
 * @file        business_wire.c
 * @brief       实现MDB1独立业务封装的32字节大端外头编解码。
 * @details     外头布局：0..3 "MDB1"、4 版本1、5 发送角色、6..7 内层长度、
 *              8..15 A启动代次、16..23 B启动代次、24..31 模式序号；32开始为原内层字节。
 *              编码先校验后写，解码先长度后逐字段校验，失败不交付半成品。
 * @note        纯函数，无全局状态、无网络访问；不解释内层报文。
 */
#include "business_wire.h"
#include <string.h>

#define BUSINESS_WIRE_VERSION 1U

/** @brief 按大端写入16位整数。 @param out 非NULL可写字段。 @param value 待写值。 @return 无返回值。 */
static void Put16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8U);
    out[1] = (uint8_t)value;
}

/** @brief 按大端读取16位整数。 @param in 非NULL只读字段。 @return 无符号值。 */
static uint16_t Get16(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0] << 8U) | in[1]);
}

/** @brief 按大端写入64位整数。 @param out 非NULL可写字段。 @param value 待写值。 @return 无返回值。 */
static void Put64(uint8_t *out, uint64_t value)
{
    for (unsigned i = 0U; i < 8U; ++i)
    {
        out[i] = (uint8_t)(value >> (8U * (7U - i)));
    }
}

/** @brief 按大端读取64位整数。 @param in 非NULL只读字段。 @return 无符号值。 */
static uint64_t Get64(const uint8_t *in)
{
    uint64_t value = 0U;
    for (unsigned i = 0U; i < 8U; ++i)
    {
        value = (value << 8U) | in[i];
    }
    return value;
}

/** @brief 校验角色/长度/代次/序号组合。 @param message 可为NULL。 @return 合法为true。 */
static bool MessageValid(const BusinessWireMessage *message)
{
    return message != NULL &&
        (message->role == BOARD_ROLE_A || message->role == BOARD_ROLE_B) &&
        message->inner_length != 0U &&
        message->inner_length <= BUSINESS_WIRE_MAX_INNER &&
        message->a_boot != 0U && message->b_boot != 0U && message->mode_seq != 0U;
}

/**
 * @brief   编码32字节MDB1外头。
 * @param   message 非NULL只读字段。
 * @param   out 非NULL32字节可写缓冲。
 * @return  成功为true。
 */
bool BusinessWire_EncodeHeader(const BusinessWireMessage *message,
                               uint8_t out[BUSINESS_WIRE_HEADER_BYTES])
{
    if (out == NULL || !MessageValid(message))
    {
        return false;
    }
    memcpy(out, "MDB1", 4U);
    out[4] = (uint8_t)BUSINESS_WIRE_VERSION;
    out[5] = (uint8_t)message->role;
    Put16(&out[6], message->inner_length);
    Put64(&out[8], message->a_boot);
    Put64(&out[16], message->b_boot);
    Put64(&out[24], message->mode_seq);
    return true;
}

/**
 * @brief   校验并解码32字节MDB1外头。
 * @param   data 非NULL只读缓冲。
 * @param   length 可用字节数。
 * @param   out 非NULL输出。
 * @return  成功为true。
 */
bool BusinessWire_DecodeHeader(const uint8_t *data, uint16_t length,
                               BusinessWireMessage *out)
{
    if (data == NULL || out == NULL || length < BUSINESS_WIRE_HEADER_BYTES ||
        memcmp(data, "MDB1", 4U) != 0 || data[4] != (uint8_t)BUSINESS_WIRE_VERSION)
    {
        return false;
    }
    const BusinessWireMessage message = {
        .role = (BoardRole)data[5],
        .inner_length = Get16(&data[6]),
        .a_boot = Get64(&data[8]),
        .b_boot = Get64(&data[16]),
        .mode_seq = Get64(&data[24]),
        .inner = NULL
    };
    if (!MessageValid(&message))
    {
        return false;
    }
    *out = message;
    return true;
}

/**
 * @brief   编码完整MDB1载荷。
 * @param   message 非NULL只读消息。
 * @param   out 非NULL可写缓冲。
 * @param   out_length 输出缓冲容量。
 * @return  成功为true。
 */
bool BusinessWire_Encode(const BusinessWireMessage *message, uint8_t *out,
                         uint16_t out_length)
{
    if (message == NULL || message->inner == NULL || out == NULL ||
        !MessageValid(message))
    {
        return false;
    }
    const uint32_t total = (uint32_t)BUSINESS_WIRE_HEADER_BYTES + message->inner_length;
    if (total > out_length)
    {
        return false;
    }
    if (!BusinessWire_EncodeHeader(message, out))
    {
        return false;
    }
    memcpy(out + BUSINESS_WIRE_HEADER_BYTES, message->inner, message->inner_length);
    return true;
}

/**
 * @brief   校验并解码完整MDB1载荷。
 * @param   data 非NULL只读缓冲。
 * @param   length 载荷总字节数。
 * @param   out 非NULL输出。
 * @return  成功为true。
 */
bool BusinessWire_Decode(const uint8_t *data, uint16_t length, BusinessWireMessage *out)
{
    if (data == NULL || out == NULL || length <= BUSINESS_WIRE_HEADER_BYTES ||
        length > BUSINESS_WIRE_MAX_BYTES)
    {
        return false;
    }
    BusinessWireMessage message;
    if (!BusinessWire_DecodeHeader(data, length, &message) ||
        message.inner_length != (uint16_t)(length - BUSINESS_WIRE_HEADER_BYTES))
    {
        return false;
    }
    message.inner = data + BUSINESS_WIRE_HEADER_BYTES;
    *out = message;
    return true;
}
