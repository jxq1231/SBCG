/**
 * @file        clap_wire.h
 * @brief       CLP1固定80字节结果及CLA1固定24字节ACK的大端协议。
 * @details     结果偏移：0 magic[4]，4 version=1，5 reserved=0，6 direction u8，7 quality u8，
 *              8 domain u32，12 epoch u32，16 id u32，20/24 A session/seq u32，
 *              28/32 B session/seq u32，36 source ns u64，44 delta ns i64，
 *              52 distance mm i32，56 speed mm/s u32，60 temperature version u32，
 *              64 temperature milli-C i32，68 calibration version u16，70 offset mm i16，
 *              72..79 reserved=0。ACK偏移：0 magic[4]，4 domain u32，8 epoch u32，
 *              12 id u32，16 source ns u64；ACK键四字段逐一精确比较。
 * @note        全部多字节字段使用大端；有符号值为二进制补码，不发送结构体，不包含新握手。
 */
#ifndef SBCG_CLAP_WIRE_H
#define SBCG_CLAP_WIRE_H

#include "Measure/clap_ranging.h"

#define CLAP_RESULT_WIRE_BYTES 80U
#define CLAP_ACK_WIRE_BYTES 24U

/** @brief 编码CLP1。 @param message 非NULL结果。 @param data 非NULL且至少length字节。 @param length 必须80。 @return true成功；失败不修改输出。 */
bool ClapWire_EncodeResult(const ClapResultMessage *message, uint8_t *data, uint16_t length);
/** @brief 解码CLP1并校验字段。 @param data 非NULL输入。 @param length 必须80。 @param out 非NULL输出。 @return true成功；失败不修改输出。 */
bool ClapWire_DecodeResult(const uint8_t *data, uint16_t length, ClapResultMessage *out);
/** @brief 编码CLA1。 @param ack 非NULL结果键。 @param data 非NULL且至少length字节。 @param length 必须24。 @return true成功；失败不修改输出。 */
bool ClapWire_EncodeAck(const ClapAckMessage *ack, uint8_t *data, uint16_t length);
/** @brief 解码CLA1。 @param data 非NULL输入。 @param length 必须24。 @param out 非NULL输出。 @return true成功；失败不修改输出。 */
bool ClapWire_DecodeAck(const uint8_t *data, uint16_t length, ClapAckMessage *out);

#endif
