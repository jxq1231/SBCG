/**
 * @file        measurement_ack.h
 * @brief       B板对A板权威测量结果的16字节MACK确认报文。
 * @details     回显测量ID和共同域令牌；显式大端、magic/版本/状态校验。
 * @note        只证明B收到并接受了该结果，不证明距离/同步精度。
 */
#ifndef SBCG_MEASUREMENT_ACK_H
#define SBCG_MEASUREMENT_ACK_H

#include <stdbool.h>
#include <stdint.h>

#define MEASUREMENT_ACK_BYTES 16U
#define MEASUREMENT_ACK_VERSION 1U

typedef enum
{
    MEASUREMENT_ACK_OK = 0,
    MEASUREMENT_ACK_INVALID
} MeasurementAckStatus;

typedef struct
{
    MeasurementAckStatus status;
    uint32_t measurement_id;
    uint32_t time_domain;
} MeasurementAck;

typedef enum
{
    MEASUREMENT_ACK_WIRE_OK = 0,
    MEASUREMENT_ACK_WIRE_INVALID_ARGUMENT,
    MEASUREMENT_ACK_WIRE_INVALID_LENGTH,
    MEASUREMENT_ACK_WIRE_INVALID_MAGIC,
    MEASUREMENT_ACK_WIRE_INVALID_VERSION,
    MEASUREMENT_ACK_WIRE_INVALID_FIELD
} MeasurementAckWireStatus;

MeasurementAckWireStatus MeasurementAck_Encode(const MeasurementAck *ack,
                                                 uint8_t *data, uint16_t length);
MeasurementAckWireStatus MeasurementAck_Decode(const uint8_t *data, uint16_t length,
                                                 MeasurementAck *ack);

#endif
