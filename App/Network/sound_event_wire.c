/**
 * @file        sound_event_wire.c
 * @brief       两板声音事件最小线协议编解码实现。
 * @details     big-endian v3固定64字节；v2的56字节后追加模型进度帧；兼容解码v1(36)/v2(56)
 *              旧事件；无socket、配对或同步行为。
 * @note        失败不返回部分有效事件；协议本身不代表跨板时间已同步。
 */
#include "sound_event_wire.h"
#include <string.h>

#define WIRE_OFF_MAGIC 0U
#define WIRE_OFF_VERSION 4U
#define WIRE_OFF_SIDE 5U
#define WIRE_OFF_TYPE 6U
#define WIRE_OFF_QUALITY 7U
#define WIRE_OFF_DOMAIN 8U
#define WIRE_OFF_SESSION 12U
#define WIRE_OFF_SEQUENCE 16U
#define WIRE_OFF_SECONDS 20U
#define WIRE_OFF_NANOS 24U
#define WIRE_OFF_FLAGS 28U
#define WIRE_OFF_RESERVED 32U
#define WIRE_OFF_CANDIDATE_COUNT 36U
#define WIRE_OFF_CANDIDATE_RESERVED 37U
#define WIRE_OFF_CANDIDATE_Q16 40U
#define WIRE_OFF_FRAME_HIGH 56U
#define WIRE_OFF_FRAME_LOW 60U
#define NSEC_PER_SEC UINT64_C(1000000000)

static void Put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24U); p[1] = (uint8_t)(value >> 16U);
    p[2] = (uint8_t)(value >> 8U); p[3] = (uint8_t)value;
}
static uint32_t Get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24U | (uint32_t)p[1] << 16U |
           (uint32_t)p[2] << 8U | (uint32_t)p[3];
}
static bool EventFieldsValid(const SoundEventRecord *event)
{
    return event != NULL && event->valid && event->side <= SOUND_SIDE_B &&
           event->type != SOUND_EVENT_INVALID &&
           (event->type == SOUND_EVENT_CHIRP || event->type == SOUND_EVENT_CLAP) &&
           event->quality <= 100U && event->time_domain != 0U &&
           event->sequence != 0U &&
           event->toa_candidate_count <= SOUND_EVENT_MAX_TOA_CANDIDATES;
}

SoundEventWireStatus SoundEventWire_Encode(const SoundEventRecord *event,
                                           uint8_t *out, size_t capacity)
{
    if (event == NULL || out == NULL) { return SOUND_EVENT_WIRE_BAD_ARGUMENT; }
    if (capacity < SOUND_EVENT_WIRE_BYTES) { return SOUND_EVENT_WIRE_BAD_LENGTH; }
    if (!EventFieldsValid(event)) { return SOUND_EVENT_WIRE_BAD_FIELD; }
    const uint64_t seconds64 = event->time_ns / NSEC_PER_SEC;
    const uint32_t nanos = (uint32_t)(event->time_ns % NSEC_PER_SEC);
    if (seconds64 > UINT32_MAX) { return SOUND_EVENT_WIRE_BAD_FIELD; }
    memset(out, 0, SOUND_EVENT_WIRE_BYTES);
    out[WIRE_OFF_MAGIC] = 'S'; out[WIRE_OFF_MAGIC + 1U] = 'E';
    out[WIRE_OFF_MAGIC + 2U] = 'V'; out[WIRE_OFF_MAGIC + 3U] = 'T';
    out[WIRE_OFF_VERSION] = SOUND_EVENT_WIRE_VERSION;
    out[WIRE_OFF_SIDE] = (uint8_t)event->side;
    out[WIRE_OFF_TYPE] = (uint8_t)event->type;
    out[WIRE_OFF_QUALITY] = event->quality;
    Put32(out + WIRE_OFF_DOMAIN, event->time_domain);
    Put32(out + WIRE_OFF_SESSION, event->session);
    Put32(out + WIRE_OFF_SEQUENCE, event->sequence);
    Put32(out + WIRE_OFF_SECONDS, (uint32_t)seconds64);
    Put32(out + WIRE_OFF_NANOS, nanos);
    Put32(out + WIRE_OFF_FLAGS, 0U);
    Put32(out + WIRE_OFF_RESERVED, 0U);
    out[WIRE_OFF_CANDIDATE_COUNT] = event->toa_candidate_count;
    out[WIRE_OFF_CANDIDATE_RESERVED] = 0U;
    out[WIRE_OFF_CANDIDATE_RESERVED + 1U] = 0U;
    out[WIRE_OFF_CANDIDATE_RESERVED + 2U] = 0U;
    for (uint8_t i = 0U; i < SOUND_EVENT_MAX_TOA_CANDIDATES; ++i)
    {
        Put32(out + WIRE_OFF_CANDIDATE_Q16 + 4U * i,
              (uint32_t)event->toa_candidate_q16[i]);
    }
    /* v3追加本地模型进度帧，两板用它在同一音频位置截取波形窗口。 */
    Put32(out + WIRE_OFF_FRAME_HIGH, (uint32_t)(event->frame >> 32U));
    Put32(out + WIRE_OFF_FRAME_LOW, (uint32_t)event->frame);
    return SOUND_EVENT_WIRE_OK;
}

