/**
 * @file        bsp_audio.c
 * @brief       SAI2 循环采集、DMA 半块借用与帧索引的板级边界。
 * @details     使用已生成的 SAI2 A 块提供时钟、B 块接收 MIC2；IRQ 只发布半块完成状态，前台通过 Begin/End 验证
 *          持续 DMA 覆写下的数据与索引。
 * @note        DMA2 Stream7 Channel0，外设到内存、16 位循环；32768 元素缓冲位于独立 64 KiB SRAM1 段并按 32 字
 *          节对齐。当前启动未启用 I/D Cache；本模块不添加维护操作。
 */

#include "bsp_audio.h"
#include "bsp_audio_codec.h"
#include "Audio/audio_frame_index.h"
#include "main.h"
#include <stddef.h>

extern SAI_HandleTypeDef hsai_BlockA2, hsai_BlockB2;
/* DMA-only NOLOAD storage is separate from BSS and precedes the newlib heap.
   Cache is disabled in
   this CubeMX snapshot; do not add unrelated cache maintenance here. */
static volatile int16_t audio_dma_buffer[BSP_AUDIO_DMA_ELEMENTS]
    __attribute__((section(".AudioDmaSection"), aligned(32)));
static volatile BSP_AudioSnapshot state;
static AudioFrameIndex frame_index;
static uint32_t session_counter;
static uint32_t capture_started_ms;
static uint64_t delivered_end_frame;
static BSP_AudioBlock claim;
_Static_assert(BSP_AUDIO_HALF_FRAMES == AUDIO_INDEX_BLOCK_FRAMES,
               "Frame index must match the generated DMA buffer layout");
static bool dma_owned, clock_owned, claimed;
static volatile bool accepting_callbacks;
static uint32_t consumed_generation, claim_generation;
static uint8_t completed_half, claim_half;

/**
 * @brief   保存中断屏蔽状态并进入音频共享状态临界区。
 * @return  进入前的 PRIMASK，交给对应 Unlock 恢复。
 * @note    仅屏蔽 CPU 中断并执行内存屏障；DMA 仍可继续写入。
 */
static uint32_t Lock(void)
{
    const uint32_t old = __get_PRIMASK();
    __disable_irq(); __DMB(); return old;
}
/**
 * @brief   发布共享状态并恢复进入临界区前的中断屏蔽状态。
 * @param   old 对应 Lock 返回的原始 PRIMASK。
 * @return  无返回值。
 * @note    不固定开启中断，保留调用者原先的中断屏蔽状态。
 */
static void Unlock(uint32_t old) { __DMB(); __set_PRIMASK(old); }

/**
 * @brief   停止音频回调接收并尝试释放本模块持有的 DMA 与时钟。
 * @return  true 表示确认 DMA 已停止并释放本模块时钟；false 表示停止未确认，保留 DMA 所有权且记录错误 5。
 * @note    仅前台且保持中断可运行；HAL 停止有短等待。先禁止回调更新，确认 SAI/DMA 禁用及 HAL DMA READY 后才允
 *          许复用缓冲；失败时不假定内存已交还 CPU。
 */
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

/**
 * @brief   启动已生成的 SAI2 时钟、MIC2 配置和循环音频接收。
 * @return  true 表示 DMA 接收启动且未被同步错误回调停止；false 的原因见快照 start_error。
 * @note    仅前台调用且须已完成 CubeMX 的 SAI2/DMA 初始化；先启用 A 块时钟再配置编解码器，最后启动 B 块接收。
 *          在配置完成后预留本次 DMA 尝试的 boot 内 session；已分配标识即使 DMA 启动失败也不复用。失败清理可能
 *          失败，须检查 Stop 和快照。
 */
bool BSP_Audio_Start(void)
{
    if (session_counter == UINT32_MAX)
    { state.start_error = 6U; return false; }
    if (dma_owned || clock_owned ||
        HAL_SAI_GetState(&hsai_BlockA2) != HAL_SAI_STATE_READY ||
        HAL_SAI_GetState(&hsai_BlockB2) != HAL_SAI_STATE_READY ||
        hsai_BlockB2.hdmarx == NULL ||
        HAL_DMA_GetState(hsai_BlockB2.hdmarx) != HAL_DMA_STATE_READY ||
        (hsai_BlockB2.hdmarx->Instance->CR & DMA_SxCR_EN) != 0U)
    { state.start_error = 1U; return false; }
    state = (BSP_AudioSnapshot){0};
    consumed_generation = claim_generation = 0U;
    completed_half = claim_half = 0U;
    delivered_end_frame = 0U;
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
    /* Boot-local, non-reused attempt identity. Reserve before DMA can IRQ. */
    state.session = ++session_counter;
    capture_started_ms = HAL_GetTick();
    AudioFrameIndex_Init(&frame_index, capture_started_ms);
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

/**
 * @brief   在短临界区中复制音频状态快照。
 * @param   output 调用者拥有的输出结构体；NULL 时不执行复制。
 * @return  无返回值。
 * @note    主循环读取 IRQ 更新的计数；快照是统计信息，不授予 PCM 缓冲区访问权。
 */
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *output)
{
    if (output == NULL) { return; }
    const uint32_t old = Lock(); *output = state; Unlock(old);
}

