#include "BSP/bsp_audio_codec.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); exit(1); \
} } while (0)

/* Independent oracle: ST CubeF7 V1.17.4 wm8994_Reset(), then Init(MIC2,
   volume=100, frequency=48000), with output disabled. Codec timeslot 1 L/R
   maps to SAI's active slots 1 and 3 in a four-slot frame. */
static const uint16_t expected[][2] = {
    {0x0000U, 0x0000U},
    {0x0102U, 0x0003U}, {0x0817U, 0x0000U}, {0x0102U, 0x0000U},
    {0x0039U, 0x006CU}, {0x0001U, 0x0013U},
    {0x0004U, 0x0C30U}, {0x0450U, 0x00DBU}, {0x0002U, 0x6000U},
    {0x0608U, 0x0002U}, {0x0609U, 0x0002U}, {0x0700U, 0x000EU},
    {0x0210U, 0x0083U}, {0x0300U, 0x4010U}, {0x0302U, 0x0000U},
    {0x0208U, 0x000AU}, {0x0200U, 0x0001U},
    {0x0001U, 0x0013U}, {0x0620U, 0x0002U}, {0x0411U, 0x3800U},
    {0x0400U, 0x01EFU}, {0x0401U, 0x01EFU},
    {0x0404U, 0x01EFU}, {0x0405U, 0x01EFU}
};
#define EXPECTED_WRITES (sizeof expected / sizeof expected[0])

I2C_HandleTypeDef hi2c3;
static uint32_t tick_ms;
static unsigned read_calls, write_calls, delay_calls;
static uint16_t chip_id;
static HAL_StatusTypeDef read_status, write_status;
static unsigned fail_at;
static uint32_t injected_error;
static unsigned read_bytes;
static int busy_after_read, busy_after_delay;
static uint16_t written[EXPECTED_WRITES][2];

