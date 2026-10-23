/**
 * @file        dht11_frame.c
 * @brief       校验并解析DHT11五字节帧。
 * @details     校验原始字节和，解析带符号十分之一摄氏度温度及整数湿度，按配置范围输出毫摄氏度和千分之一百分比。
 * @note        纯解析不采集GPIO；输入只读，失败保持输出不变。
 */
#include "dht11_frame.h"
#include "temperature_config.h"

#include <stddef.h>

/**
 * @brief   校验五字节帧并解码温湿度。
 * @param   frame 只读5字节原始帧，不可为NULL；字节3高位为温度符号，字节1须为0。
 * @param   out 可写采样对象，不可为NULL，仅成功时写入毫摄氏度与千分之一百分比。
 * @return  返回OK、参数错误、校验错误或范围错误。
 * @note    纯计算；校验和包含符号位并取低8位，失败保持输出。
 */
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
