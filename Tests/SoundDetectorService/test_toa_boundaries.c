/**
 * @file        test_toa_boundaries.c
 * @brief       验证真实检测服务在块内/跨块完整256帧窗口才调用TOA估算器。
 * @details     真实能量检测器生成候选；估算器替身记录调用，验证块尾/跨块由已提交尾部
 *              和后继块补齐，同时验证End拒绝、候选过期及后续恢复；
 *              完整窗另覆盖名义微秒越界/非有限数拒绝及长十进制日志不损坏帧号。
 * @note        纯主机测试，不链接真实估算器；分类允许CPU尾部拼窗，TOA仍不跨块重组或补零。
 */
#include "Detector/sound_detector_service.h"
#include "Detector/toa_estimator.h"
#include "BSP/bsp_console.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief 条件失败时报告行号并终止；condition只求值一次，无返回值。 */
#define CHECK(condition) do{if(!(condition)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#condition);exit(1);}}while(0)

static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS];
static int16_t expected_window[256];
static BSP_AudioSnapshot health;
static unsigned estimator_calls,candidate_logs,selected_channel;
static bool borrowed,check_window;
static char last_candidate[256];
static double estimator_fraction;

/** @brief 与真实估算器相同的默认窗口契约；此可执行只替换估算器调用边界。 */
const ToaBackwardConfig ToaBackward_DefaultConfig={256U,128U,8U,4U};

/**
 * @brief 记录服务对估算器的调用，完整窗用逐样本对照验证提取和声道。
 * @param config 服务传入的非NULL默认配置，须为256帧契约。
 * @param samples 借用的非NULL单声道窗；仅check_window为true时读取256点。
 * @param out_fraction 非NULL输出，写入测试设定的帧偏移；默认9.25，可注入非法数值。
 * @return 始终true；不完整窗测试只观察错误调用，不读取可能未初始化的尾部。
 * @note 替身使旧服务的边界错误确定性失败，不依赖栈中偶然残值或真实前沿算法。
 */
bool ToaEstimator_EstimateBackward(const ToaBackwardConfig *config,
    const int16_t *samples,double *out_fraction)
{
    ++estimator_calls;
    CHECK(config==&ToaBackward_DefaultConfig && config->window_frames==256U);
    CHECK(samples!=NULL && out_fraction!=NULL);
    if(check_window)
        for(unsigned i=0U;i<256U;++i)CHECK(samples[i]==expected_window[i]);
    *out_fraction=estimator_fraction;
    return true;
}
bool ToaEstimator_EstimateCandidates(const ToaBackwardConfig *config,
    const int16_t *samples,ToaBackwardCandidates *out)
{
    (void)config;(void)samples;if(out!=NULL)out->count=0U;return false;
}

/** @brief 提供固定HAL毫秒读数；无参数，返回0，不访问实物。 */
uint32_t BSP_Console_GetTickMs(void){return 0U;}

/**
 * @brief 检查归还后完整日志并复制候选文本。
 * @param text 回调期间借用的非NULL日志，返回前复制候选。
 * @param length 日志字节数，须包含结尾CRLF且小于256。
 * @return 无返回值；借用期发布或格式错误时终止测试。
 */
static void Log(const char *text,int length)
{
    CHECK(!borrowed && length>1 && length<256);
    CHECK(text[length-2]=='\r' && text[length-1]=='\n');
    if(strncmp(text,"$SOUND_CAND,",12)!=0)return;
    memcpy(last_candidate,text,(size_t)length);last_candidate[length]='\0';
    ++candidate_logs;
}

/** @brief 复制服务诊断；无参数，返回测试独占的状态副本。 */
static SoundDetectorServiceSnapshot Snap(void)
{SoundDetectorServiceSnapshot snapshot;SoundDetectorService_GetSnapshot(&snapshot);return snapshot;}

/**
 * @brief 清零测试状态并初始化真实服务的默认64帧能量门控。
 * @param channel 选定输入声道0或1。
 * @return 无返回值；不启动硬件，不保留外部PCM。
 */
static void Setup(unsigned channel)
{
    borrowed=check_window=false;estimator_calls=candidate_logs=0U;estimator_fraction=9.25;
    selected_channel=channel;last_candidate[0]='\0';memset(pcm,0,sizeof pcm);
    health=(BSP_AudioSnapshot){.running=true,.session=1U,.callback_count=1U};
    CHECK(SoundDetectorService_Init(0U,&SoundDetectorService_DefaultConfig,(uint8_t)channel,Log));
}

