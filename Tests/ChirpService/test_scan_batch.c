/**
 * @file        test_scan_batch.c
 * @brief       验证8192帧扫描实际走完整64点批量入口，而不是逐帧回退。
 * @details     用GCC函数进入钩子分别计数SoundDetector_Push/PushBatch；扫描时不得逐帧调用，
 *              每个8192帧块应恰好128次批量推进，且候选与任务行为保持不变。
 * @note        仅主机测试；不访问HAL、DMA、日志队列或开发板，耗时结论不来自本测试。
 */
#include "Detector/chirp_service.h"
#include "Detector/chirp_template.h"
#include "Detector/sound_detector.h"
#include "BSP/bsp_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t tick_ms;
static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS];
static unsigned log_lines;
static uint64_t push_calls,batch_calls;
static void *push_address,*batch_address,*last_function;
static bool calibrating;
/** @brief 条件求值一次；失败打印行号退出，无返回值，测试专用。 */
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)

/** @brief 受控HAL毫秒，仅由测试推进；无参数。 */
uint32_t BSP_Console_GetTickMs(void){return tick_ms;}
/** @brief 记录非空有界日志行；text借用length字节，返回前消费，无返回值。 */
static void Log(const char *text,int length)
{
    (void)text;CHECK(length>0 && length<256);++log_lines;
}
/**
 * @brief 测试专用GCC函数进入钩子，分别累计两个检测入口的调用次数。
 * @param function GCC提供的被进入函数地址，仅与校准地址比较。
 * @param caller GCC提供的调用点地址，未使用。
 * @return 无返回值。
 * @note 本函数禁止自身插桩；校准阶段只保存最后进入地址，不累计。
 */
void __attribute__((no_instrument_function)) __cyg_profile_func_enter(void *function,void *caller)
{
    (void)caller;
    if(calibrating){last_function=function;return;}
    if(function==push_address)++push_calls;
    else if(function==batch_address)++batch_calls;
}
/** @brief GCC退出钩子；两个地址均未使用，无返回值。 */
void __attribute__((no_instrument_function)) __cyg_profile_func_exit(void *function,void *caller)
{(void)function;(void)caller;}

/**
 * @brief 校准两个检测入口的真实地址，避免函数指针到void*的非标准转换。
 * @return 无返回值；校准失败由CHECK终止。
 * @note 逐帧入口以未配置状态调用，只进入并返回INVALID；批量入口以NULL消费指针提前拒绝，
 *       不会嵌套调用逐帧入口。
 */
static void Calibrate(void)
{
    SoundCandidate out;SoundDetector detector={0};const int16_t sample=0;
    calibrating=true;
    CHECK(SoundDetector_Push(&detector,0U,0U,sample,&out)==SOUND_DETECTOR_INVALID);
    push_address=last_function;
    CHECK(SoundDetector_PushBatch(NULL,1U,0U,&sample,1U,NULL,&out)==SOUND_DETECTOR_INVALID);
    batch_address=last_function;
    calibrating=false;
    CHECK(push_address!=NULL && batch_address!=NULL);
}
/**
 * @brief 暂存并提交一个8192帧块，仅在声道0的onset处写入冻结模板。
 * @param first 块首绝对帧。
 * @param onset 模板起点绝对帧。
 * @return 无返回值；Stage/Complete失败由CHECK终止。
 */
static void StageBlock(uint64_t first,uint64_t onset)
{
    memset(pcm,0,sizeof pcm);
    for(uint32_t i=0U;i<BSP_AUDIO_HALF_FRAMES;++i)
    {
        const uint64_t frame=first+i;
        if(frame>=onset && frame-onset<CHIRP_TEMPLATE_FRAMES)
            pcm[2U*i]=(int16_t)ChirpTemplate_PCM[frame-onset];
    }
    const BSP_AudioBlock block={.samples=pcm,.session=1U,.frame_count=BSP_AUDIO_HALF_FRAMES,
        .first_frame=first,.completed_ms=tick_ms,.index_valid=true,.contiguous=true};
    CHECK(ChirpService_Stage(tick_ms,&block));
    CHECK(ChirpService_Complete(true));
}
/** @brief 以当前会话快照推进一次Process；返回无返回值，只借用快照。 */
static void ProcessOnce(void)
{
    const BSP_AudioSnapshot snapshot={.running=true,.session=1U,.callback_count=1U,
        .last_callback_ms=tick_ms};
    ChirpService_Process(tick_ms++,&snapshot);
}
/** @brief 验证两个连续8192帧块均为128次批量推进且不逐帧回退；成功返回0。 */
int main(void)
{
    Calibrate();
    CHECK(ChirpService_Init(0U,0U,Log));
    tick_ms=100U;
    StageBlock(0U,2000U);
    ChirpServiceSnapshot state;
    push_calls=batch_calls=0U;
    ProcessOnce();
    ChirpService_GetSnapshot(&state);
    CHECK(batch_calls==128U && push_calls==0U);
    CHECK(state.candidates==1U && state.last_scanned==CHIRP_SERVICE_SCAN_BUDGET);
    CHECK(state.max_scanned==CHIRP_SERVICE_SCAN_BUDGET);
    /* 连续第二块必须继续走快速路径，不能因块边界回退。 */
    tick_ms=371U;
    StageBlock(8192U,UINT64_MAX);
    push_calls=batch_calls=0U;
    ProcessOnce();
    ChirpService_GetSnapshot(&state);
    CHECK(batch_calls==128U && push_calls==0U && state.candidates==1U);
    /* 原任务仍须完成：257窗搜索后再出现一个匹配事件。 */
    for(unsigned i=0U;i<40U;++i)ProcessOnce();
    ChirpService_GetSnapshot(&state);
    CHECK(state.matches==1U && state.completed_jobs==1U && state.dropped==0U);
    printf("PASS scan batch path: 2x8192 frames, 128 batch calls/block, 0 per-frame pushes, candidate and task retained\n");
    return 0;
}
