/**
 * @file        test_batch_service.c
 * @brief       对照逐帧参考验证SoundDetectorService_Stage的64点批量迁移。
 * @details     同一合成PCM和End结果分别驱动真实服务与一个独立的逐帧SoundDetector参考；
 *              比较候选序列、session/帧号/质量字段、候选计数、布防与队列状态。
 *              使用GCC函数进入钩子确认窗口64的整块Stage实际走SoundDetector_PushBatch。
 * @note        纯主机测试；不访问HAL、DMA、串口或开发板，耗时结论不来自本测试。
 */
#include "Detector/sound_detector_service.h"
#include "Detector/sound_detector.h"
#include "BSP/bsp_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS];
static SoundDetector ref;
static SoundCandidate expected[16];
static unsigned expected_count,published_count,state_logs;
static uint32_t test_channel;
static char candidate_lines[16][256];
static BSP_AudioSnapshot health;
static const SoundDetectorConfig config={64U,256U,128U,2U,8U,24000U};
static volatile int counting;
static unsigned push_calls,batch_calls;
static void *push_address,*batch_address,*last_function;
static bool calibrating;

/** @brief 受控HAL毫秒，本测试固定为0，避免主机时间影响业务判定。 */
uint32_t BSP_Console_GetTickMs(void){return 0U;}
/** @brief GCC函数进入钩子：校准两个检测入口地址，并在计数窗口内分别累计调用。 */
void __attribute__((no_instrument_function)) __cyg_profile_func_enter(void *function,void *caller)
{
    (void)caller;
    if(calibrating){last_function=function;return;}
    if(!counting)return;
    if(function==push_address)++push_calls;
    else if(function==batch_address)++batch_calls;
}
/** @brief GCC函数退出钩子；参数未使用，无返回值。 */
void __attribute__((no_instrument_function)) __cyg_profile_func_exit(void *function,void *caller)
{(void)function;(void)caller;}
/** @brief 保存候选行并计数；text借用length字节，返回前复制。 */
static void Log(const char *text,int length)
{
    CHECK(length>1 && length<256 && text[length-2]=='\r' && text[length-1]=='\n');
    if(strncmp(text,"$SOUND_STATE,",13)==0){++state_logs;return;}
    if(strncmp(text,"$SOUND_CAND,",12)!=0)return;
    CHECK(published_count<16U);
    memcpy(candidate_lines[published_count],text,(size_t)length);
    candidate_lines[published_count][length]=0;
    ++published_count;
}
/**
 * @brief 校准两个检测入口地址，避免函数指针到void*的非标准转换。
 * @return 无返回值；失败由CHECK终止。
 */
static void Calibrate(void)
{
    SoundCandidate out;SoundDetector dummy={0};const int16_t sample=0;
    calibrating=true;
    CHECK(SoundDetector_Push(&dummy,0U,0U,sample,&out)==SOUND_DETECTOR_INVALID);
    push_address=last_function;
    CHECK(SoundDetector_PushBatch(NULL,1U,0U,&sample,1U,NULL,&out)==SOUND_DETECTOR_INVALID);
    batch_address=last_function;
    calibrating=false;
    CHECK(push_address!=NULL && batch_address!=NULL && push_address!=batch_address);
}
/** @brief 填充一个8192帧块：声道0在[high_begin,high_end)写±10000，其余为0。 */
/** @brief 填充一个8192帧块：指定声道在[high_begin,high_end)写±10000，其余为0。 */
static void FillBlockOn(uint32_t channel,uint32_t high_begin,uint32_t high_end)
{
    memset(pcm,0,sizeof pcm);
    for(uint32_t i=high_begin;i<high_end;++i)pcm[2U*i+channel]=(int16_t)(i%2U ? 10000:-10000);
}
/** @brief 声道0的FillBlock快捷方式。 */
static void FillBlock(uint32_t high_begin,uint32_t high_end){FillBlockOn(0U,high_begin,high_end);}
/**
 * @brief 用逐帧参考提交与真实服务相同的已接受块，并把参考事件追加到期望序列。
 * @param first 块首绝对帧；session 会话；contiguous/gap_frames 流连续性；channel 选定声道。
 * @return 无返回值；参考拒绝或事件越界由CHECK终止。
 * @note 参考镜像服务current；仅在服务已提交的块上调用。
 */
