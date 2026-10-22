#ifndef SBCG_BSP_AUDIO_CODEC_H
#define SBCG_BSP_AUDIO_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_AUDIO_CODEC_ADDRESS_7BIT 0x1AU
#define BSP_AUDIO_CODEC_EXPECTED_ID 0x8994U

typedef enum
{
    BSP_CODEC_OK,
    BSP_CODEC_BUSY,
    BSP_CODEC_IO_ERROR,
    BSP_CODEC_ID_MISMATCH,
    BSP_CODEC_INVALID_ARG
} BSP_AudioCodecStatus;

typedef struct
{
    uint16_t id;
    bool id_valid;
    uint32_t hal_error;
    uint32_t read_ms;
} BSP_AudioCodecResult;

typedef struct
{
    uint32_t hal_error;
    uint16_t failed_register;
    uint16_t writes_ok;
} BSP_AudioCodecSetupResult;

/* Main-loop only. Uses CubeMX's I2C3 without reinitializing the shared bus.
   HAL has internal error/BUSY timeouts beyond the 2 ms transfer parameter. */
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
BSP_AudioCodecStatus BSP_AudioCodec_ConfigureMic2(BSP_AudioCodecSetupResult *result);

#endif
