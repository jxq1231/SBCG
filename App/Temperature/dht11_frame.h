/**
 * @file        dht11_frame.h
 * @brief       校验并解析DHT11五字节帧。
 * @details     声明本模块接口、数据类型或配置常量。校验原始字节和，解析带符号十分之一摄氏度温度及整数湿度，按配置范围输出毫摄氏度和千分之一百分比。
 * @note        纯解析不采集GPIO；输入只读，失败保持输出不变。
 */
#ifndef SBCG_DHT11_FRAME_H
#define SBCG_DHT11_FRAME_H

#include <stdint.h>

/** @brief 已通过帧校验和配置范围检查的温湿度；单位分别为毫摄氏度和千分之一百分比。 */
typedef struct
{
    int32_t temperature_milli_c;
    uint32_t relative_humidity_milli_pct;
} Dht11Sample;

/** @brief 帧解码状态：成功、空指针参数、校验和或数值范围错误。 */
typedef enum Dht11FrameStatus
{
    DHT11_FRAME_OK = 0,
    DHT11_FRAME_ARGUMENT,
    DHT11_FRAME_CHECKSUM,
    DHT11_FRAME_RANGE
} Dht11FrameStatus;

/* AoSong DHT11 V1.3: signed tenths in byte 3; byte 1 must be zero.
 * Neither input nor output is changed on failure. */
/**
 * @brief   校验五字节帧并解码温湿度。
 * @param   frame 只读5字节原始帧，不可为NULL；字节3高位为温度符号，字节1须为0。
 * @param   out 可写采样对象，不可为NULL，仅成功时写入毫摄氏度与千分之一百分比。
 * @return  返回OK、参数错误、校验错误或范围错误。
 * @note    纯计算；校验和包含符号位并取低8位，失败保持输出。
 */
Dht11FrameStatus Dht11Frame_Decode(const uint8_t frame[5], Dht11Sample *out);

#endif
