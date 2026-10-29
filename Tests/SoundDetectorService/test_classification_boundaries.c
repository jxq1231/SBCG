/**
 * @file        test_classification_boundaries.c
 * @brief       用真实能量检测器和分类器验证跨半块分类及End事务。
 * @details     通过链接包装仅观察分类输入，真实分类算法仍执行；TOA替身记录原窗口契约。
 * @note        合成PCM、会话及End结果，不访问硬件，不代替实物声学验收。
 */
#include "Detector/sound_detector_service.h"
#include "Detector/event_classifier.h"
#include "Detector/sound_event_time.h"
#include "Detector/toa_estimator.h"
#include "BSP/bsp_console.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define CLASS_FRAMES 2400U
static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS],chirp[CLASS_FRAMES];
static BSP_AudioSnapshot health;
static SoundDetector reference;
static SoundCandidate expected,last_event;
static SoundToaPosition last_toa;
static uint64_t chirp_first;
static uint32_t session;
static unsigned selected_channel,submits,logs,classifications,toa_calls,invalidations;
static uint64_t submitted_onsets[8];
static uint32_t submitted_sequences[8];
static bool borrowed,probe,expected_toa,queue_signal;
const ToaBackwardConfig ToaBackward_DefaultConfig={256U,128U,8U,4U};