static void ReferenceBlock(uint64_t first,uint32_t session,bool contiguous,uint32_t gap_frames,uint32_t channel)
{
    const bool boundary=ref.have_input && (!contiguous || gap_frames!=0U ||
        session!=ref.session || ref.last_frame==UINT64_MAX || first!=ref.last_frame+1U);
    if(boundary)(void)SoundDetector_Reset(&ref);
    for(uint32_t i=0U;i<BSP_AUDIO_HALF_FRAMES;++i)
    {
        SoundCandidate event;
        const SoundDetectorResult result=SoundDetector_Push(&ref,session,first+i,pcm[2U*i+channel],&event);
        if(result==SOUND_DETECTOR_EVENT)
        {
            CHECK(expected_count<16U);expected[expected_count++]=event;
        }
        else CHECK(result==SOUND_DETECTOR_NONE);
    }
}
/** @brief 从候选行按key读取一个uint64字段；缺失字段由CHECK终止。 */
static uint64_t FieldU64(const char *line,const char *key)
{
    const char *found=strstr(line,key);CHECK(found!=NULL);
    return strtoull(found+strlen(key),NULL,10);
}
/** @brief 按字段比较一条已发布候选与期望候选，不读取填充字节。 */
static void SameCandidate(const char *line,const SoundCandidate *event)
{
    CHECK(FieldU64(line,",SESSION=")==event->session);
    CHECK(FieldU64(line,",CH=")==test_channel);
    CHECK(FieldU64(line,",FIRST=")==event->onset_frame);
    CHECK(FieldU64(line,",CONFIRMED=")==event->confirmed_frame);
    CHECK(FieldU64(line,",E_AC=")==event->mean_square_ac);
    CHECK(FieldU64(line,",PEAK=")==event->peak_abs);
    CHECK(FieldU64(line,",CLIP=")==(event->clipped ? 1U:0U));
}
/** @brief 运行一次Stage并断言窗口64整块走批量入口，返回Stage结果。 */
static bool StageBlock(uint64_t first,uint32_t session,bool contiguous,uint32_t gap_frames,uint32_t completed_ms)
{
    const BSP_AudioBlock block={.samples=pcm,.session=session,.frame_count=BSP_AUDIO_HALF_FRAMES,
        .first_frame=first,.completed_ms=completed_ms,.index_valid=true,.contiguous=contiguous,
        .gap_frames=gap_frames};
    push_calls=batch_calls=0U;counting=1;
    const bool staged=SoundDetectorService_Stage(completed_ms,&block);
    counting=0;
    CHECK(push_calls==0U && batch_calls>0U);
    return staged;
}
/** @brief 在给定时刻推进4次Process以发布排队候选；session同步到新会话，无返回值。 */
static void Drain(uint32_t now,uint32_t session)
{
    health.session=session;
    for(unsigned i=0U;i<4U;++i)
    {
        health.last_callback_ms=now;
        SoundDetectorService_Process(now+i*250U,&health);
    }
}
/** @brief 比较全部已发布候选与参考事件。 */
static void CompareAll(void)
{
    CHECK(published_count==expected_count);
    for(unsigned i=0U;i<expected_count;++i)SameCandidate(candidate_lines[i],&expected[i]);
}
/** @brief 执行快速路径等价测试，成功返回0。 */
int main(void)
{
    Calibrate();
    test_channel=0U;
    CHECK(SoundDetectorService_Init(0U,&config,0U,Log));
    CHECK(SoundDetector_Init(&ref,&config));
    health=(BSP_AudioSnapshot){.running=true,.session=1U,.callback_count=1U,.last_callback_ms=0U};
    /* 块0：初始安静后高能量，形成候选1；块1/2安静；块3高能量，形成候选2。 */
    FillBlock(1024U,4096U);CHECK(StageBlock(0U,1U,true,0U,0U));CHECK(SoundDetectorService_Complete(true));
    ReferenceBlock(0U,1U,true,0U,0U);Drain(250U,1U);
    FillBlock(0U,0U);CHECK(StageBlock(8192U,1U,true,0U,171U));CHECK(SoundDetectorService_Complete(true));
    ReferenceBlock(8192U,1U,true,0U,0U);Drain(421U,1U);
    FillBlock(0U,0U);CHECK(StageBlock(16384U,1U,true,0U,342U));CHECK(SoundDetectorService_Complete(true));
    ReferenceBlock(16384U,1U,true,0U,0U);Drain(592U,1U);
    FillBlock(640U,3000U);CHECK(StageBlock(24576U,1U,true,0U,513U));CHECK(SoundDetectorService_Complete(true));
    ReferenceBlock(24576U,1U,true,0U,0U);Drain(763U,1U);
    /* 新会话/不连续块必须复位后同语义处理，形成候选3。 */
    FillBlock(1024U,4096U);CHECK(StageBlock(32768U,2U,false,0U,684U));CHECK(SoundDetectorService_Complete(true));
    ReferenceBlock(32768U,2U,false,0U,0U);Drain(934U,2U);
    CompareAll();
    SoundDetectorServiceSnapshot snap;SoundDetectorService_GetSnapshot(&snap);
    CHECK(snap.candidates==expected_count && snap.dropped==0U && snap.errors==0U);
    CHECK(snap.rejected_blocks==0U && snap.valid_blocks==5U && snap.pending==0U);
    CHECK(snap.armed==ref.armed);
    const unsigned channel0_events=expected_count;
    /* 声道1：同一批量Stage对交错PCM另一声道的候选/质量一致。 */
    expected_count=published_count=0U;test_channel=1U;
    CHECK(SoundDetectorService_Init(0U,&config,1U,Log));
    CHECK(SoundDetector_Init(&ref,&config));
    health=(BSP_AudioSnapshot){.running=true,.session=1U,.callback_count=1U,.last_callback_ms=0U};
    FillBlockOn(1U,1024U,4096U);
    CHECK(StageBlock(0U,1U,true,0U,0U));CHECK(SoundDetectorService_Complete(true));
    ReferenceBlock(0U,1U,true,0U,1U);Drain(250U,1U);CompareAll();
    SoundDetectorService_GetSnapshot(&snap);
    CHECK(snap.candidates==expected_count && snap.dropped==0U && snap.errors==0U && snap.rejected_blocks==0U);
    const unsigned channel1_events=expected_count;
    /* 窗口64的End失败：Stage可暂算，但Complete(false)拒绝且不发布候选。 */
    expected_count=published_count=0U;
    CHECK(SoundDetectorService_Init(0U,&config,1U,Log));
    CHECK(SoundDetector_Init(&ref,&config));
    health=(BSP_AudioSnapshot){.running=true,.session=1U,.callback_count=1U,.last_callback_ms=0U};
    FillBlockOn(1U,1024U,4096U);
    CHECK(StageBlock(0U,1U,true,0U,0U));CHECK(!SoundDetectorService_Complete(false));
    SoundDetectorService_GetSnapshot(&snap);
    CHECK(snap.rejected_blocks==1U && snap.candidates==0U && snap.pending==0U && published_count==0U);
    printf("PASS batch service: channel0 events=%u and channel1 events=%u identical; window64 rejected End no publish; 0 per-frame pushes\n",
        channel0_events,channel1_events);
    return 0;
}
