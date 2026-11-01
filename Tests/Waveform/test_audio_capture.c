/**
 * @file test_audio_capture.c
 * @brief 联测真实AudioCapture与波形服务的唯一借用和归还后发布边界。
 * @details BSP仅提供一块PCM及受控End结果；被测代码自行执行Stage/Complete。
 * @note 纯主机测试，无DMA或SDRAM实物访问；不直接调用波形Stage/Complete接口。
 */
#include "Audio/audio_capture.h"
#include "UI/waveform_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)

static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS];
static BSP_AudioSnapshot audio;
static uint32_t now_ms;
static unsigned begins, ends;
static bool available, end_accepted = true, inside_borrow;
static int16_t committed_value;
static bool expect_committed;
static uint64_t next_frame;

/** @brief 返回前台fixture毫秒，不在本次处理内自行推进。@return 当前毫秒。 */
uint32_t BSP_Console_GetTickMs(void) { return now_ms; }
/** @brief 停止fixture流。@return 成功true。 */
bool BSP_Audio_Stop(void) { audio.running = false; return true; }
/** @brief 启动fixture流。@return 成功true。 */
bool BSP_Audio_Start(void) { audio.running = true; return true; }
/** @brief 复制BSP标量状态。@param out 非NULL输出。@return 无返回值。 */
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *out) { *out = audio; }

/** @brief 每次处理至多借出一块。@param block 非NULL借用输出。@return 有块true，否则false。 */
bool BSP_Audio_BeginBlock(BSP_AudioBlock *block)
{
    ++begins;
    CHECK(!inside_borrow);
    if (!available) return false;
    available = false;
    inside_borrow = true;
    *block = (BSP_AudioBlock){.samples=pcm, .generation=audio.callback_count,
        .completed_ms=now_ms, .session=1U, .frame_count=BSP_AUDIO_HALF_FRAMES,
        .first_frame=next_frame, .index_valid=true, .contiguous=next_frame!=0U};
    next_frame += BSP_AUDIO_HALF_FRAMES;
    return true;
}

/**
 * @brief 归还前主动读取波形，验证本次暂存尚未发布；模拟DMA立即覆盖借用内存。
 * @param block 原Begin借用，仅本次调用有效。
 * @param processed 必须为真实统计计算成功结果。
 * @return fixture指定的End结果；false必须使波形撤值。
 */
bool BSP_Audio_EndBlock(const BSP_AudioBlock *block, bool processed)
{
    CHECK(inside_borrow && block->samples==pcm && processed);
    ++ends;
    WaveformService_Update(BOARD_ROLE_A,false,NULL,0U,now_ms);
    WaveformSnapshot before;
    WaveformService_GetSnapshot(&before);
    CHECK(before.valid==expect_committed);
    if (expect_committed)
        for (unsigned i=0U;i<before.point_count;++i) CHECK(before.points[i]==committed_value);
    memset(pcm,0,sizeof pcm);
    inside_borrow = false;
    return end_accepted;
}

/** @brief 消费同步日志，不保存栈指针。@param text 只读日志。@param length 有效字节。@return 无返回值。 */
static void Log(const char *text, int length) { CHECK(text!=NULL && length>0); }

/** @brief 准备左右不同定值PCM。@param left 左声道幅值。@return 无返回值，仅安排下一块。 */
static void Prepare(int16_t left)
{
    for (unsigned i=0U;i<BSP_AUDIO_HALF_FRAMES;++i) { pcm[2U*i]=left; pcm[2U*i+1U]=-25000; }
    available = true;
    ++audio.callback_count;
    audio.last_callback_ms = now_ms;
    audio.observed_frames = next_frame + BSP_AUDIO_HALF_FRAMES;
}

/** @brief 推进真实波形服务并验证归还后独立CPU副本。@param valid 预期资格。@param value 预期左声道值。@return 无返回值。 */
static void CheckWave(bool valid, int16_t value)
{
    WaveformService_Update(BOARD_ROLE_A,false,NULL,0U,now_ms);
    WaveformSnapshot wave;
    WaveformService_GetSnapshot(&wave);
    CHECK(wave.valid==valid);
    if (valid) {
        CHECK(!wave.synced && wave.point_count==WAVEFORM_POINT_COUNT);
        for (unsigned i=0U;i<wave.point_count;++i) CHECK(wave.points[i]==value);
    }
}

/** @brief 验证初始化、成功/失败归还及前台暂停的真实扇出。@return 成功0。 */
int main(void)
{
    audio.session = 1U;
    audio.derived_milli_hz = 48000000U;
    AudioCapture_Init(0U,Log);
    // 未初始化波形服务时，音频仍正常消费，但波形不得碰外部历史或发布数据。
    now_ms=100U;Prepare(11000);AudioCapture_Process(now_ms,true);
    CHECK(begins==1U && ends==1U && !inside_borrow);
    now_ms=300U;CheckWave(false,0);

    WaveformService_Init();
    now_ms=400U;Prepare(12000);AudioCapture_Process(now_ms,true);
    CHECK(begins==2U && ends==2U && !inside_borrow);
    now_ms=600U;CheckWave(true,12000);
    expect_committed=true;committed_value=12000;

    now_ms=800U;end_accepted=false;Prepare(23000);AudioCapture_Process(now_ms,true);
    CHECK(begins==3U && ends==3U && !inside_borrow);
    now_ms=1000U;CheckWave(false,0);
    expect_committed=false;

    now_ms=1100U;Prepare(23000);end_accepted=true;
    WaveformSnapshot paused_before,paused_after;
    WaveformService_GetSnapshot(&paused_before);
    AudioCapture_Process(now_ms,false);
    WaveformService_GetSnapshot(&paused_after);
    CHECK(begins==3U && ends==3U && available && !inside_borrow);
    CHECK(paused_before.frame_seq==paused_after.frame_seq && paused_before.valid==paused_after.valid);
    CHECK(paused_before.point_count==paused_after.point_count);
    CHECK(memcmp(paused_before.points,paused_after.points,sizeof paused_before.points)==0);
    now_ms=1200U;AudioCapture_Process(now_ms,true);
    CHECK(begins==4U && ends==4U && !inside_borrow);
    now_ms=1400U;CheckWave(true,23000);
    AudioCapture_Process(now_ms,true); // 无块时只尝试一次Begin，不额外调用End。
    CHECK(begins==5U && ends==4U && !inside_borrow);
    puts("PASS real AudioCapture->Waveform: one borrow, pre-End unpublished, real End failure clears, pause, left PCM CPU copy");
    return 0;
}
