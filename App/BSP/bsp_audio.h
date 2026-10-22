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
    uint64_t observed_frames;
    uint32_t session, index_faults;
} BSP_AudioSnapshot;

typedef struct
{
    const volatile int16_t *samples;
    uint32_t generation, completed_ms;
    uint8_t half;
    uint32_t session, frame_count;
    uint64_t first_frame, gap_frames;
    bool index_valid, contiguous;
} BSP_AudioBlock;

typedef struct
{
    uint64_t complete_frames;
    uint32_t session, boundary_generation;
} BSP_AudioCursor;

/* Foreground only; a read-only NDTR-based stereo transfer-group count, not
   proof of SRAM write visibility or an acoustic timestamp. PCM ownership
   still requires Begin/End. Rejects boundary/error/reload ambiguity.
   Clears output on failure; does not consume blocks or clear IRQ flags. */
bool BSP_Audio_ReadCursor(BSP_AudioCursor *output);

/* Main-loop only. Uses already-generated SAI2 and DMA; no peripheral init.
   Start errors: 1=not ready/owned, 2=codec, 3=DMA start, 4=clock, 5=stop,
   6=boot-local session identity exhausted. */
bool BSP_Audio_Start(void);
bool BSP_Audio_Stop(void);
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *output);
/* Single foreground consumer. Every successful Begin requires one End.
   End rejects DMA-overwritten data, even when its boundary IRQ is pending.
   Position is usable only after End succeeds AND index_valid is true.
   gap_frames counts observed frames since the preceding successful End.
   session is boot-local; a new BOOT delimits the consumer history. */
bool BSP_Audio_BeginBlock(BSP_AudioBlock *block);
bool BSP_Audio_EndBlock(const BSP_AudioBlock *block, bool processed);
#endif
