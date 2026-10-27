/** @file test_toa_handoff.c @brief 检测服务向事件时间服务交接固定窗口TOA的主机验证。 */
#include "Detector/sound_detector_service.h"
#include "Detector/toa_estimator.h"
#include "Detector/sound_event_time.h"
#include "BSP/bsp_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS];
static BSP_AudioSnapshot health;
static SoundDetectorConfig config={64U,256U,128U,2U,8U,24000U};
static unsigned submits,invalidations,logs;
static SoundToaPosition last_toa;
static SoundCandidate last_candidate;
static bool last_called;
const ToaBackwardConfig ToaBackward_DefaultConfig={256U,128U,8U,4U};
/** @brief 返回固定的窗口内9.25帧，验证服务拆分整数/Q16而非把绝对帧转double。 */
bool ToaEstimator_EstimateBackward(const ToaBackwardConfig *c,const int16_t *s,double *out)
{
    (void)c;(void)s;CHECK(out!=NULL);*out=9.25;return true;
}
/** @brief 记录服务交接的候选和固定TOA；无返回值、不读PCM。 */
bool SoundEventTime_SubmitToa(uint32_t now,uint32_t sequence,
                              const SoundCandidate *candidate,
                              const SoundToaPosition *position)
{
    (void)now;CHECK(sequence!=0U && candidate!=NULL && position!=NULL);
    last_called=true;++submits;last_candidate=*candidate;last_toa=*position;return true;
}
/** @brief 记录流/时钟失效；无返回值。 */
void SoundEventTime_Invalidate(SoundTimeReset reason)
{
    CHECK(reason==SOUND_TIME_CLOCK || reason==SOUND_TIME_STREAM);++invalidations;
}
/** @brief 固定HAL毫秒，主机测试不访问硬件。 */
uint32_t BSP_Console_GetTickMs(void){return 0U;}
/** @brief 只统计日志，不保存指针。 */
static void Log(const char *line,int length){CHECK(line!=NULL && length>0 && length<256);++logs;}
/** @brief 初始化服务、清空合成PCM和健康快照。 */
static void Setup(uint32_t now)
{
    submits=invalidations=logs=0U;last_called=false;memset(&last_toa,0,sizeof last_toa);
    memset(pcm,0,sizeof pcm);health=(BSP_AudioSnapshot){.running=true,.session=1U,.callback_count=1U,.last_callback_ms=now};
    CHECK(SoundDetectorService_Init(now,&config,0U,Log));
}
/** @brief 填一个完整高能量候选窗（onset=8000，anchor=7936）。 */
static void Tone(void)
{
    for(unsigned i=8000U;i<8192U;++i)pcm[2U*i]=(int16_t)(i%2U ? 10000:-10000);
}
/** @brief 用一个模拟半块完成Stage/End；accepted为真实End结果，返回Complete结果。 */
static bool Deliver(uint32_t now,uint64_t first,uint32_t frames,bool accepted)
{
    const BSP_AudioBlock block={.samples=pcm,.frame_count=frames,.session=1U,
        .first_frame=first,.completed_ms=now,.index_valid=true,.contiguous=true};
    health.last_callback_ms=now;
    if(!SoundDetectorService_Stage(now,&block))return false;
    return SoundDetectorService_Complete(accepted);
}
/** @brief 先确认候选仍等待完整分类，再用静默后继块解锁，原TOA位置不得重算；无返回值。 */
static void ContinueClassification(void)
{
    SoundDetectorService_Process(100U,&health);CHECK(!last_called && submits==0U);
    memset(pcm,0,sizeof pcm);CHECK(Deliver(170U,8192U,BSP_AUDIO_HALF_FRAMES,true));
}
int main(void)
{
    /* 完整窗End成功后提交候选；TOA=anchor7936+9.25 => frame7945/Q16=0.25。 */
    Setup(0U);Tone();CHECK(Deliver(0U,0U,BSP_AUDIO_HALF_FRAMES,true));
    ContinueClassification();
    SoundDetectorService_Process(250U,&health);
    CHECK(last_called && submits==1U);
    CHECK(last_candidate.onset_frame==8000U);
    CHECK(last_toa.status==SOUND_TOA_VALID);
    CHECK(last_toa.frame==7945U);
    CHECK(last_toa.fraction_q16==16384U);

    /* End失败不得产生成功TOA；旧候选也不得被提交。 */
    Setup(0U);Tone();CHECK(!Deliver(0U,0U,BSP_AUDIO_HALF_FRAMES,false));
    SoundDetectorService_Process(250U,&health);
    CHECK(!last_called && submits==0U && invalidations>0U);

    /* 块尾不足的右窗在下一连续块到达后由已提交尾部+当前块补齐，不伪造FIRST+1。 */
    Setup(0U);
    for(unsigned i=8064U;i<8192U;++i)pcm[2U*i]=(int16_t)(i%2U ? 10000:-10000);
    CHECK(Deliver(0U,0U,BSP_AUDIO_HALF_FRAMES,true));
    ContinueClassification();
    SoundDetectorService_Process(250U,&health);
    CHECK(last_called && submits==1U);
    CHECK(last_toa.status==SOUND_TOA_VALID);
    CHECK(last_candidate.onset_frame==8064U);
    CHECK(last_toa.frame==8009U && last_toa.fraction_q16==16384U);
    puts("PASS TOA handoff: complete window frame/Q16, End failure no submit, cross-block tail completed");
    return 0;
}
