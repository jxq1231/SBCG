/**
 * @file        bsp_audio_codec.h
 * @brief       WM8994 身份读取和 MIC2 输入通路的同步 I2C 访问。
 * @details     声明 I2C 地址/预期标识、操作状态、身份读取结果和配置结果；read_ms 为耗时毫秒，寄存器/标识为 16
 *          位，hal_error 保留 HAL 原始错误位。
 * @note        主循环/启动上下文专用；采集配置前需 SAI 主时钟，调用者串行管理共享 I2C3，不重初始化 MCU 外设，
 *          不进行总线恢复。
 */

#ifndef SBCG_BSP_AUDIO_CODEC_H
#define SBCG_BSP_AUDIO_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_AUDIO_CODEC_ADDRESS_7BIT 0x1AU
#define BSP_AUDIO_CODEC_EXPECTED_ID 0x8994U

/** @brief 编解码器操作结果：完成、总线忙、传输错误、标识不符或空输出参数。 */
typedef enum
{
    BSP_CODEC_OK,
    BSP_CODEC_BUSY,
    BSP_CODEC_IO_ERROR,
    BSP_CODEC_ID_MISMATCH,
    BSP_CODEC_INVALID_ARG
} BSP_AudioCodecStatus;

/**
 * @brief 一次身份读事务的结果；id_valid 表示读成功，并不保证 id 等于预期值。
 * @note read_ms 为事务的 HAL 毫秒耗时；HAL_BUSY 保持零 hal_error，不能据此认为总线没有问题。
 */
typedef struct
{
    uint16_t id;
    bool id_valid;
    uint32_t hal_error;
    uint32_t read_ms;
} BSP_AudioCodecResult;

/**
 * @brief 配置阶段的诊断快照：HAL 错误、失败寄存器与 HAL_OK 写入次数。
 * @note failed_register 为 UINT16_MAX 表示全部完成或尚未开始事务，必须结合返回状态判断。
 */
typedef struct
{
    uint32_t hal_error;
    uint16_t failed_register;
    uint16_t writes_ok;
} BSP_AudioCodecSetupResult;

/* Main-loop only. Uses CubeMX's I2C3 without reinitializing the shared bus.
   HAL has internal error/BUSY timeouts beyond the 2 ms transfer parameter. */
/**
 * @brief   同步读取 WM8994 寄存器零并按高字节在前还原芯片标识。
 * @param   result 调用者拥有的输出；NULL 返回 INVALID_ARG，非空时先清零。成功传输后 id_valid 为真，即使标识不
 *          匹配。read_ms 为 HAL tick 差的毫秒值。
 * @return  OK 表示 ID 为 0x8994；其余区分参数、总线忙、读失败和 ID 不符。
 * @note    仅启动/主循环上下文；使用已初始化 I2C3、7 位地址 0x1A，不写复位寄存器。单次传输参数超时 2 ms，HAL
 *          内部等待可增加耗时；BUSY 路径不复制其它客户的旧错误。
 */
BSP_AudioCodecStatus BSP_AudioCodec_ReadId(BSP_AudioCodecResult *result);

/* Startup/main-loop only, never ISR. The caller must already supply the SAI
   master clocks and must serialize access to I2C3 for this synchronous call.
   Configures MIC2 -> AIF1 timeslot 1 L/R (SAI slots 1/3), 48 kHz, 16-bit I2S,
   input volume 100, no output. Resets codec registers, not MCU peripherals.
   Includes one 50 ms VMID startup delay; each transfer uses a 2 ms HAL timeout
   (HAL internal BUSY/error waits can add time). No retry or bus recovery.
   writes_ok counts only HAL_OK writes. failed_register is UINT16_MAX on
   success or when initial bus preflight prevents all transactions; register
   0 identifies failed ID verification/read or reset write (writes_ok=0).
   hal_error is the raw HAL error after a failed transaction; preflight BUSY
   leaves it zero. ID-read HAL_BUSY preserves ReadId's zero-error behavior.
   Failure can leave partial codec configuration; caller must not start DMA. */
/**
 * @brief   验证芯片身份后按固定寄存器序列配置 MIC2 采集通路。
 * @param   result 调用者拥有的输出；NULL 返回 INVALID_ARG。writes_ok 只计 HAL_OK 写入；failed_register 成功或
 *          初始 BUSY 为 UINT16_MAX；其它失败指示当前寄存器，身份读取失败记零。
 * @return  OK 表示全部配置写入成功；其它状态保留已完成写入数量和可取得的 HAL 错误。
 * @note    仅启动/前台且不得在 ISR 调用；须先提供 SAI 主时钟并串行独占 I2C3。设置 48 kHz、16 位、SAI 槽 1/3，
 *          包含一次 50 ms 延时；会复位编解码器寄存器，失败可能留下部分配置，调用方不能继续启动 DMA。
 */
BSP_AudioCodecStatus BSP_AudioCodec_ConfigureMic2(BSP_AudioCodecSetupResult *result);

#endif