/**
 * @brief 在当前合成块选定声道写交替高能量，另一声道写不同值以检验声道提取。
 * @param begin 包含的块内首帧。
 * @param end 不包含的块内末帧，调用方保证不超过8192。
 * @return 无返回值；其余帧保持调用前内容。
 */
static void Tone(unsigned begin,unsigned end)
{
    for(unsigned i=begin;i<end;++i){
        pcm[2U*i+selected_channel]=(int16_t)(i%2U ? 10000:-10000);
        pcm[2U*i+1U-selected_channel]=-1234;
    }
}

/**
 * @brief 从测试已知完整的源PCM准备估算器逐点期望。
 * @param local_anchor 块内窗起点，调用方保证其后至少256帧可用。
 * @return 无返回值；后续估算器调用须交付完全相同的256点。
 */
static void ExpectWindow(unsigned local_anchor)
{
    for(unsigned i=0U;i<256U;++i)
        expected_window[i]=pcm[2U*(local_anchor+i)+selected_channel];
    check_window=true;
}

/**
 * @brief 在模拟唯一借用中暂存有效块，检查借用期不发布，再提交真实注入的End结果。
 * @param first_frame 当前块首绝对帧。
 * @param completed_ms 当前块完成的HAL毫秒，兼作Stage当前时间。
 * @param accepted 模拟BSP End真实结果，false时要求服务拒绝。
 * @return 无返回值；不保存描述符指针，PCM在Stage返回后不再由服务读取。
 */
static void Deliver(uint64_t first_frame,uint32_t completed_ms,bool accepted)
{
    const BSP_AudioBlock block={.samples=pcm,.frame_count=BSP_AUDIO_HALF_FRAMES,
        .session=1U,.first_frame=first_frame,.completed_ms=completed_ms,
        .index_valid=true,.contiguous=true};
    health.last_callback_ms=completed_ms;
    const unsigned before=candidate_logs;
    borrowed=true;CHECK(SoundDetectorService_Stage(completed_ms,&block));
    SoundDetectorService_Process(completed_ms+250U,&health);CHECK(candidate_logs==before);
    borrowed=false;CHECK(SoundDetectorService_Complete(accepted)==accepted);
}

/** @brief 用连续静默后继块补全分类，first为后继首帧；仅分类解锁，禁止重算原TOA，无返回值。 */
static void ContinueClassification(uint64_t first)
{
    SoundDetectorService_Process(100U,&health);CHECK(candidate_logs==0U);
    memset(pcm,0,sizeof pcm);Deliver(first,170U,true);
}

/**
 * @brief 输出调用次数和候选，便于RED/GREEN证据确认失败确由估算器错误调用导致。
 * @param label 当前测试情形的非NULL短名。
 * @return 无返回值；只读服务快照，不改变待发布队列。
 */
static void Trace(const char *label)
{
    const SoundDetectorServiceSnapshot snapshot=Snap();
    printf("TRACE %s calls=%u candidates=%u toa_ok=%u toa_miss=%u logs=%u\n%s",
        label,estimator_calls,(unsigned)snapshot.candidates,(unsigned)snapshot.toa_ok,
        (unsigned)snapshot.toa_miss,candidate_logs,last_candidate);
}

/** @brief 块尾onset8064需anchor8000，只有192帧，候选保留但估算器不得调用；无参数/返回值。 */
static void TestPartialTail(void)
{
    Setup(0U);Tone(8064U,8192U);Deliver(0U,0U,true);
    ContinueClassification(8192U);
    SoundDetectorService_Process(250U,&health);Trace("partial-tail");
    CHECK(estimator_calls==1U);
    CHECK(Snap().candidates==1U && Snap().toa_ok==1U && Snap().toa_miss==0U);
    CHECK(candidate_logs==1U && strstr(last_candidate,",FIRST=8064,CONFIRMED=8191,")!=NULL);
    CHECK(strstr(last_candidate,",TOA_US=NA\r\n")==NULL);
    puts("PASS partial-tail: right window completed from next block; no missing TOA");
}

