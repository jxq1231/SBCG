/**
 * @file        temperature_wire.h
 * @brief       A板共享温度到B板的20字节UDP线协议。
 * @details     固定大端字段：有效标志、来源、毫摄氏度、声速、温度版本；
 *              解码端校验数值范围及固定声速公式，不用于距离计算。
 * @note        纯字节编解码，无网络/HAL/动态内存；温度不是PTP时间。
 */
#ifndef SBCG_TEMPERATURE_WIRE_H
#define SBCG_TEMPERATURE_WIRE_H

#include <stdbool.h>
#include <stdint.h>

#define TEMPERATURE_WIRE_BYTES 20U
#define TEMPERATURE_WIRE_VERSION 1U

/** @brief 共享温度来源；A板唯一自动源或A板手动值。 */
typedef enum
{
    TEMPERATURE_SOURCE_AUTO = 0,
    TEMPERATURE_SOURCE_MANUAL = 1
} TemperatureWireSource;

/** @brief 一条A→B共享温度消息；valid只表示字段可用。 */
typedef struct
{
    bool valid;
    TemperatureWireSource source;
    int32_t temperature_milli_c;
    uint32_t sound_speed_mm_s;
    uint32_t version;
} TemperatureWireMessage;

/**
 * @brief 编码一条有效共享温度消息。
 * @param message 非NULL输入；温度/声速/版本必须通过范围校验。
 * @param data 非NULL输出，至少TEMPERATURE_WIRE_BYTES字节。
 * @param length 输出容量，必须不小于TEMPERATURE_WIRE_BYTES。
 * @return 成功返回OK；参数/字段错误返回对应状态。
 */
bool TemperatureWire_Encode(const TemperatureWireMessage *message,
                            uint8_t *data, uint16_t length);

/**
 * @brief 解码并校验一条共享温度消息。
 * @param data 非NULL输入，长度至少TEMPERATURE_WIRE_BYTES。
 * @param length 输入长度，必须精确等于TEMPERATURE_WIRE_BYTES。
 * @param message 非NULL输出；失败时清零。
 * @return 成功返回true；否则false。
 * @note 声速必须与当前整数声速公式一致，避免B板使用另一套算法。
 */
bool TemperatureWire_Decode(const uint8_t *data, uint16_t length,
                            TemperatureWireMessage *message);

#endif
