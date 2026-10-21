#include "dht11_frame.h"
#include "temperature_config.h"

#include <stddef.h>

Dht11FrameStatus Dht11Frame_Decode(const uint8_t frame[5], Dht11Sample *out)
{
    unsigned checksum;
    uint32_t relative_humidity_milli_pct;
    int32_t temperature_milli_c;
    uint8_t temperature_tenths;

    if ((frame == NULL) || (out == NULL))
    {
        return DHT11_FRAME_ARGUMENT;
    }

    /* Sum the raw bytes, including the sign bit, modulo 256. */
    checksum = (unsigned)frame[0] + frame[1] + frame[2] + frame[3];
    if ((uint8_t)checksum != frame[4])
    {
        return DHT11_FRAME_CHECKSUM;
    }

    temperature_tenths = frame[3] & UINT8_C(0x7F);
    relative_humidity_milli_pct = (uint32_t)frame[0] * 1000U;
    temperature_milli_c = (int32_t)frame[2] * 1000
                         + (int32_t)temperature_tenths * 100;
    if ((frame[3] & UINT8_C(0x80)) != 0U)
    {
        temperature_milli_c = -temperature_milli_c;
    }

    if ((frame[1] != 0U) || (temperature_tenths > 9U)
        || (relative_humidity_milli_pct < TEMP_RH_MIN_MILLI_PCT)
        || (relative_humidity_milli_pct > TEMP_RH_MAX_MILLI_PCT)
        || (temperature_milli_c < TEMP_MIN_MILLI_C)
        || (temperature_milli_c > TEMP_MAX_MILLI_C))
    {
        return DHT11_FRAME_RANGE;
    }

    out->temperature_milli_c = temperature_milli_c;
    out->relative_humidity_milli_pct = relative_humidity_milli_pct;
    return DHT11_FRAME_OK;
}