/**
 * @brief   基于 DMA NDTR 与已完成块计数读取保守的双声道帧游标。
 * @param   output 调用者拥有的游标输出；NULL 返回 false，非空时先清零，失败保持零值。
 * @return  true 表示两次 NDTR、边界标志及会话状态一致；false 表示状态或采样存在歧义。
 * @note    仅前台调用；短临界区不停止 DMA，拒绝 HT/TC/错误待处理、重载边界和超期状态。不消费音频块，不清 DMA
 *          标志，也不保证 SRAM 写入可见性或声学采样时刻。
 */
bool BSP_Audio_ReadCursor(BSP_AudioCursor *output)
{
    if (output == NULL) { return false; }
    *output = (BSP_AudioCursor){0};
    const uint32_t old = Lock();
    const uint32_t last_ms = state.observed_frames == 0U ? capture_started_ms : state.last_callback_ms;
    if (!state.running || !dma_owned || state.session == 0U || state.index_faults != 0U ||
        hsai_BlockB2.hdmarx == NULL ||
        HAL_SAI_GetState(&hsai_BlockB2) != HAL_SAI_STATE_BUSY_RX ||
        HAL_DMA_GetState(hsai_BlockB2.hdmarx) != HAL_DMA_STATE_BUSY ||
        (hsai_BlockB2.Instance->CR1 & SAI_xCR1_SAIEN) == 0U ||
        (hsai_BlockB2.hdmarx->Instance->CR & DMA_SxCR_EN) == 0U ||
        (uint32_t)(HAL_GetTick() - last_ms) > AUDIO_INDEX_MAX_INTERVAL_MS)
    { Unlock(old); return false; }
    const uint32_t first = __HAL_DMA_GET_COUNTER(hsai_BlockB2.hdmarx);
    const uint32_t second = __HAL_DMA_GET_COUNTER(hsai_BlockB2.hdmarx);
    /* IRQs are masked only for these bounded reads. DMA keeps running; flags
       read AFTER NDTR catch boundaries/errors even before the IRQ executes. */
    const uint32_t flags = __HAL_DMA_GET_FLAG(hsai_BlockB2.hdmarx,
        __HAL_DMA_GET_HT_FLAG_INDEX(hsai_BlockB2.hdmarx) |
        __HAL_DMA_GET_TC_FLAG_INDEX(hsai_BlockB2.hdmarx) |
        __HAL_DMA_GET_TE_FLAG_INDEX(hsai_BlockB2.hdmarx) |
        __HAL_DMA_GET_FE_FLAG_INDEX(hsai_BlockB2.hdmarx) |
        __HAL_DMA_GET_DME_FLAG_INDEX(hsai_BlockB2.hdmarx));
    const bool first_half = first > BSP_AUDIO_HALF_ELEMENTS;
    if (flags != RESET || first == 0U || first > BSP_AUDIO_DMA_ELEMENTS ||
        second == 0U || second > first ||
        first_half != (second > BSP_AUDIO_HALF_ELEMENTS) ||
        (state.callback_count & 1U) != (first_half ? 0U : 1U))
    { Unlock(old); return false; }
    const uint32_t base = first_half ? BSP_AUDIO_DMA_ELEMENTS : BSP_AUDIO_HALF_ELEMENTS;
    const uint32_t complete = (base - second) / 2U;
    if (state.observed_frames > UINT64_MAX - complete)
    { Unlock(old); return false; }
    *output = (BSP_AudioCursor){state.observed_frames + complete,
                              state.session, state.callback_count};
    Unlock(old);
    return true;
}

/**
 * @brief   判断指定音频半区此刻是否脱离 DMA 当前写入范围。
 * @param   half 半区索引；本模块仅传入 0 或 1。
 * @return  true 表示无待处理半满/全满标志且 NDTR 指向另一半区；false 表示不能确认安全。
 * @note    由持有短临界区的前台 Begin/End 调用；DMA 不受 PRIMASK 限制，NDTR 为零的重载边界必须拒绝。
 */
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

/**
 * @brief   向唯一前台消费者借出最新完成且当前未被 DMA 写入的半块。
 * @param   block 调用者拥有的输出描述符；NULL 返回 false，失败不承诺清空。成功时 samples 借用模块缓冲，不得写
 *          入或释放。
 * @return  true 表示建立一次待结束认领；false 表示无新块、已有认领或无法确认半区安全。
 * @note    仅单一前台消费者；每次成功必须调用一次 EndBlock。每半块为 16384 个 int16 元素、8192 双声道帧；
 *          Begin 不冻结 DMA，结果须经 End 验证才可发布。gap_frames 是距前一次成功 End 的观测帧缺口。
 */
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
    block->session = state.session;
    block->frame_count = BSP_AUDIO_HALF_FRAMES;
    block->first_frame = state.observed_frames - BSP_AUDIO_HALF_FRAMES;
    block->index_valid = state.index_faults == 0U;
    block->gap_frames = block->index_valid ? block->first_frame - delivered_end_frame : 0U;
    block->contiguous = block->index_valid && block->gap_frames == 0U;
    claim = *block;
    Unlock(old);
    return true;
}