SoundEventWireStatus SoundEventWire_Decode(const uint8_t *data, size_t length,
                                           SoundEventRecord *event)
{
    if (event == NULL) { return SOUND_EVENT_WIRE_BAD_ARGUMENT; }
    memset(event, 0, sizeof *event);
    if (data == NULL) { return SOUND_EVENT_WIRE_BAD_ARGUMENT; }
    if (length != SOUND_EVENT_WIRE_V1_BYTES && length != SOUND_EVENT_WIRE_V2_BYTES &&
        length != SOUND_EVENT_WIRE_BYTES)
    { return SOUND_EVENT_WIRE_BAD_LENGTH; }
    if (data[WIRE_OFF_MAGIC] != 'S' || data[WIRE_OFF_MAGIC + 1U] != 'E' ||
        data[WIRE_OFF_MAGIC + 2U] != 'V' || data[WIRE_OFF_MAGIC + 3U] != 'T')
    { return SOUND_EVENT_WIRE_BAD_MAGIC; }
    const uint8_t version = data[WIRE_OFF_VERSION];
    if (!((version == 1U && length == SOUND_EVENT_WIRE_V1_BYTES) ||
          (version == 2U && length == SOUND_EVENT_WIRE_V2_BYTES) ||
          (version == 3U && length == SOUND_EVENT_WIRE_BYTES)))
    { return SOUND_EVENT_WIRE_BAD_VERSION; }
    if (data[WIRE_OFF_SIDE] > (uint8_t)SOUND_SIDE_B) { return SOUND_EVENT_WIRE_BAD_FIELD; }
    const SoundEventType type = (SoundEventType)data[WIRE_OFF_TYPE];
    if (type != SOUND_EVENT_CHIRP && type != SOUND_EVENT_CLAP)
    { return SOUND_EVENT_WIRE_BAD_FIELD; }
    if (data[WIRE_OFF_QUALITY] > 100U) { return SOUND_EVENT_WIRE_BAD_FIELD; }
    if (Get32(data + WIRE_OFF_FLAGS) != 0U ||
        Get32(data + WIRE_OFF_RESERVED) != 0U)
    { return SOUND_EVENT_WIRE_BAD_FIELD; }
    const uint32_t seconds = Get32(data + WIRE_OFF_SECONDS);
    const uint32_t nanos = Get32(data + WIRE_OFF_NANOS);
    if (nanos >= (uint32_t)NSEC_PER_SEC) { return SOUND_EVENT_WIRE_BAD_FIELD; }
    event->valid = true;
    event->type = type;
    event->quality = data[WIRE_OFF_QUALITY];
    event->side = (SoundEventSide)data[WIRE_OFF_SIDE];
    event->time_domain = Get32(data + WIRE_OFF_DOMAIN);
    event->session = Get32(data + WIRE_OFF_SESSION);
    event->sequence = Get32(data + WIRE_OFF_SEQUENCE);
    if (event->time_domain == 0U || event->sequence == 0U)
    { memset(event, 0, sizeof *event); return SOUND_EVENT_WIRE_BAD_FIELD; }
    event->time_ns = (uint64_t)seconds * NSEC_PER_SEC + nanos;
    if (version >= 2U)
    {
        const uint8_t count = data[WIRE_OFF_CANDIDATE_COUNT];
        if (count > SOUND_EVENT_MAX_TOA_CANDIDATES ||
            data[WIRE_OFF_CANDIDATE_RESERVED] != 0U ||
            data[WIRE_OFF_CANDIDATE_RESERVED + 1U] != 0U ||
            data[WIRE_OFF_CANDIDATE_RESERVED + 2U] != 0U)
        { memset(event, 0, sizeof *event); return SOUND_EVENT_WIRE_BAD_FIELD; }
        event->toa_candidate_count = count;
        for (uint8_t i = 0U; i < SOUND_EVENT_MAX_TOA_CANDIDATES; ++i)
        {
            event->toa_candidate_q16[i] =
                (int32_t)Get32(data + WIRE_OFF_CANDIDATE_Q16 + 4U * i);
        }
    }
    if (version == 3U)
    {
        event->frame = (uint64_t)Get32(data + WIRE_OFF_FRAME_HIGH) << 32U |
                       Get32(data + WIRE_OFF_FRAME_LOW);
    }
    return SOUND_EVENT_WIRE_OK;
}
