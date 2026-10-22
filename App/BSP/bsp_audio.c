#include "bsp_audio.h"
#include "bsp_audio_codec.h"
#include "main.h"
#include <stddef.h>

extern SAI_HandleTypeDef hsai_BlockA2, hsai_BlockB2;
/* DMA-only NOLOAD storage is separate from BSS and precedes the newlib heap.
   Cache is disabled in
   this CubeMX snapshot; do not add unrelated cache maintenance here. */
static volatile int16_t audio_dma_buffer[BSP_AUDIO_DMA_ELEMENTS]
    __attribute__((section(".AudioDmaSection"), aligned(32)));
static volatile BSP_AudioSnapshot state;
static bool dma_owned, clock_owned, claimed;
static volatile bool accepting_callbacks;
static uint32_t consumed_generation, claim_generation;
static uint8_t completed_half, expected_half, claim_half;

static uint32_t Lock(void)
{
    const uint32_t old = __get_PRIMASK();
    __disable_irq(); __DMB(); return old;
}
static void Unlock(uint32_t old) { __DMB(); __set_PRIMASK(old); }

bool BSP_Audio_Stop(void)
{
    /* HAL stop can wait briefly; call only from foreground with IRQs enabled. */
    accepting_callbacks = false;
    state.running = false;
    /* HAL_SAI_DMAStop sets SAI READY even when HAL_DMA_Abort times out.
       A later HAL_OK alone therefore cannot release DMA memory ownership. */
    if (dma_owned)
    {
        const HAL_StatusTypeDef stopped = HAL_SAI_DMAStop(&hsai_BlockB2);
        if (stopped != HAL_OK ||
            (hsai_BlockB2.Instance->CR1 & SAI_xCR1_SAIEN) != 0U ||
            (hsai_BlockB2.hdmarx->Instance->CR & DMA_SxCR_EN) != 0U ||
            HAL_DMA_GetState(hsai_BlockB2.hdmarx) != HAL_DMA_STATE_READY)
        {
            state.start_error = 5U;
            state.hal_error |= HAL_SAI_GetError(&hsai_BlockB2);
            state.dma_error |= HAL_DMA_GetError(hsai_BlockB2.hdmarx);
            return false;
        }
    }
    dma_owned = false; claimed = false;
    if (clock_owned) { __HAL_SAI_DISABLE(&hsai_BlockA2); clock_owned = false; }
    return true;
}

bool BSP_Audio_Start(void)
{
    if (dma_owned || clock_owned ||
        HAL_SAI_GetState(&hsai_BlockA2) != HAL_SAI_STATE_READY ||
        HAL_SAI_GetState(&hsai_BlockB2) != HAL_SAI_STATE_READY ||
        hsai_BlockB2.hdmarx == NULL ||
        HAL_DMA_GetState(hsai_BlockB2.hdmarx) != HAL_DMA_STATE_READY ||
        (hsai_BlockB2.hdmarx->Instance->CR & DMA_SxCR_EN) != 0U)
    { state.start_error = 1U; return false; }
    state = (BSP_AudioSnapshot){0};
    consumed_generation = claim_generation = 0U;
    completed_half = expected_half = claim_half = 0U;
    claimed = false;
    const uint32_t source_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI2);
    const uint32_t divider = hsai_BlockA2.Init.Mckdiv;
    if (source_hz == 0U || divider == 0U)
    { state.start_error = 4U; return false; }
    state.derived_milli_hz = (uint32_t)((uint64_t)source_hz * 1000U /
                                      (2U * divider * 256U));
    for (uint32_t i = 0; i < BSP_AUDIO_DMA_ELEMENTS; ++i) { audio_dma_buffer[i] = 0; }
    __DMB();
    /* Enable already-configured clock master; never initialize it here. */
    __HAL_SAI_ENABLE(&hsai_BlockA2);
    clock_owned = true;
    BSP_AudioCodecSetupResult setup;
    const BSP_AudioCodecStatus codec = BSP_AudioCodec_ConfigureMic2(&setup);
    state.codec_status = (uint32_t)codec;
    state.codec_hal_error = setup.hal_error;
    state.codec_register = setup.failed_register;
    state.codec_writes = setup.writes_ok;
    if (codec != BSP_CODEC_OK)
    { state.start_error = 2U; (void)BSP_Audio_Stop(); return false; }
    state.running = true;
    accepting_callbacks = true;
    /* Own cleanup of partially-started HAL transfers as well as successful ones. */
    dma_owned = true;
    const HAL_StatusTypeDef result = HAL_SAI_Receive_DMA(&hsai_BlockB2,
        (uint8_t *)(uintptr_t)audio_dma_buffer, (uint16_t)BSP_AUDIO_DMA_ELEMENTS);
    if (result != HAL_OK || !state.running)
    {
        state.start_error = 3U;
        state.hal_error |= HAL_SAI_GetError(&hsai_BlockB2);
        (void)BSP_Audio_Stop();
        return false;
    }
    return true;
}

