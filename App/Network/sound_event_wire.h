/**
 * @file        sound_event_wire.h
 * @brief       两板声音事件的最小UDP线协议编解码（纯C，无socket）。
 * @details     显式大端序字段；只承载已配对所需身份/时间域/质量/时间，不携带PCM。
 *              本模块不做发送、接收、重试或同步；实际网络接入是后续独立增量。
 * @note        事件时间必须是本地PTP模型时间；线协议不证明跨板同步。
 */
#ifndef SBCG_SOUND_EVENT_WIRE_H
#define SBCG_SOUND_EVENT_WIRE_H

#include "Measure/sound_pair.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SOUND_EVENT_WIRE_BYTES 36U
#define SOUND_EVENT_WIRE_VERSION 1U
/** @brief 线协议解码状态；OK才产生事件副本。 */
typedef enum
{
    SOUND_EVENT_WIRE_OK = 0,
    SOUND_EVENT_WIRE_BAD_ARGUMENT,
    SOUND_EVENT_WIRE_BAD_LENGTH,
    SOUND_EVENT_WIRE_BAD_MAGIC,
    SOUND_EVENT_WIRE_BAD_VERSION,
    SOUND_EVENT_WIRE_BAD_FIELD
} SoundEventWireStatus;

/** @brief 将事件编码为固定36字节协议；out容量不足或字段非法返回false。 */
SoundEventWireStatus SoundEventWire_Encode(const SoundEventRecord *event,
                                           uint8_t *out, size_t capacity);

/** @brief 解码固定协议；成功填充event并返回OK，失败不产生有效事件。 */
SoundEventWireStatus SoundEventWire_Decode(const uint8_t *data, size_t length,
                                           SoundEventRecord *event);

#endif