/** @brief onset8000恰好留完整256帧，两声道都应原样交付默认估算器；无参数/返回值。 */
static void TestCompleteBoundary(void)
{
    for(unsigned channel=0U;channel<2U;++channel){
        Setup(channel);Tone(8000U,8192U);ExpectWindow(7936U);Deliver(0U,0U,true);
        ContinueClassification(8192U);
        SoundDetectorService_Process(250U,&health);Trace("complete-boundary");
        CHECK(estimator_calls==1U && Snap().toa_ok==1U && Snap().toa_miss==0U);
        CHECK(Snap().candidates==1U && candidate_logs==1U);
        CHECK(strstr(last_candidate,",FIRST=8000,CONFIRMED=8127,")!=NULL);
        CHECK(strstr(last_candidate,",TOA_US=165526\r\n")!=NULL);
    }
    puts("PASS complete-boundary: exact 256-frame window and selected channel preserved");
}

/** @brief onset8128跨块确认时anchor8064已不在当前借用，不能裁到块首伪造TOA；无参数/返回值。 */
static void TestCrossBlockLeft(void)
{
    Setup(0U);Tone(8128U,8192U);Deliver(0U,0U,true);
    CHECK(Snap().candidates==0U && estimator_calls==0U);
    memset(pcm,0,sizeof pcm);Tone(0U,256U);Deliver(8192U,170U,true);
    SoundDetectorService_Process(250U,&health);Trace("cross-block-left");
    CHECK(estimator_calls==1U);
    CHECK(Snap().candidates==1U && Snap().toa_ok==1U && Snap().toa_miss==0U);
    CHECK(candidate_logs==1U && strstr(last_candidate,",FIRST=8128,CONFIRMED=8255,")!=NULL);
    CHECK(strstr(last_candidate,",TOA_US=NA\r\n")==NULL);
    puts("PASS cross-block-left: committed tail + current block complete the TOA window");
}

/** @brief 完整窗暂算后End失败不发布，清布防历史，后续完整候选可重新估算；无参数/返回值。 */
static void TestRejectedAndRecovery(void)
{
    Setup(0U);Tone(8000U,8192U);ExpectWindow(7936U);Deliver(0U,0U,false);
    CHECK(estimator_calls==1U && Snap().rejected_blocks==1U && Snap().resets==1U);
    CHECK(Snap().candidates==0U && Snap().pending==0U && !Snap().armed);
    SoundDetectorService_Process(250U,&health);CHECK(candidate_logs==0U);
    memset(pcm,0,sizeof pcm);Tone(0U,512U);Tone(1024U,1280U);ExpectWindow(960U);
    Deliver(8192U,300U,true);SoundDetectorService_Process(550U,&health);Trace("rejected-recovery");
    CHECK(estimator_calls==2U && Snap().candidates==1U && candidate_logs==1U);
    CHECK(strstr(last_candidate,",FIRST=9216,CONFIRMED=9343,")!=NULL);
    CHECK(strstr(last_candidate,",TOA_US=190859\r\n")!=NULL);
    puts("PASS rejected-recovery: End failure publishes nothing; quiet rearm restores a later complete window");
}

/** @brief 不完整候选输出NA后，等待原24000帧冷却再验证完整候选不沿用失效状态；无参数/返回值。 */
static void TestPendingExpiry(void)
{
    Setup(0U);Tone(8064U,8192U);Deliver(0U,0U,true);
    SoundDetectorService_Process(1000U,&health);
    CHECK(estimator_calls==0U && candidate_logs==0U && Snap().pending==0U);
    CHECK(Snap().toa_ok==0U && Snap().toa_miss==0U);
    puts("PASS pending-expiry: no next block -> no fabricated TOA, candidate expired");
}


/** @brief 合法极大块首仍完成局部估算，但名义微秒超过uint64范围必须输出NA；无参数/返回值。 */
static void TestLargeFrameOverflow(void)
{
    Setup(0U);Tone(8000U,8192U);ExpectWindow(7936U);
    Deliver(UINT64_MAX-BSP_AUDIO_HALF_FRAMES,0U,true);
    SoundDetectorService_Process(250U,&health);Trace("large-frame-overflow");
    CHECK(estimator_calls==1U && Snap().toa_ok==1U && Snap().toa_miss==0U);
    CHECK(Snap().candidates==1U && candidate_logs==1U);
    CHECK(strstr(last_candidate,",TOA_US=NA\r\n")!=NULL);
    CHECK(strstr(last_candidate,",FIRST=18446744073709551423,CONFIRMED=18446744073709551550,")!=NULL);
    puts("PASS large-frame-overflow: local estimate succeeds; unrepresentable nominal microseconds emit NA");
}

