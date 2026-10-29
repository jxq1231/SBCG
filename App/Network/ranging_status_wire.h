/**
 * @file        ranging_status_wire.h
 * @brief       A权威测距状态RST2/RAK2纯编解码。
 * @details     固定88/24字节、显式大端；状态/新鲜度/源截止时间/证据类型分开表达。
 * @note        只做字节布局与字段校验，不持有接收历史、不发UDP、不访问硬件。
 */
#ifndef SBCG_RANGING_STATUS_WIRE_H
#define SBCG_RANGING_STATUS_WIRE_H

#include "Measure/ranging_tracker.h"
#include <stdbool.h>
#include <stdint.h>

#define RANGING_STATUS_WIRE_BYTES 88U
#define RANGING_STATUS_ACK_BYTES 24U
#define RANGING_STATUS_WIRE_VERSION 2U

#define RANGING_STATUS_FLAG_HAS_VALUE         UINT16_C(0x0001)
#define RANGING_STATUS_FLAG_FRESH             UINT16_C(0x0002)
#define RANGING_STATUS_FLAG_IDENTITY_VERIFIED UINT16_C(0x0004)
#define RANGING_STATUS_FLAG_FORMAL            UINT16_C(0x0008)
#define RANGING_STATUS_FLAG_KNOWN             UINT16_C(0x000F)

/** @brief RAK2确认状态；只确认精确revision，不续租。 */
typedef enum
{
    RANGING_STATUS_ACK_OK = 0,
    RANGING_STATUS_ACK_REJECT = 1,
    RANGING_STATUS_ACK_FAULT = 2
} RangingStatusAckState;

/** @brief 编解码错误；不把部分写入当成功。 */
typedef enum
{
    RANGING_STATUS_WIRE_OK = 0,
    RANGING_STATUS_WIRE_INVALID_ARGUMENT,
    RANGING_STATUS_WIRE_INVALID_LENGTH,
    RANGING_STATUS_WIRE_INVALID_MAGIC,
    RANGING_STATUS_WIRE_INVALID_VERSION,
    RANGING_STATUS_WIRE_INVALID_FIELD
} RangingStatusWireStatus;

/** @brief RST2的只读/可写字段；flags显式给出，编码器校验组合关系。 */
typedef struct
{
    uint8_t state;                 /**< RangingTrackerState数值。 */
    uint8_t direction;             /**< 0未知，1A侧，2B侧。 */
    uint8_t quality;               /**< 0..100，代理/实际支持质量。 */
    uint16_t flags;                /**< RANGING_STATUS_FLAG_*。 */
    uint32_t domain;
    uint32_t authority_clock_epoch;
    uint32_t state_revision;
    uint32_t measurement_id;
    uint32_t temperature_version;
    uint32_t sound_speed_mm_s;
    int32_t distance_mm;
    uint16_t calibration_version;
    int16_t calibration_offset_mm;
    uint16_t reason;               /**< 状态原因短码，由上层定义。 */
    uint64_t source_common_ns;
    uint64_t fresh_deadline_ns;
    uint64_t display_deadline_ns;
    int64_t filtered_delta_ns;
    uint32_t source_key_crc32;     /**< 仅诊断摘要，不是唯一去重键。 */
    uint16_t detection_version;
    uint8_t evidence_kind;         /**< RangingEvidenceKind。 */
} RangingStatusMessage;

/** @brief RAK2确认字段；status只接受枚举值。 */
typedef struct
{
    uint8_t status;
    uint32_t domain;
    uint32_t authority_clock_epoch;
    uint32_t state_revision;
    uint32_t measurement_id;
} RangingStatusAck;

/** @brief 编码RST2；长度不足/字段组合非法返回错误且不保证输出。 */
RangingStatusWireStatus RangingStatusWire_Encode(const RangingStatusMessage *message,
                                                  uint8_t *data, uint16_t length);

/** @brief 解码RST2；成功只写out，失败不把未知字段当成可用结果。 */
RangingStatusWireStatus RangingStatusWire_Decode(const uint8_t *data, uint16_t length,
                                                  RangingStatusMessage *out);

/** @brief 编码RAK2；固定24字节大端。 */
RangingStatusWireStatus RangingStatusAck_Encode(const RangingStatusAck *ack,
                                                 uint8_t *data, uint16_t length);

/** @brief 解码RAK2；保留字、版本和状态必须合法。 */
RangingStatusWireStatus RangingStatusAck_Decode(const uint8_t *data, uint16_t length,
                                                 RangingStatusAck *out);

#endif
