#include "bsp_audio_codec.h"
#include "main.h"
#include <stddef.h>

extern I2C_HandleTypeDef hi2c3;

#define CODEC_I2C_HANDLE hi2c3
#define CODEC_ID_REGISTER 0x0000U
#define CODEC_READ_TIMEOUT_MS 2U

BSP_AudioCodecStatus BSP_AudioCodec_ReadId(BSP_AudioCodecResult *result)
{
    if (result == NULL) { return BSP_CODEC_INVALID_ARG; }
    *result = (BSP_AudioCodecResult){0};

    /* No blocking wait or bus recovery when another client owns the bus.
       Do not copy a previous client's ErrorCode for a transaction not started. */
    if (HAL_I2C_GetState(&CODEC_I2C_HANDLE) != HAL_I2C_STATE_READY ||
        __HAL_I2C_GET_FLAG(&CODEC_I2C_HANDLE, I2C_FLAG_BUSY) == SET)
    {
        return BSP_CODEC_BUSY;
    }

    uint8_t bytes[2] = {0};
    const uint32_t start_ms = HAL_GetTick();
    /* WM8994 protocol per ST CubeF7 V1.17.4 BSP: 16-bit register address,
       16-bit big-endian data. Register 0 is also reset-on-write: NEVER write it. */
    const HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&CODEC_I2C_HANDLE,
        (uint16_t)(BSP_AUDIO_CODEC_ADDRESS_7BIT << 1U), CODEC_ID_REGISTER,
        I2C_MEMADD_SIZE_16BIT, bytes, sizeof bytes, CODEC_READ_TIMEOUT_MS);
    result->read_ms = (uint32_t)(HAL_GetTick() - start_ms);
    if (status == HAL_BUSY) { return BSP_CODEC_BUSY; }
    result->hal_error = HAL_I2C_GetError(&CODEC_I2C_HANDLE);
    if (status != HAL_OK) { return BSP_CODEC_IO_ERROR; }

    result->id = (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
    result->id_valid = true;
    return result->id == BSP_AUDIO_CODEC_EXPECTED_ID ? BSP_CODEC_OK : BSP_CODEC_ID_MISMATCH;
}