/** @brief 可表示的17位名义微秒应完整输出且不破坏FIRST/CONFIRMED相邻日志；无参数/返回值。 */
static void TestLongDecimal(void)
{
    Setup(0U);Tone(8000U,8192U);ExpectWindow(7936U);
    Deliver(UINT64_C(480000000000000),0U,true);
    ContinueClassification(UINT64_C(480000000008192));
    SoundDetectorService_Process(250U,&health);Trace("long-decimal");
    CHECK(estimator_calls==1U && Snap().toa_ok==1U && Snap().toa_miss==0U);
    CHECK(Snap().candidates==1U && candidate_logs==1U);
    CHECK(strstr(last_candidate,",TOA_US=10000000000165526\r\n")!=NULL);
    CHECK(strstr(last_candidate,",FIRST=480000000008000,CONFIRMED=480000000008127,")!=NULL);
    puts("PASS long-decimal: representable 17-digit nominal time and original frame fields remain intact");
}

/**
 * @brief 在完整窗局部估算成功后注入负数、NaN、无穷和超范围值，验证发布前拒绝。
 * @return 无返回值；仅输出NA，不把序列化失败改记为局部估算失败。
 * @note NAN/INFINITY为编译期常量，测试与产品都不需要链接额外数学库。
 */
static void TestInvalidNumbers(void)
{
    const double fractions[]={-8000.0,-7936.001,NAN,INFINITY,-INFINITY,0x1p64};
    const char *labels[]={"negative-toa","negative-submicrosecond-toa","nan-toa",
        "positive-inf-toa","negative-inf-toa","too-large-toa"};
    for(unsigned i=0U;i<sizeof fractions/sizeof fractions[0];++i){
        Setup(0U);estimator_fraction=fractions[i];Tone(8000U,8192U);ExpectWindow(7936U);
        Deliver(0U,0U,true);ContinueClassification(8192U);
        SoundDetectorService_Process(250U,&health);Trace(labels[i]);
        CHECK(estimator_calls==1U && Snap().toa_ok==1U && Snap().toa_miss==0U);
        CHECK(Snap().candidates==1U && candidate_logs==1U);
        CHECK(strstr(last_candidate,",TOA_US=NA\r\n")!=NULL);
        CHECK(strstr(last_candidate,",FIRST=8000,CONFIRMED=8127,")!=NULL);
    }
    puts("PASS invalid-numbers: negative/nonfinite/overflow nominal time rejected; local success counters preserved");
}

/**
 * @brief 按可选情形名运行边界测试，允许RED逐项保留独立证据。
 * @param argc 无参数为1；指定一个情形名时为2。
 * @param argv 进程只读参数，借用至进程退出。
 * @return 成功0，未知情形2；断言失败由CHECK退出1。
 */
int main(int argc,char **argv)
{
    const char *scenario=argc==2 ? argv[1]:"all";
    bool matched=false;
    if(strcmp(scenario,"all")==0 || strcmp(scenario,"partial-tail")==0){TestPartialTail();matched=true;}
    if(strcmp(scenario,"all")==0 || strcmp(scenario,"complete-boundary")==0){TestCompleteBoundary();matched=true;}
    if(strcmp(scenario,"all")==0 || strcmp(scenario,"cross-block-left")==0){TestCrossBlockLeft();matched=true;}
    if(strcmp(scenario,"all")==0 || strcmp(scenario,"rejected-recovery")==0){TestRejectedAndRecovery();matched=true;}
    if(strcmp(scenario,"all")==0 || strcmp(scenario,"pending-expiry")==0){TestPendingExpiry();matched=true;}
    if(strcmp(scenario,"all")==0 || strcmp(scenario,"large-frame-overflow")==0){TestLargeFrameOverflow();matched=true;}
    if(strcmp(scenario,"all")==0 || strcmp(scenario,"long-decimal")==0){TestLongDecimal();matched=true;}
    if(strcmp(scenario,"all")==0 || strcmp(scenario,"invalid-numbers")==0){TestInvalidNumbers();matched=true;}
    if(!matched){fprintf(stderr,"Unknown boundary scenario: %s\n",scenario);return 2;}
    return 0;
}
