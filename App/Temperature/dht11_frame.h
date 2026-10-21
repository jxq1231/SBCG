#ifndef SBCG_DHT11_FRAME_H
#define SBCG_DHT11_FRAME_H

#include <stdint.h>

typedef struct
{
    int32_t temperature_milli_c;
    uint32_t relative_humidity_milli_pct;
} Dht11Sample;

typedef enum Dht11FrameStatus
{
    DHT11_FRAME_OK = 0,
    DHT11_FRAME_ARGUMENT,
    DHT11_FRAME_CHECKSUM,
    DHT11_FRAME_RANGE
} Dht11FrameStatus;

/* AoSong DHT11 V1.3: signed tenths in byte 3; byte 1 must be zero.
 * Neither input nor output is changed on failure. */
Dht11FrameStatus Dht11Frame_Decode(const uint8_t frame[5], Dht11Sample *out);

#endif
