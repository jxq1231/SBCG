#include "BSP/bsp_audio_codec.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
I2C_HandleTypeDef hi2c3;
static unsigned calls;
static uint32_t tick_ms;
static HAL_StatusTypeDef hal_result;
static uint8_t response[2];
static unsigned bytes_to_write;
HAL_I2C_StateTypeDef HAL_I2C_GetState(const I2C_HandleTypeDef *h) { return h->state; }
uint32_t HAL_I2C_GetError(const I2C_HandleTypeDef *h) { return h->error; }
uint32_t HAL_GetTick(void) { return tick_ms; }
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h,uint16_t address,uint16_t reg,
    uint16_t width,uint8_t *bytes,uint16_t count,uint32_t timeout_ms)
{
    CHECK(h==&hi2c3 && address==0x34U && reg==0U);
    CHECK(width==I2C_MEMADD_SIZE_16BIT && count==2U && timeout_ms==2U);
    ++calls; tick_ms+=3U;
    for (unsigned i=0; i<bytes_to_write; ++i) { bytes[i]=response[i]; }
    return hal_result;
}
static void Reset(void)
{
    hi2c3=(I2C_HandleTypeDef){HAL_I2C_STATE_READY,0U,0U};
    tick_ms=0; calls=0; hal_result=HAL_OK; response[0]=0x89; response[1]=0x94; bytes_to_write=2;
}
int main(void)
{
    BSP_AudioCodecResult r;
    Reset(); CHECK(BSP_AudioCodec_ReadId(NULL)==BSP_CODEC_INVALID_ARG && calls==0);
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_OK);
    CHECK(r.id_valid && r.id==0x8994U && r.hal_error==0U && r.read_ms==3U && calls==1);
    response[0]=0x94; response[1]=0x89;
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_ID_MISMATCH && r.id_valid && r.id==0x9489U);
    response[0]=response[1]=0;
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_ID_MISMATCH && r.id_valid && r.id==0);
    puts("PASS: exact WM8994 read transaction, big-endian ID and mismatch detection");
    Reset(); hi2c3.state=HAL_I2C_STATE_BUSY_RX; hi2c3.error=123;
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_BUSY && !r.id_valid && calls==0 && r.hal_error==0);
    hi2c3.state=HAL_I2C_STATE_RESET;
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_BUSY && !r.id_valid && calls==0);
    hi2c3.state=HAL_I2C_STATE_READY; hi2c3.flags=I2C_FLAG_BUSY;
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_BUSY && !r.id_valid && calls==0 && r.read_ms==0);
    puts("PASS: unavailable handle or busy bus never starts a transaction or leaks old error bits");
    Reset(); hal_result=HAL_ERROR; hi2c3.error=HAL_I2C_ERROR_AF;
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_IO_ERROR && !r.id_valid && r.id==0);
    CHECK(r.hal_error==HAL_I2C_ERROR_AF && r.read_ms==3);
    bytes_to_write=1;
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_IO_ERROR && !r.id_valid && r.id==0);
    bytes_to_write=0;
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_IO_ERROR && !r.id_valid && r.id==0);
    bytes_to_write=2;
    hal_result=HAL_TIMEOUT; hi2c3.error=HAL_I2C_ERROR_TIMEOUT;
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_IO_ERROR && !r.id_valid && r.hal_error==HAL_I2C_ERROR_TIMEOUT);
    hal_result=HAL_BUSY;
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_BUSY && !r.id_valid && r.hal_error==0);
    hal_result=HAL_OK; hi2c3.error=0; tick_ms=UINT32_MAX-1U;
    CHECK(BSP_AudioCodec_ReadId(&r)==BSP_CODEC_OK && r.read_ms==3);
    puts("PASS: failed/partial reads cannot publish ID; raw I2C errors and tick-wrap duration retained");
    return 0;
}