/** @brief 固定主机HAL时钟；无参数，返回0，不影响显式测试毫秒。 */
uint32_t BSP_Console_GetTickMs(void){return 0U;}
/** @brief 从全局合成时间轴读取单声道帧，chirp区间以外为0；返回PCM16幅值。 */
static int16_t Signal(uint64_t frame)
{
    if(queue_signal){
        const bool high=(frame>=9216U && frame<9472U) || (frame>=12288U && frame<12544U) ||
            (frame>=16092U && frame<16512U) || (frame>=17408U && frame<17664U) ||
            (frame>=20480U && frame<20736U) || (frame>=24284U && frame<24576U);
        return high ? (int16_t)(frame%2U ? 10000:-10000):0;
    }
    return frame>=chirp_first && frame-chirp_first<CLASS_FRAMES ? chirp[frame-chirp_first]:0;
}
/** @brief 合成与既有分类器测试一致的50ms升频chirp，无参数/返回值。 */
static void MakeChirp(void)
{
    double phase=0.0;
    for(unsigned i=0U;i<CLASS_FRAMES;++i){
        const double frequency=2000.0+4000.0*(double)i/(double)CLASS_FRAMES;
        phase+=2.0*3.14159265358979323846*frequency/48000.0;
        const double envelope=i<48U ? (double)i/48.0:
            (i>2352U ? (double)(CLASS_FRAMES-i)/48.0:1.0);
        chirp[i]=(int16_t)(sin(phase)*envelope*12000.0);
    }
}
/** @brief 核对完整2400帧与独立时间轴逐点相同；samples为调用内借用，stride单位为int16元素。 */
static void CheckWindow(const int16_t *samples,uint16_t stride,uint16_t frames)
{
    CHECK(borrowed && samples!=NULL);
    if(!probe)return;
    CHECK(expected.session!=0U && frames==CLASS_FRAMES);
    for(unsigned i=0U;i<CLASS_FRAMES;++i)
        CHECK(samples[(size_t)i*stride]==Signal(expected.onset_frame+i));
    ++classifications;
}
bool __real_EventClassifier_Classify(const int16_t *,uint16_t,EventClassResult *);
bool __real_EventClassifier_ClassifyStrided(const int16_t *,uint16_t,uint16_t,EventClassResult *);
/** @brief 观察连续分类窗后执行真实分类器；各参数保持原接口所有权，返回真实结果。 */
bool __wrap_EventClassifier_Classify(const int16_t *samples,uint16_t frames,EventClassResult *result)
{CheckWindow(samples,1U,frames);return __real_EventClassifier_Classify(samples,frames,result);}
/** @brief 观察交错分类窗后执行真实分类器；各参数保持原接口所有权，返回真实结果。 */
bool __wrap_EventClassifier_ClassifyStrided(const int16_t *samples,uint16_t stride,
                                          uint16_t frames,EventClassResult *result)
{CheckWindow(samples,stride,frames);return __real_EventClassifier_ClassifyStrided(samples,stride,frames,result);}
/** @brief TOA仅允许原借用期间的完整256点窗；输出9.25帧，禁止分类缓存触发归还后重算。 */
bool ToaEstimator_EstimateBackward(const ToaBackwardConfig *config,const int16_t *samples,double *out)
{
    CHECK(borrowed && config==&ToaBackward_DefaultConfig && samples!=NULL && out!=NULL);
    ++toa_calls;*out=9.25;return true;
}
bool ToaEstimator_EstimateCandidates(const ToaBackwardConfig *config,const int16_t *samples,
                                     ToaBackwardCandidates *out)
{
    (void)config;(void)samples;if(out!=NULL)out->count=0U;return false;
}
/** @brief 记录归还后唯一标量交接；参数立即复制，返回true。 */
bool SoundEventTime_SubmitToa(uint32_t now,uint32_t sequence,const SoundCandidate *event,
                             const SoundToaPosition *toa)
{
    (void)now;CHECK(!borrowed && sequence!=0U && event!=NULL && toa!=NULL);
    CHECK(submits<8U);submitted_onsets[submits]=event->onset_frame;
    submitted_sequences[submits]=sequence;
    ++submits;last_event=*event;last_toa=*toa;return true;
}
/** @brief 记录流失效原因；不访问PCM，无返回值。 */
void SoundEventTime_Invalidate(SoundTimeReset reason)
{CHECK(reason==SOUND_TIME_STREAM || reason==SOUND_TIME_CLOCK);++invalidations;}
/** @brief 验证日志在归还后输出；文本仅当次借用，无返回值。 */
static void Log(const char *text,int length)
{CHECK(!borrowed && text!=NULL && length>1 && length<256);if(strncmp(text,"$SOUND_CAND,",12)==0)++logs;}
/** @brief 返回服务状态副本，不访问PCM。 */
static SoundDetectorServiceSnapshot Snap(void)
{SoundDetectorServiceSnapshot result;SoundDetectorService_GetSnapshot(&result);return result;}
/** @brief 初始化独立检测参考、服务和选定声道；phase为目标块内chirp起点。 */
static void Setup(unsigned phase,unsigned channel)
{
    borrowed=false;probe=true;queue_signal=false;session=1U;selected_channel=channel;
    submits=logs=classifications=toa_calls=invalidations=0U;expected_toa=false;
    expected=(SoundCandidate){0};last_event=(SoundCandidate){0};last_toa=(SoundToaPosition){0};
    chirp_first=BSP_AUDIO_HALF_FRAMES+phase;
    health=(BSP_AudioSnapshot){.running=true,.session=1U,.callback_count=1U};
    CHECK(SoundDetector_Init(&reference,&SoundDetectorService_DefaultConfig));
    CHECK(SoundDetectorService_Init(0U,&SoundDetectorService_DefaultConfig,(uint8_t)channel,Log));
}
/** @brief 填充一块时间轴PCM；另一声道写不同常数以识别声道串用，无返回值。 */
static void Fill(uint64_t first)
{
    for(unsigned i=0U;i<BSP_AUDIO_HALF_FRAMES;++i){
        pcm[2U*i+selected_channel]=Signal(first+i);
        pcm[2U*i+1U-selected_channel]=-1234;
    }
}
/** @brief 独立逐帧参考消费当前块，记录原能量候选与原TOA可得性，无返回值。 */
static void Reference(uint64_t first)
{
    for(unsigned i=0U;i<BSP_AUDIO_HALF_FRAMES;++i){
        SoundCandidate event;
        const SoundDetectorResult result=SoundDetector_Push(&reference,session,first+i,
            pcm[2U*i+selected_channel],&event);
        if(result==SOUND_DETECTOR_EVENT){
            CHECK(expected.session==0U);expected=event;
            const uint64_t anchor=event.onset_frame>64U ? event.onset_frame-64U:0U;
            (void)anchor;expected_toa=true; /* TOA跨块在下一连续块补齐 */
        }
        else CHECK(result==SOUND_DETECTOR_NONE);
    }
}
/**
 * @brief 在模拟借用内Stage，模拟End擦除源PCM后才Complete。
 * @param first 本块绝对首帧；now 本块完成毫秒；accepted 真实End注入结果。
 * @param contiguous/gap 模拟流连续性；reference_input 是否推进独立参考。
 * @return Complete真实结果；End后源缓冲已填不同值，后续结果不得依赖其指针。
 */
