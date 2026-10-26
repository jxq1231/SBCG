/**
 * @file        measurement_wire.h
 * @brief       A板权威测量结果到B板的28字节UDP线协议。
 * @details     显式大端字段：结果ID、距离、方向、质量、声速、温度版本、共同时间域。
 * @note        只传结果，不传PCM/PTP原始时间；接收端不得用接收时刻替代测量时间。
 */
#ifndef SBCG_MEASUREMENT_WIRE_H
#define SBCG_MEASUREMENT_WIRE_H

#include "Measure/measurement_result.h"
#include <stdbool.h>
#include <stdint.h>

#define MEASUREMENT_WIRE_BYTES 28U
#define MEASUREMENT_WIRE_VERSION 1U

typedef enum
{
    MEASUREMENT_WIRE_OK = 0,
    MEASUREMENT_WIRE_INVALID_ARGUMENT,
    MEASUREMENT_WIRE_INVALID_LENGTH,
    MEASUREMENT_WIRE_INVALID_MAGIC,
    MEASUREMENT_WIRE_INVALID_VERSION,
    MEASUREMENT_WIRE_INVALID_FIELD
} MeasurementWireStatus;

MeasurementWireStatus MeasurementWire_Encode(const MeasurementResultSnapshot *result,
                                               uint8_t *data, uint16_t length);
MeasurementWireStatus MeasurementWire_Decode(const uint8_t *data, uint16_t length,
                                               MeasurementResultSnapshot *result);

#endif