HAL_I2C_StateTypeDef HAL_I2C_GetState(const I2C_HandleTypeDef *handle)
{
    CHECK(handle == &hi2c3);
    return handle->state;
}
uint32_t HAL_I2C_GetError(const I2C_HandleTypeDef *handle)
{
    CHECK(handle == &hi2c3);
    return handle->error;
}
uint32_t HAL_GetTick(void) { return tick_ms; }
void HAL_Delay(uint32_t delay_ms)
{
    CHECK(delay_ms == 50U && write_calls == 6U && delay_calls == 0U);
    ++delay_calls;
    tick_ms += delay_ms;
    if (busy_after_delay) { hi2c3.flags = I2C_FLAG_BUSY; }
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *handle, uint16_t address,
    uint16_t reg, uint16_t width, uint8_t *bytes, uint16_t count, uint32_t timeout_ms)
{
    CHECK(handle == &hi2c3 && address == 0x34U && reg == 0U);
    CHECK(width == I2C_MEMADD_SIZE_16BIT && count == 2U && timeout_ms == 2U);
    CHECK(read_calls == 0U && write_calls == 0U && delay_calls == 0U);
    ++read_calls;
    if (read_bytes > 0U) { bytes[0] = (uint8_t)(chip_id >> 8U); }
    if (read_bytes > 1U) { bytes[1] = (uint8_t)chip_id; }
    hi2c3.error = read_status == HAL_OK ? 0U : injected_error;
    if (busy_after_read) { hi2c3.state = HAL_I2C_STATE_BUSY_RX; }
    return read_status;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *handle, uint16_t address,
    uint16_t reg, uint16_t width, uint8_t *bytes, uint16_t count, uint32_t timeout_ms)
{
    CHECK(handle == &hi2c3 && address == 0x34U);
    CHECK(width == I2C_MEMADD_SIZE_16BIT && count == 2U && timeout_ms == 2U);
    CHECK(read_calls == 1U && write_calls < EXPECTED_WRITES);
    CHECK(reg == expected[write_calls][0]);
    CHECK(bytes[0] == (uint8_t)(expected[write_calls][1] >> 8U));
    CHECK(bytes[1] == (uint8_t)expected[write_calls][1]);
    CHECK(delay_calls == (write_calls >= 6U ? 1U : 0U));
    written[write_calls][0] = reg;
    written[write_calls][1] = (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
    ++write_calls;
    hi2c3.error = write_calls == fail_at ? injected_error : 0U;
    return write_calls == fail_at ? write_status : HAL_OK;
}
static void Reset(void)
{
    hi2c3 = (I2C_HandleTypeDef){HAL_I2C_STATE_READY, 0U, 0U};
    tick_ms = 0U; read_calls = 0U; write_calls = 0U; delay_calls = 0U;
    chip_id = 0x8994U; read_status = HAL_OK; write_status = HAL_ERROR;
    fail_at = 0U; injected_error = HAL_I2C_ERROR_AF; read_bytes = 2U;
    busy_after_read = 0; busy_after_delay = 0;
}
static void CheckNoWrites(const BSP_AudioCodecSetupResult *result)
{
    CHECK(write_calls == 0U && delay_calls == 0U && result->writes_ok == 0U);
}
static void TestSuccess(void)
{
    BSP_AudioCodecSetupResult result;
    Reset();
    CHECK(BSP_AudioCodec_ConfigureMic2(&result) == BSP_CODEC_OK);
    CHECK(result.hal_error == 0U && result.failed_register == UINT16_MAX);
    CHECK(result.writes_ok == EXPECTED_WRITES && write_calls == EXPECTED_WRITES);
    CHECK(delay_calls == 1U && tick_ms == 50U);
    /* Decisive routing/format: ADC2+DMIC2 only; no analog input path;
       L/R -> timeslot 1, 48k/256fs, 16-bit I2S, slave/MCLK1.
       0x3800 retains the official ADC2 HPF/unmuted control value;
       0x01EF is VU=1 and VOLUME_IN_CONVERT(100)=239 on both ADC pairs. */
    CHECK(written[6][1] == 0x0C30U && written[8][1] == 0x6000U);
    CHECK(written[9][1] == 2U && written[10][1] == 2U);
    CHECK(written[12][1] == 0x0083U && written[13][1] == 0x4010U);
    CHECK(written[14][1] == 0U && written[16][1] == 1U);
    CHECK((written[19][1] & 0xC000U) == 0U);
    CHECK(written[22][1] == 0x01EFU && written[23][1] == 0x01EFU);
    puts("PASS: exact 16-bit big-endian MIC2/48k/16-bit/input-volume sequence and bounded VMID delay");
}
static void TestUnavailable(void)
{
    BSP_AudioCodecSetupResult result = {123U, 123U, 123U};
    Reset(); CHECK(BSP_AudioCodec_ConfigureMic2(NULL) == BSP_CODEC_INVALID_ARG);
    CHECK(read_calls == 0U && write_calls == 0U);
    hi2c3.state = HAL_I2C_STATE_BUSY_RX; hi2c3.error = 999U;
    CHECK(BSP_AudioCodec_ConfigureMic2(&result) == BSP_CODEC_BUSY);
    CHECK(read_calls == 0U && result.hal_error == 0U && result.failed_register == UINT16_MAX);
    CheckNoWrites(&result);
    hi2c3.state = HAL_I2C_STATE_RESET;
    CHECK(BSP_AudioCodec_ConfigureMic2(&result) == BSP_CODEC_BUSY && read_calls == 0U);
    CheckNoWrites(&result);
    hi2c3.state = HAL_I2C_STATE_READY; hi2c3.flags = I2C_FLAG_BUSY;
    CHECK(BSP_AudioCodec_ConfigureMic2(&result) == BSP_CODEC_BUSY && read_calls == 0U);
    CheckNoWrites(&result);
    Reset(); busy_after_read = 1;
    CHECK(BSP_AudioCodec_ConfigureMic2(&result) == BSP_CODEC_BUSY);
    CHECK(result.hal_error == 0U && result.failed_register == 0U);
    CheckNoWrites(&result);
    Reset(); busy_after_delay = 1;
    CHECK(BSP_AudioCodec_ConfigureMic2(&result) == BSP_CODEC_BUSY);
    CHECK(result.writes_ok == 6U && write_calls == 6U && result.failed_register == 4U);
    CHECK(result.hal_error == 0U && delay_calls == 1U);
    puts("PASS: preflight and mid-setup busy states never issue unavailable transactions or leak stale errors");
}
static void TestIdGate(void)
{
    BSP_AudioCodecSetupResult result;
    Reset(); chip_id = 0x9489U;
    CHECK(BSP_AudioCodec_ConfigureMic2(&result) == BSP_CODEC_ID_MISMATCH);
    CHECK(read_calls == 1U && result.hal_error == 0U && result.failed_register == 0U);
    CheckNoWrites(&result);
    const HAL_StatusTypeDef statuses[] = {HAL_ERROR, HAL_TIMEOUT, HAL_BUSY};
    for (unsigned i = 0; i < sizeof statuses / sizeof statuses[0]; ++i)
    {
        for (unsigned partial = 0U; partial <= 2U; ++partial)
        {
            Reset(); read_status = statuses[i]; read_bytes = partial;
            injected_error = i == 1U ? HAL_I2C_ERROR_TIMEOUT : HAL_I2C_ERROR_AF;
            CHECK(BSP_AudioCodec_ConfigureMic2(&result) ==
                (i == 2U ? BSP_CODEC_BUSY : BSP_CODEC_IO_ERROR));
            CHECK(result.failed_register == 0U && read_calls == 1U);
            CHECK(result.hal_error == (i == 2U ? 0U : injected_error));
            CheckNoWrites(&result);
        }
    }
    puts("PASS: ID mismatch and failed/partial ID reads cannot reset or configure codec");
}
static void TestEveryWriteFailure(void)
{
    const HAL_StatusTypeDef statuses[] = {HAL_ERROR, HAL_TIMEOUT, HAL_BUSY};
    for (unsigned s = 0; s < sizeof statuses / sizeof statuses[0]; ++s)
    {
        for (unsigned position = 1U; position <= EXPECTED_WRITES; ++position)
        {
            BSP_AudioCodecSetupResult result = {999U, 999U, 999U};
            Reset(); fail_at = position; write_status = statuses[s];
            injected_error = s == 1U ? HAL_I2C_ERROR_TIMEOUT : HAL_I2C_ERROR_AF;
            CHECK(BSP_AudioCodec_ConfigureMic2(&result) ==
                (s == 2U ? BSP_CODEC_BUSY : BSP_CODEC_IO_ERROR));
            CHECK(write_calls == position && result.writes_ok == position - 1U);
            CHECK(result.failed_register == expected[position - 1U][0]);
            CHECK(result.hal_error == injected_error);
            CHECK(delay_calls == (position > 6U ? 1U : 0U));
        }
    }
    puts("PASS: NACK/timeout/HAL_BUSY at each of 24 writes stops on first error with exact diagnostics");
}
int main(void)
{
    TestSuccess();
    TestUnavailable();
    TestIdGate();
    TestEveryWriteFailure();
    return 0;
}
