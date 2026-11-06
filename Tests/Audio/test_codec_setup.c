/**
 * @file        test_codec_setup.c
 * @brief       验证 WM8994 MIC2 启动序列与诊断增益操作。
 * @details     通过主机 I2C 桩验证寄存器地址、字节序、成对更新及失败回退。
 * @note        本测试只模拟 I2C 事务，增益生效与 PCM 幅度由实机另行验证。
 */
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
static uint8_t gain_left, gain_right;
static bool gain_test, bad_gain_readback;
static unsigned gain_read_fail_at;

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
/**
 * @brief   模拟芯片标识或增益寄存器读回，并校验传输参数。
 * @param   handle 测试用 I2C3 句柄。
 * @param   address 带读写位占位的编解码器 I2C 地址。
 * @param   reg 目标 WM8994 寄存器地址。
 * @param   width 寄存器地址宽度标记。
 * @param   bytes 调用者提供的读缓冲，至少可写 count 字节。
 * @param   count 读取字节数。
 * @param   timeout_ms 调用方传入的超时毫秒数。
 * @return  当前用例配置的 HAL 状态。
 */
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *handle, uint16_t address,
    uint16_t reg, uint16_t width, uint8_t *bytes, uint16_t count, uint32_t timeout_ms)
{
    CHECK(handle == &hi2c3 && address == 0x34U);
    CHECK(width == I2C_MEMADD_SIZE_16BIT && count == 2U && timeout_ms == 2U);
    if (gain_test)
    {
        CHECK(reg == 0x0404U || reg == 0x0405U);
        ++read_calls;
        if (read_calls == gain_read_fail_at) { return HAL_ERROR; }
        bytes[0] = 1U; /* The test also requires masking off VU. */
        bytes[1] = reg == 0x0404U ? gain_left : gain_right;
        if (bad_gain_readback && read_calls == 4U) { bytes[1] ^= 1U; }
        return HAL_OK;
    }
    CHECK(reg == 0U);
    CHECK(read_calls == 0U && write_calls == 0U && delay_calls == 0U);
    ++read_calls;
    if (read_bytes > 0U) { bytes[0] = (uint8_t)(chip_id >> 8U); }
    if (read_bytes > 1U) { bytes[1] = (uint8_t)chip_id; }
    hi2c3.error = read_status == HAL_OK ? 0U : injected_error;
    if (busy_after_read) { hi2c3.state = HAL_I2C_STATE_BUSY_RX; }
    return read_status;
}
/**
 * @brief   校验并记录配置或增益写入，按测试参数注入失败。
 * @param   handle 测试用 I2C3 句柄。
 * @param   address 带读写位占位的编解码器 I2C 地址。
 * @param   reg 目标 WM8994 寄存器地址。
 * @param   width 寄存器地址宽度标记。
 * @param   bytes 调用者提供的寄存器数据，只读，含 count 字节。
 * @param   count 写入字节数。
 * @param   timeout_ms 调用方传入的超时毫秒数。
 * @return  注入的 HAL 状态，未注入时返回 HAL_OK。
 */
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *handle, uint16_t address,
    uint16_t reg, uint16_t width, uint8_t *bytes, uint16_t count, uint32_t timeout_ms)
{
    CHECK(handle == &hi2c3 && address == 0x34U);
    CHECK(width == I2C_MEMADD_SIZE_16BIT && count == 2U && timeout_ms == 2U);
    if (gain_test)
    {
        CHECK(write_calls < EXPECTED_WRITES);
        CHECK(read_calls >= 2U);
        const bool left = write_calls % 2U == 0U;
        CHECK(reg == (left ? 0x0404U : 0x0405U));
        CHECK(bytes[0] == (left ? 0U : 1U));
        written[write_calls][0] = reg;
        written[write_calls][1] = (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
        ++write_calls;
        if (write_calls == fail_at) { return write_status; }
        if (left) { gain_left = bytes[1]; }
        else { gain_right = bytes[1]; }
        return HAL_OK;
    }
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
/** @brief 将主机 I2C 桩和事务计数恢复到每个用例的初始状态。 */
static void Reset(void)
{
    hi2c3 = (I2C_HandleTypeDef){HAL_I2C_STATE_READY, 0U, 0U};
    tick_ms = 0U; read_calls = 0U; write_calls = 0U; delay_calls = 0U;
    chip_id = 0x8994U; read_status = HAL_OK; write_status = HAL_ERROR;
    fail_at = 0U; injected_error = HAL_I2C_ERROR_AF; read_bytes = 2U;
    busy_after_read = 0; busy_after_delay = 0;
    gain_test = false; bad_gain_readback = false; gain_read_fail_at = 0U;
    gain_left = 0xEFU; gain_right = 0xEFU;
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
/** @brief 验证左右读回、同步更新、恢复和异常路径。 */
static void TestMic2Gain(void)
{
    BSP_AudioCodecMic2Gain gain;
    Reset(); gain_test = true;
    CHECK(BSP_AudioCodec_DiagnoseMic2Gain(BSP_CODEC_GAIN_READ, &gain) == BSP_CODEC_OK);
    CHECK(gain.left == 0xEFU && gain.right == 0xEFU && write_calls == 0U);
    CHECK(BSP_AudioCodec_DiagnoseMic2Gain(BSP_CODEC_GAIN_LOW, &gain) == BSP_CODEC_OK);
    CHECK(gain.left == 0xC0U && gain.right == 0xC0U && write_calls == 2U);
    CHECK(written[0][1] == 0x00C0U && written[1][1] == 0x01C0U);
    CHECK(BSP_AudioCodec_DiagnoseMic2Gain(BSP_CODEC_GAIN_DEFAULT, &gain) == BSP_CODEC_OK);
    CHECK(gain.left == 0xEFU && gain.right == 0xEFU && write_calls == 4U);

    Reset(); gain_test = true; fail_at = 2U; gain_right = 0xE0U;
    CHECK(BSP_AudioCodec_DiagnoseMic2Gain(BSP_CODEC_GAIN_LOW, &gain) == BSP_CODEC_IO_ERROR);
    CHECK(write_calls == 4U && gain_left == 0xEFU && gain_right == 0xE0U);

    Reset(); gain_test = true; bad_gain_readback = true;
    CHECK(BSP_AudioCodec_DiagnoseMic2Gain(BSP_CODEC_GAIN_LOW, &gain) == BSP_CODEC_IO_ERROR);
    CHECK(write_calls == 4U && gain_left == 0xEFU && gain_right == 0xEFU);

    Reset(); gain_test = true; gain_read_fail_at = 2U;
    CHECK(BSP_AudioCodec_DiagnoseMic2Gain(BSP_CODEC_GAIN_LOW, &gain) == BSP_CODEC_IO_ERROR);
    CHECK(write_calls == 0U);

    Reset(); gain_test = true; hi2c3.flags = I2C_FLAG_BUSY;
    CHECK(BSP_AudioCodec_DiagnoseMic2Gain(BSP_CODEC_GAIN_LOW, &gain) == BSP_CODEC_BUSY);
    CHECK(read_calls == 0U && write_calls == 0U);
    puts("PASS: MIC2 gain reads, stereo latch, restore, and failed-change rollback");
}

/** @brief 顺序运行 MIC2 主机用例，全部通过时返回零。 */
int main(void)
{
    TestSuccess();
    TestUnavailable();
    TestIdGate();
    TestEveryWriteFailure();
    TestMic2Gain();
    return 0;
}
