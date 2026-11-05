/**
 * @file        business_wire.h
 * @brief       定义MDB1独立业务封装的固定32字节外头编解码。
 * @details     外头承载magic/版本/发送角色/内层长度和双方启动代次+模式序号，
 *              内层保留原SEV2、WFR1、RST2、RAK2报文的逐字节内容和语义。
 * @note        只做字节布局与字段校验；模式/方向/世代资格判断由udp_sound_event前台完成。
 */
#ifndef SBCG_BUSINESS_WIRE_H
#define SBCG_BUSINESS_WIRE_H

#include "BSP/board_identity.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief MDB1外头固定字节数。 */
#define BUSINESS_WIRE_HEADER_BYTES 32U
/** @brief MDB1封装允许的最大内层字节数（WFR1为1048，留出余量给WVP2的88）。 */
#define BUSINESS_WIRE_MAX_INNER 1088U
/** @brief MDB1封装后的最大载荷字节数。 */
#define BUSINESS_WIRE_MAX_BYTES (BUSINESS_WIRE_HEADER_BYTES + BUSINESS_WIRE_MAX_INNER)

/** @brief MDB1外头字段的CPU副本；inner仅Decode时指向调用方缓冲。 */
typedef struct
{
    BoardRole role;
    uint16_t inner_length;
    uint64_t a_boot;
    uint64_t b_boot;
    uint64_t mode_seq;
    const uint8_t *inner; /* Decode成功时指向data+32，仅本次调用有效。 */
} BusinessWireMessage;

/**
 * @brief   编码32字节MDB1外头，不接触内层字节。
 * @param   message 非NULL只读字段，inner可为NULL。
 * @param   out 非NULL32字节可写缓冲。
 * @return  成功为true；角色/长度/代次/序号非法为false且不保证输出。
 * @note    大端序；内层长度1..BUSINESS_WIRE_MAX_INNER。
 */
bool BusinessWire_EncodeHeader(const BusinessWireMessage *message,
                               uint8_t out[BUSINESS_WIRE_HEADER_BYTES]);

/**
 * @brief   校验并解码32字节MDB1外头，不做内层类型判断。
 * @param   data 非NULL只读缓冲，至少32字节。
 * @param   length 可用字节数，必须不小于32。
 * @param   out 非NULL输出；成功时inner为NULL，仅填字段。
 * @return  成功为true，失败不修改out。
 */
bool BusinessWire_DecodeHeader(const uint8_t *data, uint16_t length,
                               BusinessWireMessage *out);

/**
 * @brief   编码完整MDB1载荷（外头+内层逐字节拷贝）。
 * @param   message 非NULL只读消息，inner必须非NULL且长度匹配。
 * @param   out 非NULL可写缓冲。
 * @param   out_length 输出缓冲容量，必须至少32+inner_length。
 * @return  成功为true；容量不足或字段非法为false。
 */
bool BusinessWire_Encode(const BusinessWireMessage *message, uint8_t *out,
                         uint16_t out_length);

/**
 * @brief   校验并解码完整MDB1载荷。
 * @param   data 非NULL只读缓冲。
 * @param   length 载荷总字节数。
 * @param   out 非NULL输出；成功时inner指向data+32，仅本次调用有效。
 * @return  成功为true，失败不修改out。
 */
bool BusinessWire_Decode(const uint8_t *data, uint16_t length, BusinessWireMessage *out);

#endif
