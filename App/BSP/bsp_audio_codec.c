#include "bsp_audio_codec.h"
#include "main.h"
#include <stddef.h>

extern I2C_HandleTypeDef hi2c3;

#define CODEC_I2C_HANDLE hi2c3
#define CODEC_ID_REGISTER 0x0000U
#define CODEC_READ_TIMEOUT_MS 2U
#define CODEC_WRITE_TIMEOUT_MS 2U

typedef struct
{
    uint16_t reg;
    uint16_t value;
    uint16_t delay_after_ms;
} CodecSetupWrite;

/* Register facts/sequence checked against STM32Cube_FW_F7_V1.17.4:
   Drivers/BSP/Components/wm8994/wm8994.c wm8994_Reset(), wm8994_Init()
   INPUT_DEVICE_DIGITAL_MICROPHONE_2 + AUDIO_FREQUENCY_48K + Volume=100,
   output_device=0, and wm8994_SetVolume() input-only branch.
   The board BSP confirms MIC2 uses SAI four-slot-frame slots 1 and 3.
   Keep this a fixed input-only recipe: no imported AUDIO_IO_Init(), MCU
   clock/GPIO/SAI reconfiguration, output power-up, or error-blind writes. */
static const CodecSetupWrite mic2_setup[] = {
    {0x0000U, 0x0000U, 0U},  /* Explicit codec software reset. */
    {0x0102U, 0x0003U, 0U},  /* ST errata workaround, in this order. */
    {0x0817U, 0x0000U, 0U},
    {0x0102U, 0x0000U, 0U},
    {0x0039U, 0x006CU, 0U},  /* VMID fast soft-start and startup bias. */
    {0x0001U, 0x0013U, 50U}, /* Bias/VMID; settle before input path. */
    {0x0004U, 0x0C30U, 0U},  /* AIF1ADC2 L/R and DMICDAT2 L/R. */
    {0x0450U, 0x00DBU, 0U},  /* ADC2 DRC and signal detection. */
    {0x0002U, 0x6000U, 0U},  /* Analog inputs off; thermal protection. */
    {0x0608U, 0x0002U, 0U},  /* DMIC2 L -> AIF1 timeslot 1 L. */
    {0x0609U, 0x0002U, 0U},  /* DMIC2 R -> AIF1 timeslot 1 R. */
    {0x0700U, 0x000EU, 0U},  /* Codec GPIO1 DRC2 detect, no MCU IRQ. */
    {0x0210U, 0x0083U, 0U},  /* 48 kHz, MCLK/fs ratio 256. */
    {0x0300U, 0x4010U, 0U},  /* 16-bit I2S. */
    {0x0302U, 0x0000U, 0U},  /* Codec is slave to SAI clocks. */
    {0x0208U, 0x000AU, 0U},  /* AIF1 DSP and core clocks. */
    {0x0200U, 0x0001U, 0U},  /* Enable AIF1 from MCLK1. */
    {0x0001U, 0x0013U, 0U},  /* Microphone bias and VMID. */
    {0x0620U, 0x0002U, 0U},  /* ADC oversampling. */
    {0x0411U, 0x3800U, 0U},  /* Official ADC2 HPF controls; unmuted. */
    {0x0400U, 0x01EFU, 0U},  /* VU=1, VOLUME_IN_CONVERT(100)=239. */
    {0x0401U, 0x01EFU, 0U},
    {0x0404U, 0x01EFU, 0U},
    {0x0405U, 0x01EFU, 0U}
};

static bool CodecBusReady(void)
{
    return HAL_I2C_GetState(&CODEC_I2C_HANDLE) == HAL_I2C_STATE_READY &&
        __HAL_I2C_GET_FLAG(&CODEC_I2C_HANDLE, I2C_FLAG_BUSY) != SET;
}

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
       16-bit big-endian data. Register 0 is reset-on-write: ReadId never writes. */
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

BSP_AudioCodecStatus BSP_AudioCodec_ConfigureMic2(BSP_AudioCodecSetupResult *result)
{
    if (result == NULL) { return BSP_CODEC_INVALID_ARG; }
    *result = (BSP_AudioCodecSetupResult){0U, UINT16_MAX, 0U};
    if (!CodecBusReady()) { return BSP_CODEC_BUSY; }

    /* Verify before resetting: no write at all to an unidentified device. */
    BSP_AudioCodecResult identity;
    BSP_AudioCodecStatus status = BSP_AudioCodec_ReadId(&identity);
    if (status != BSP_CODEC_OK)
    {
        result->hal_error = identity.hal_error;
        result->failed_register = CODEC_ID_REGISTER;
        return status;
    }

    for (size_t i = 0U; i < sizeof mic2_setup / sizeof mic2_setup[0]; ++i)
    {
        const CodecSetupWrite *write = &mic2_setup[i];
        result->failed_register = write->reg;
        /* The startup caller owns I2C3; still fail promptly if unavailable.
           No recovery/reinitialization or repeated polling on the shared bus. */
        if (!CodecBusReady()) { return BSP_CODEC_BUSY; }
        uint8_t bytes[2] = {(uint8_t)(write->value >> 8U), (uint8_t)write->value};
        const HAL_StatusTypeDef hal_status = HAL_I2C_Mem_Write(&CODEC_I2C_HANDLE,
            (uint16_t)(BSP_AUDIO_CODEC_ADDRESS_7BIT << 1U), write->reg,
            I2C_MEMADD_SIZE_16BIT, bytes, sizeof bytes, CODEC_WRITE_TIMEOUT_MS);
        if (hal_status != HAL_OK)
        {
            result->hal_error = HAL_I2C_GetError(&CODEC_I2C_HANDLE);
            return hal_status == HAL_BUSY ? BSP_CODEC_BUSY : BSP_CODEC_IO_ERROR;
        }
        ++result->writes_ok;
        if (write->delay_after_ms != 0U) { HAL_Delay(write->delay_after_ms); }
    }
    result->failed_register = UINT16_MAX;
    return BSP_CODEC_OK;
}
