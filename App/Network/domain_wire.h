/**
 * @file        domain_wire.h
 * @brief       A/B共同时间域请求与确认的24字节UDP线协议。
 * @details     显式大端字段：类型、角色、本地资格状态、A派生令牌、启动nonce、序号。
 * @note        这是“双方看到同一会话令牌”的软件证据，不是PTP epoch或外部精度证明。
 */
#ifndef SBCG_DOMAIN_WIRE_H
#define SBCG_DOMAIN_WIRE_H

#include "BSP/board_identity.h"
#include <stdbool.h>
#include <stdint.h>

#define DOMAIN_WIRE_BYTES 24U
#define DOMAIN_WIRE_VERSION 1U

typedef enum
{
    DOMAIN_MESSAGE_REQUEST = 1,
    DOMAIN_MESSAGE_ACK = 2
} DomainMessageType;

typedef enum
{
    DOMAIN_LOCAL_INVALID = 0,
    DOMAIN_LOCAL_VALID = 1
} DomainLocalState;

typedef struct
{
    DomainMessageType type;
    BoardRole role;
    DomainLocalState state;
    uint32_t token;
    uint32_t boot_nonce;
    uint32_t sequence;
} DomainMessage;

typedef enum
{
    DOMAIN_WIRE_OK = 0,
    DOMAIN_WIRE_INVALID_ARGUMENT,
    DOMAIN_WIRE_INVALID_LENGTH,
    DOMAIN_WIRE_INVALID_MAGIC,
    DOMAIN_WIRE_INVALID_VERSION,
    DOMAIN_WIRE_INVALID_FIELD
} DomainWireStatus;

DomainWireStatus DomainWire_Encode(const DomainMessage *message,
                                    uint8_t *data, uint16_t length);
DomainWireStatus DomainWire_Decode(const uint8_t *data, uint16_t length,
                                    DomainMessage *message);

#endif
