/**
 * @file        mode_wire.h
 * @brief       定义双板模式控制类型及固定40字节MDP1编解码接口。
 * @details     CPU结构体仅用于前台消息交接，线上字段独立按大端序传输。
 * @note        模式状态机负责代次、序号及探测水位，UDP入口负责固定对端校验。
 */
#ifndef SBCG_MODE_WIRE_H
#define SBCG_MODE_WIRE_H

#include "BSP/board_identity.h"
#include <stdbool.h>
#include <stdint.h>

#define MODE_CONTROL_WIRE_BYTES 40U
#define MODE_CONTROL_WIRE_VERSION 1U

/** @brief 系统工作模式，NONE仅用于尚未同步的状态及HELLO。 */
typedef enum
{
    MODE_NONE = 0,
    MODE_RANGE,
    MODE_CLAP,
    MODE_WAVE,
    MODE_CALIBRATION
} ModeKind;

/** @brief MDP1控制操作的固定线上编号。 */
typedef enum
{
    MODE_CONTROL_HELLO = 1,
    MODE_CONTROL_PREPARE,
    MODE_CONTROL_READY,
    MODE_CONTROL_COMMIT,
    MODE_CONTROL_COMMITTED,
    MODE_CONTROL_PING,
    MODE_CONTROL_PONG
} ModeControlOp;

/** @brief MDP1的前台消息值，所有标识字段均为无单位的无符号64位整数。 */
typedef struct
{
    ModeControlOp op;
    ModeKind mode;
    BoardRole role;
    uint64_t a_boot;
    uint64_t b_boot;
    uint64_t mode_seq;
    uint64_t probe_id;
} ModeControlMessage;

/**
 * @brief 按固定布局编码经过字段校验的MDP1。
 * @param message 非NULL只读消息，仅本次借用。
 * @param data 非NULL可写缓冲，由调用方持有。
 * @param length 缓冲长度，单位字节，必须等于MODE_CONTROL_WIRE_BYTES。
 * @return 成功为true，失败为false且不修改data。
 */
bool ModeControlWire_Encode(const ModeControlMessage *message, uint8_t *data, uint16_t length);

/**
 * @brief 验证并解码完整MDP1，包括操作、模式、角色及标识的组合。
 * @param data 非NULL只读字节缓冲，仅本次借用。
 * @param length 报文长度，单位字节，必须等于MODE_CONTROL_WIRE_BYTES。
 * @param out 非NULL输出，由调用方持有。
 * @return 成功为true，失败为false且不修改out。
 */
bool ModeControlWire_Decode(const uint8_t *data, uint16_t length, ModeControlMessage *out);

#endif
