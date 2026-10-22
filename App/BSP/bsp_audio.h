#ifndef SBCG_BSP_AUDIO_H
#define SBCG_BSP_AUDIO_H
#include <stdbool.h>
#include <stdint.h>

#define BSP_AUDIO_DMA_ELEMENTS 32768U
#define BSP_AUDIO_HALF_ELEMENTS (BSP_AUDIO_DMA_ELEMENTS / 2U)
#define BSP_AUDIO_HALF_FRAMES (BSP_AUDIO_HALF_ELEMENTS / 2U)
#define BSP_AUDIO_NOMINAL_HZ 48000U

typedef struct
{
    bool running;
    uint32_t half_count, full_count, callback_count;
    uint32_t processed_blocks, dropped_blocks, error_count;
    uint32_t hal_error, dma_error, first_callback_ms, last_callback_ms;
    uint32_t start_error, codec_status, codec_hal_error, derived_milli_hz;
    uint16_t codec_register, codec_writes;
} BSP_AudioSnapshot;

typedef struct
{
    const volatile int16_t *samples;
    uint32_t generation, completed_ms;
    uint8_t half;
} BSP_AudioBlock;

/* Main-loop only. Uses already-generated SAI2 and DMA; no peripheral init.
   Start errors: 1=not ready/owned, 2=codec, 3=DMA start, 4=clock, 5=stop. */
bool BSP_Audio_Start(void);
bool BSP_Audio_Stop(void);
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *output);
/* Single foreground consumer. Every successful Begin requires one End.
   End rejects DMA-overwritten data, even when its boundary IRQ is pending. */
bool BSP_Audio_BeginBlock(BSP_AudioBlock *block);
bool BSP_Audio_EndBlock(const BSP_AudioBlock *block, bool processed);
#endif
