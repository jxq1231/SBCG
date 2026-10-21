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

/* Main-loop only. Uses CubeMX's I2C3 without reinitializing the shared bus.
   HAL has internal error/BUSY timeouts beyond the 2 ms transfer parameter. */
BSP_AudioCodecStatus BSP_AudioCodec_ReadId(BSP_AudioCodecResult *result);

#endif