void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *output)
{
    if (output == NULL) { return; }
    const uint32_t old = Lock(); *output = state; Unlock(old);
}

static bool HalfIsInactive(uint8_t half)
{
    const uint32_t remaining = __HAL_DMA_GET_COUNTER(hsai_BlockB2.hdmarx);
    const uint32_t flags = __HAL_DMA_GET_FLAG(hsai_BlockB2.hdmarx,
        __HAL_DMA_GET_HT_FLAG_INDEX(hsai_BlockB2.hdmarx) |
        __HAL_DMA_GET_TC_FLAG_INDEX(hsai_BlockB2.hdmarx));
    /* Pending boundaries invalidate data even before ISR increments generation.
       NDTR=0 is a reload edge, not proof that memory is inactive. */
    if (flags != RESET || remaining == 0U || remaining > BSP_AUDIO_DMA_ELEMENTS)
    { return false; }
    return half == 0U ? remaining <= BSP_AUDIO_HALF_ELEMENTS
                      : remaining > BSP_AUDIO_HALF_ELEMENTS;
}

bool BSP_Audio_BeginBlock(BSP_AudioBlock *block)
{
    if (block == NULL) { return false; }
    const uint32_t old = Lock();
    const uint32_t generation = state.callback_count;
    const uint32_t elapsed = generation - consumed_generation;
    if (!state.running || claimed || elapsed == 0U)
    { Unlock(old); return false; }
    state.dropped_blocks += elapsed - 1U;
    consumed_generation = generation;
    if (!HalfIsInactive(completed_half))
    { ++state.dropped_blocks; Unlock(old); return false; }
    claim_generation = generation; claim_half = completed_half; claimed = true;
    block->samples = audio_dma_buffer + (uint32_t)claim_half * BSP_AUDIO_HALF_ELEMENTS;
    block->generation = generation;
    block->completed_ms = state.last_callback_ms; block->half = claim_half;
    Unlock(old);
    return true;
}

bool BSP_Audio_EndBlock(const BSP_AudioBlock *block, bool processed)
{
    if (block == NULL) { return false; }
    const uint32_t old = Lock();
    if (!claimed || block->generation != claim_generation || block->half != claim_half ||
        block->samples != audio_dma_buffer + (uint32_t)claim_half * BSP_AUDIO_HALF_ELEMENTS)
    { Unlock(old); return false; }
    const bool valid = processed && state.running &&
        state.callback_count == claim_generation && HalfIsInactive(claim_half);
    claimed = false;
    if (valid) { ++state.processed_blocks; } else { ++state.dropped_blocks; }
    Unlock(old);
    return valid;
}

static void Completed(SAI_HandleTypeDef *handle, uint8_t half)
{
    if (handle != &hsai_BlockB2 || !accepting_callbacks || !state.running) { return; }
    if (half != expected_half) { ++state.error_count; }
    expected_half = half ^ 1U;
    if (half == 0U) { ++state.half_count; } else { ++state.full_count; }
    completed_half = half;
    const uint32_t tick_ms = HAL_GetTick();
    if (state.callback_count == 0U) { state.first_callback_ms = tick_ms; }
    state.last_callback_ms = tick_ms;
    __DMB(); ++state.callback_count;
}
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *handle) { Completed(handle, 0U); }
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *handle) { Completed(handle, 1U); }
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *handle)
{
    if (handle != &hsai_BlockB2 || !accepting_callbacks) { return; }
    ++state.error_count;
    state.hal_error |= HAL_SAI_GetError(handle);
    state.dma_error |= HAL_DMA_GetError(handle->hdmarx);
    state.running = false;
}