static bool Deliver(uint64_t first,uint32_t now,bool accepted,bool contiguous,uint64_t gap,
                    bool reference_input)
{
    Fill(first);if(reference_input)Reference(first);
    const BSP_AudioBlock block={.samples=pcm,.session=session,.frame_count=BSP_AUDIO_HALF_FRAMES,
        .first_frame=first,.completed_ms=now,.index_valid=true,.contiguous=contiguous,.gap_frames=gap};
    health.session=session;health.last_callback_ms=now;++health.callback_count;
    borrowed=true;CHECK(SoundDetectorService_Stage(now,&block));
    const unsigned before=submits;SoundDetectorService_Process(now+250U,&health);CHECK(submits==before);
    borrowed=false;memset(pcm,0xA5,sizeof pcm);
    const bool completed=SoundDetectorService_Complete(accepted);CHECK(completed==accepted);
    memset(pcm,0x5A,sizeof pcm);return completed;
}
/** @brief 检查真实分类等于完整参考窗、原能量字段和TOA契约；无返回值。 */
static void CheckEvent(void)
{
    CHECK(submits==1U && logs==1U && classifications==1U);
    CHECK(last_event.session==expected.session && last_event.onset_frame==expected.onset_frame);
    CHECK(last_event.confirmed_frame==expected.confirmed_frame &&
        last_event.mean_square_ac==expected.mean_square_ac && last_event.peak_abs==expected.peak_abs);
    int16_t window[CLASS_FRAMES];
    for(unsigned i=0U;i<CLASS_FRAMES;++i)window[i]=Signal(expected.onset_frame+i);
    EventClassResult result;CHECK(__real_EventClassifier_Classify(window,CLASS_FRAMES,&result));
    CHECK(result.type==EVENT_CLASS_CHIRP && result.quality>=80U);
    CHECK(last_event.event_class==result.type && last_event.quality==result.quality);
    CHECK(last_toa.status==SOUND_TOA_VALID);
    CHECK(toa_calls==1U);
}
/** @brief 验证一个相位/声道，必要时等待唯一后继块；无返回值。 */
static void RunPhase(unsigned phase,unsigned channel)
{
    Setup(phase,channel);CHECK(Deliver(0U,0U,true,true,0U,true));
    CHECK(Deliver(8192U,171U,true,true,0U,true));
    SoundDetectorService_Process(300U,&health);
    if(expected.session==0U || expected.onset_frame+CLASS_FRAMES>16384U){
        CHECK(submits==0U && logs==0U);
        CHECK(Deliver(16384U,342U,true,true,0U,true));
        SoundDetectorService_Process(592U,&health);
    }
    CheckEvent();
}
/** @brief 拒绝初块/补块，均不得发布；其后新会话可重新获取完整分类，无返回值。 */
static void TestRejected(void)
{
    for(unsigned failed=0U;failed<2U;++failed){
        Setup(7500U,failed);CHECK(Deliver(0U,0U,true,true,0U,true));
        const bool first_ok=failed!=0U;
        CHECK(Deliver(8192U,171U,first_ok,true,0U,true)==first_ok);
        if(first_ok){CHECK(Snap().pending==1U);CHECK(!Deliver(16384U,342U,false,true,0U,true));}
        SoundDetectorService_Process(592U,&health);
        CHECK(submits==0U && logs==0U && Snap().pending==0U && Snap().rejected_blocks==1U);
        CHECK(!Snap().armed && invalidations!=0U);
        session=2U;chirp_first=40960U+1024U;expected=(SoundCandidate){0};
        classifications=toa_calls=0U;expected_toa=false;
        CHECK(SoundDetector_Reset(&reference));
        CHECK(Deliver(32768U,700U,true,false,0U,true));
        CHECK(Deliver(40960U,871U,true,true,0U,true));
        SoundDetectorService_Process(1121U,&health);CheckEvent();
        CHECK(Snap().rejected_blocks==1U && last_event.session==2U);
    }
    puts("PASS classification rejected: first/continuation End failure, new-session recovery");
}
/** @brief gap/session/显式不连续及非法Stage清等待项，不得拼接不同流PCM；无返回值。 */
static void TestBoundaries(void)
{
    for(unsigned fault=0U;fault<4U;++fault){
        Setup(7500U,0U);CHECK(Deliver(0U,0U,true,true,0U,true));
        CHECK(Deliver(8192U,171U,true,true,0U,true));CHECK(Snap().pending==1U);
        probe=false;
        if(fault==3U){
            const BSP_AudioBlock bad={.samples=NULL};
            borrowed=true;CHECK(!SoundDetectorService_Stage(342U,&bad));borrowed=false;
            CHECK(!SoundDetectorService_Complete(true));
        }else{
            if(fault==1U){session=2U;health.session=2U;}
            CHECK(Deliver(fault==0U ? 16385U:16384U,342U,true,fault==1U,
                fault==0U ? 1U:0U,false));
        }
        SoundDetectorService_Process(592U,&health);
        CHECK(submits==0U && logs==0U && Snap().pending==0U && Snap().dropped==1U);
    }
    puts("PASS classification boundaries: gap/session/discontinuity/invalid Stage purge pending");
}
/** @brief 不完整队头不发布；即使流快照健康也在原始完成时间满1s时明确丢弃，无返回值。 */
static void TestExpiry(void)
{
    Setup(7500U,0U);CHECK(Deliver(0U,0U,true,true,0U,true));
    CHECK(Deliver(8192U,171U,true,true,0U,true));
    health.last_callback_ms=1170U;SoundDetectorService_Process(1170U,&health);
    CHECK(submits==0U && logs==0U && Snap().pending==1U && Snap().dropped==0U);
    health.last_callback_ms=1171U;SoundDetectorService_Process(1171U,&health);
    CHECK(submits==0U && logs==0U && Snap().pending==0U && Snap().dropped==1U);
    puts("PASS classification expiry: unready head blocks publication and expires after original 1s");
}
/** @brief 多候选跨块后保持FIFO、环回容量4和原sequence/丢弃计数；无返回值。 */
static void TestQueue(void)
{
    Setup(0U,0U);probe=false;queue_signal=true;
    const SoundDetectorConfig config={64U,256U,128U,2U,2U,0U};
    CHECK(SoundDetectorService_Init(0U,&config,0U,Log));
    CHECK(Deliver(0U,0U,true,true,0U,false));
    CHECK(Deliver(8192U,171U,true,true,0U,false));
    CHECK(Snap().candidates==3U && Snap().pending==3U && Snap().dropped==0U);
    SoundDetectorService_Process(300U,&health);CHECK(submits==1U && Snap().pending==2U);
    SoundDetectorService_Process(301U,&health);CHECK(submits==1U);
    CHECK(Deliver(16384U,342U,true,true,0U,false));
    CHECK(Snap().candidates==6U && Snap().pending==4U && Snap().dropped==1U);
    for(unsigned i=0U;i<4U;++i){
        const uint32_t now=550U+i*250U;health.last_callback_ms=now;
        SoundDetectorService_Process(now,&health);
    }
    const uint64_t onsets[]={9216U,12288U,16064U,17408U,20480U};
    CHECK(submits==5U && logs==5U && Snap().pending==0U && Snap().dropped==1U);
    for(unsigned i=0U;i<5U;++i)
        CHECK(submitted_onsets[i]==onsets[i] && submitted_sequences[i]==i+1U);
    puts("PASS classification queue: pending continuation, FIFO, ring wrap, capacity4 and exact drop/sequence");
}
/** @brief 按场景运行；无参数运行边界组，sweep遍历两声道全部8192种相位，成功0。 */
int main(int argc,char **argv)
{
    MakeChirp();const char *scenario=argc==2 ? argv[1]:"boundaries";
    if(strcmp(scenario,"sweep")==0){
        for(unsigned channel=0U;channel<2U;++channel)
            for(unsigned phase=0U;phase<8192U;++phase)RunPhase(phase,channel);
        puts("PASS classification phase sweep: 8192 phases x 2 channels; exact 2400-frame windows");
    }else if(strcmp(scenario,"boundaries")==0){
        const unsigned phases[]={0U,5792U,5793U,7500U,7808U,8064U,8128U,8191U};
        for(unsigned channel=0U;channel<2U;++channel)
            for(unsigned i=0U;i<sizeof phases/sizeof phases[0];++i)RunPhase(phases[i],channel);
        TestRejected();TestBoundaries();TestExpiry();TestQueue();
        puts("PASS classification boundaries: complete waveform/quality, selected channels, PCM ownership and old TOA");
    }else{fprintf(stderr,"Unknown scenario: %s\n",scenario);return 2;}
    return 0;
}
