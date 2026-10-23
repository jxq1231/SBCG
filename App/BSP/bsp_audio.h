/**
 * @file        bsp_audio.h
 * @brief       SAI2 循环采集、DMA 半块借用与帧索引的板级边界。
 * @details     声明采集状态快照、只读 PCM 半块描述符、帧游标及启动/停止/认领接口；时间字段为 HAL 毫秒计数，帧
 *          字段按双声道传输组计，derived_milli_hz 单位为毫赫兹。
 * @note        DMA2 Stream7 Channel0，外设到内存、16 位循环；32768 元素缓冲位于独立 64 KiB SRAM1 段并按 32 字
 *          节对齐。当前启动未启用 I/D Cache；本模块不添加维护操作。
 */

#ifndef SBCG_BSP_AUDIO_H
#define SBCG_BSP_AUDIO_H
#include <stdbool.h>
#include <stdint.h>

/** @brief 整个 DMA 环的 int16 元素数；每个有效双声道帧含两个元素。 */
#define BSP_AUDIO_DMA_ELEMENTS 32768U
#define BSP_AUDIO_HALF_ELEMENTS (BSP_AUDIO_DMA_ELEMENTS / 2U)
#define BSP_AUDIO_HALF_FRAMES (BSP_AUDIO_HALF_ELEMENTS / 2U)
/** @brief 标称采样率（Hz）；实际配置推导值在快照 derived_milli_hz 中报告。 */
#define BSP_AUDIO_NOMINAL_HZ 48000U

/**
 * @brief 音频启动诊断和 IRQ/前台处理计数的复制快照。
 * @note running=false 不等于 DMA 已确认停止；错误详情保留在 hal_error/dma_error。
 *       session 为本次开机内不复用的接收尝试标识；index_faults 为索引故障位，非零时不得使用绝对帧位置。
 */
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

/**
 * @brief 一次成功 Begin 借出的只读音频半块和对应观测位置。
 * @note samples 指向模块持有的循环 DMA 内存；half 为 0/1，generation 为完成回调代数，completed_ms 为 HAL tick
 *          。
 *       frame_count/first_frame/gap_frames 单位均为双声道帧，first_frame 从会话首帧零开始。
 *       只有 End 成功且 index_valid=true 才能使用位置；contiguous 表示距前次成功交付无观测帧缺口。
 */
typedef struct
{
    const volatile int16_t *samples;
    uint32_t generation, completed_ms;
    uint8_t half;
    uint32_t session, frame_count;
    uint64_t first_frame, gap_frames;
    bool index_valid, contiguous;
} BSP_AudioBlock;

/**
 * @brief NDTR 观测的会话内完整双声道传输组数量。
 * @note complete_frames 含 IRQ 已发布半块与当前半块进度；boundary_generation 为读取时的完成回调代数。
 *       不授予 samples 的读取权，也不证明采样在物理声学时间上的位置。
 */
typedef struct
{
    uint64_t complete_frames;
    uint32_t session, boundary_generation;
} BSP_AudioCursor;

/* Foreground only; a read-only NDTR-based stereo transfer-group count, not
   proof of SRAM write visibility or an acoustic timestamp. PCM ownership
   still requires Begin/End. Rejects boundary/error/reload ambiguity.
   Clears output on failure; does not consume blocks or clear IRQ flags. */
/**
 * @brief   基于 DMA NDTR 与已完成块计数读取保守的双声道帧游标。
 * @param   output 调用者拥有的游标输出；NULL 返回 false，非空时先清零，失败保持零值。
 * @return  true 表示两次 NDTR、边界标志及会话状态一致；false 表示状态或采样存在歧义。
 * @note    仅前台调用；短临界区不停止 DMA，拒绝 HT/TC/错误待处理、重载边界和超期状态。不消费音频块，不清 DMA
 *          标志，也不保证 SRAM 写入可见性或声学采样时刻。
 */
bool BSP_Audio_ReadCursor(BSP_AudioCursor *output);

/* Main-loop only. Uses already-generated SAI2 and DMA; no peripheral init.
   Start errors: 1=not ready/owned, 2=codec, 3=DMA start, 4=clock, 5=stop,
   6=boot-local session identity exhausted. */
/**
 * @brief   启动已生成的 SAI2 时钟、MIC2 配置和循环音频接收。
 * @return  true 表示 DMA 接收启动且未被同步错误回调停止；false 的原因见快照 start_error。
 * @note    仅前台调用且须已完成 CubeMX 的 SAI2/DMA 初始化；先启用 A 块时钟再配置编解码器，最后启动 B 块接收。
 *          在配置完成后预留本次 DMA 尝试的 boot 内 session；已分配标识即使 DMA 启动失败也不复用。失败清理可能
 *          失败，须检查 Stop 和快照。
 */
bool BSP_Audio_Start(void);
/**
 * @brief   停止音频回调接收并尝试释放本模块持有的 DMA 与时钟。
 * @return  true 表示确认 DMA 已停止并释放本模块时钟；false 表示停止未确认，保留 DMA 所有权且记录错误 5。
 * @note    仅前台且保持中断可运行；HAL 停止有短等待。先禁止回调更新，确认 SAI/DMA 禁用及 HAL DMA READY 后才允
 *          许复用缓冲；失败时不假定内存已交还 CPU。
 */
bool BSP_Audio_Stop(void);
/**
 * @brief   在短临界区中复制音频状态快照。
 * @param   output 调用者拥有的输出结构体；NULL 时不执行复制。
 * @return  无返回值。
 * @note    主循环读取 IRQ 更新的计数；快照是统计信息，不授予 PCM 缓冲区访问权。
 */
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *output);
/* Single foreground consumer. Every successful Begin requires one End.
   End rejects DMA-overwritten data, even when its boundary IRQ is pending.
   Position is usable only after End succeeds AND index_valid is true.
   gap_frames counts observed frames since the preceding successful End.
   session is boot-local; a new BOOT delimits the consumer history. */
/**
 * @brief   向唯一前台消费者借出最新完成且当前未被 DMA 写入的半块。
 * @param   block 调用者拥有的输出描述符；NULL 返回 false，失败不承诺清空。成功时 samples 借用模块缓冲，不得写
 *          入或释放。
 * @return  true 表示建立一次待结束认领；false 表示无新块、已有认领或无法确认半区安全。
 * @note    仅单一前台消费者；每次成功必须调用一次 EndBlock。每半块为 16384 个 int16 元素、8192 双声道帧；
 *          Begin 不冻结 DMA，结果须经 End 验证才可发布。gap_frames 是距前一次成功 End 的观测帧缺口。
 */
bool BSP_Audio_BeginBlock(BSP_AudioBlock *block);
/**
 * @brief   验证并结束音频半块认领，统计成功处理或丢弃。
 * @param   block 本次 Begin 输出的未修改描述符，只在调用期间读取；NULL 或字段不匹配返回 false，字段不匹配不会
 *          解除已有认领。
 * @param   processed 调用者是否已成功完成本块计算；false 会结束匹配认领并将其计为丢弃。
 * @return  true 表示处理成功、会话仍运行且半区未被 DMA 覆写；false 表示认领无效或结果须丢弃。
 * @note    仅前台调用；有效位置还要求 block->index_valid。匹配认领结束后不可继续依赖借出的 samples；成功时才
 *          推进已交付帧末端。
 */
bool BSP_Audio_EndBlock(const BSP_AudioBlock *block, bool processed);
#endif