/**
 * @brief   验证并结束音频半块认领，统计成功处理或丢弃。
 * @param   block 本次 Begin 输出的未修改描述符，只在调用期间读取；NULL 或字段不匹配返回 false，字段不匹配不会
 *          解除已有认领。
 * @param   processed 调用者是否已成功完成本块计算；false 会结束匹配认领并将其计为丢弃。
 * @return  true 表示处理成功、会话仍运行且半区未被 DMA 覆写；false 表示认领无效或结果须丢弃。
 * @note    仅前台调用；有效位置还要求 block->index_valid。匹配认领结束后不可继续依赖借出的 samples；成功时才
 *          推进已交付帧末端。
 */
bool BSP_Audio_EndBlock(const BSP_AudioBlock *block, bool processed)
{
    if (block == NULL) { return false; }
    const uint32_t old = Lock();
    if (!claimed || block->generation != claim_generation || block->half != claim_half ||
        block->samples != audio_dma_buffer + (uint32_t)claim_half * BSP_AUDIO_HALF_ELEMENTS ||
        block->session != claim.session || block->first_frame != claim.first_frame ||
        block->frame_count != claim.frame_count || block->gap_frames != claim.gap_frames ||
        block->completed_ms != claim.completed_ms || block->index_valid != claim.index_valid ||
        block->contiguous != claim.contiguous)
    { Unlock(old); return false; }
    const bool valid = processed && state.running &&
        state.callback_count == claim_generation && HalfIsInactive(claim_half);
    claimed = false;
    if (valid)
    {
        ++state.processed_blocks;
        if (block->index_valid) { delivered_end_frame = block->first_frame + block->frame_count; }
    }
    else { ++state.dropped_blocks; }
    Unlock(old);
    return valid;
}

/**
 * @brief   将 SAI 接收半满或全满事件更新为前台可读取的块代数和索引。
 * @param   handle HAL 提供的借用 SAI 句柄；只接受 hsai_BlockB2，其余忽略。
 * @param   half 完成半区索引；半满回调传 0，全满回调传 1。
 * @return  无返回值。
 * @note    DMA2_Stream7 的 HAL 回调路径中执行；记录 HAL 毫秒 tick、帧数、错误和完成半区后发布 callback_count
 *          ，不做统计计算或日志输出，也不停止循环 DMA。
 */
static void Completed(SAI_HandleTypeDef *handle, uint8_t half)
{
    if (handle != &hsai_BlockB2 || !accepting_callbacks || !state.running) { return; }
    if (half != frame_index.expected_half) { ++state.error_count; }
    if (half == 0U) { ++state.half_count; } else { ++state.full_count; }
    completed_half = half;
    const uint32_t tick_ms = HAL_GetTick();
    if (state.observed_frames == 0U) { state.first_callback_ms = tick_ms; }
    AudioFrameIndex_Complete(&frame_index, half, tick_ms);
    state.observed_frames = frame_index.frames;
    state.index_faults |= frame_index.faults;
    state.last_callback_ms = tick_ms;
    __DMB(); ++state.callback_count;
}
/**
 * @brief   把音频 DMA 前半区完成事件交给 Completed。
 * @param   handle HAL 提供的借用句柄；非 hsai_BlockB2 或非接收状态由 Completed 忽略。
 * @return  无返回值。
 * @note    DMA2_Stream7 IRQ 经 HAL_DMA_IRQHandler 和 SAI 内部回调进入；标记半区 0 可供前台尝试认领。
 */
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *handle) { Completed(handle, 0U); }
/**
 * @brief   把音频 DMA 后半区完成事件交给 Completed。
 * @param   handle HAL 提供的借用句柄；非 hsai_BlockB2 或非接收状态由 Completed 忽略。
 * @return  无返回值。
 * @note    DMA2_Stream7 IRQ 经 HAL_DMA_IRQHandler 和 SAI 内部回调进入；标记半区 1，DMA 循环重载后继续接收。
 */
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *handle) { Completed(handle, 1U); }
/**
 * @brief   记录音频 SAI/DMA 错误并禁止前台接受后续音频结果。
 * @param   handle HAL 提供的借用句柄；仅处理 hsai_BlockB2 且 accepting_callbacks 为真时的错误。
 * @return  无返回值。
 * @note    可由 SAI2 IRQ 或 SAI 的 DMA 错误转发路径触发；只累计错误、标记索引失效并清 running，不在 IRQ 中停
 *          止 DMA，内存所有权仍需前台 Stop 确认。
 */
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *handle)
{
    if (handle != &hsai_BlockB2 || !accepting_callbacks) { return; }
    ++state.error_count;
    state.index_faults |= AUDIO_INDEX_HAL_ERROR;
    state.hal_error |= HAL_SAI_GetError(handle);
    state.dma_error |= HAL_DMA_GetError(handle->hdmarx);
    state.running = false;
}
