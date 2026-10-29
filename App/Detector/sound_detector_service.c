/**
 * @file        sound_detector_service.c
 * @brief       在PCM唯一借用中暂存检测，归还验证成功后发布有界本地帧候选。
 * @details     工作副本隔离未验证PCM，固定两级4条队列；分类等待完整2400帧，失败/断流清历史。
 *              TOA仅使用当前借用块内完整256帧窗口；窗口不完整时保留能量候选并输出NA。
 *              名义微秒发布前检查数值及uint64范围，十进制缓冲可容纳20位及结尾字符。
 * @note        仅串行前台；双2400点CPU自有尾部缓存经End提交，不保留DMA指针，无新IRQ/DMA/PTP时间查询。
 */
#include "sound_detector_service.h"
#include "sound_event_time.h"
#include "event_classifier.h"
#include "BSP/bsp_console.h"
#include "toa_estimator.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define BLOCK_MAX_AGE_MS 250U
#define STREAM_MAX_AGE_MS 1000U
#define EVENT_LOG_PERIOD_MS 250U
#define STATE_LOG_PERIOD_MS 1000U
#define TOA_SEARCH_WINDOW 256U
#define TOA_LEFT_MARGIN 64U
#define AUDIO_RATE_HZ 48000U
#define CLASSIFY_FRAMES 2400U
const SoundDetectorConfig SoundDetectorService_DefaultConfig={64U,256U,128U,2U,8U,24000U};
typedef struct { SoundCandidate event; uint32_t completed_ms,sequence;
                 uint64_t toa_anchor; double toa_fraction; bool toa_valid,toa_ready;
                 SoundToaPosition toa; bool class_ready; } QueuedCandidate;
static SoundDetector current,working;
static SoundDetectorServiceSnapshot diagnostics;
static SoundCandidate staged[SOUND_DETECTOR_QUEUE_CAPACITY];
static QueuedCandidate queue[SOUND_DETECTOR_QUEUE_CAPACITY];
static QueuedCandidate queue_working[SOUND_DETECTOR_QUEUE_CAPACITY];
/* CPU独占，不与DMA共享；启动不清NOLOAD数据，有效标记只在整尾部写满且End成功后提交。 */
static int16_t sound_class_pcm[2][CLASSIFY_FRAMES]
    __attribute__((section(".SoundClassSection"),aligned(32)));
static uint64_t class_first_frame,staged_class_first_frame;
static uint32_t class_session,staged_class_session;
static uint8_t class_committed_slot;
static bool class_history_valid;
static bool staged_class_ready[SOUND_DETECTOR_QUEUE_CAPACITY];
_Static_assert(sizeof sound_class_pcm==9600U,"classification PCM reservation must be 9600 bytes");
static double staged_toa[SOUND_DETECTOR_QUEUE_CAPACITY];
static uint64_t staged_toa_anchor[SOUND_DETECTOR_QUEUE_CAPACITY];
static bool staged_toa_ok[SOUND_DETECTOR_QUEUE_CAPACITY];
static bool staged_toa_ready[SOUND_DETECTOR_QUEUE_CAPACITY];
static SoundToaPosition staged_toa_position[SOUND_DETECTOR_QUEUE_CAPACITY];
static uint32_t staged_total,staged_ms,last_event_ms,last_state_ms;
static uint8_t staged_count,head,count;
static bool active,stage_valid,boundary;
static void (*emit_line)(const char *,int);
#define TOA_CANDIDATE_CACHE 8U
static struct { uint32_t sequence; SoundDetectorServiceToaCandidates data; } toa_cache[TOA_CANDIDATE_CACHE];
static uint8_t toa_cache_next;
/** @brief 保存最近发布候选的TOA齿偏移；仅前台，环形覆盖最旧项。 */
static void __attribute__((unused)) CacheToaCandidates(uint32_t sequence,
    const SoundToaPosition *position)
{
    if(position==NULL)return;
    toa_cache[toa_cache_next].sequence=sequence;
    SoundDetectorServiceToaCandidates *out=&toa_cache[toa_cache_next].data;
    out->count=position->candidate_count;
    for(uint8_t i=0U;i<4U;++i)out->offset_q16[i]=position->candidate_offset_q16[i];
    toa_cache_next=(uint8_t)((toa_cache_next+1U)%TOA_CANDIDATE_CACHE);
}
bool SoundDetectorService_GetToaCandidates(uint32_t sequence,
    SoundDetectorServiceToaCandidates *out)
{
    if(out==NULL||sequence==0U){if(out!=NULL)out->count=0U;return false;}
    out->count=0U;
    for(uint8_t i=0U;i<TOA_CANDIDATE_CACHE;++i){
        if(toa_cache[i].sequence==sequence){*out=toa_cache[i].data;return true;}
    }
    return false;
}

/** @brief 饱和增加计数；value为非NULL可写计数、amount为增加条数，无返回值，仅前台。 */
static void Add(uint32_t *value,uint32_t amount)
{ *value=UINT32_MAX-*value<amount ? UINT32_MAX:*value+amount; }
/** @brief 将uint64数值value转为output至少21字节缓冲的十进制字符串，无返回值；避免nano的64位printf。 */
static void Decimal(uint64_t value,char output[21])
{
    char reverse[20];unsigned length=0U;
    do { reverse[length++]=(char)('0'+value%10U);value/=10U; } while(value!=0U);
    for(unsigned i=0U;i<length;++i)output[i]=reverse[length-i-1U];
    output[length]='\0';
}
/** @brief 发送已完成格式化的临时line；length为snprintf长度，无返回值；回调须返回前消费，禁止截断行。 */
static void Emit(const char *line,int length)
{
    if(emit_line!=NULL && length>0 && length<256)emit_line(line,length);
    else if(emit_line!=NULL)Add(&diagnostics.errors,1U);
}
/** @brief 丢弃已验证但尚未发送的队列并记录损失；无参数，无返回值，不访问PCM。 */
static void DropQueue(void)
{ Add(&diagnostics.dropped,count);head=count=0U; }
/** @brief 记录Stage入口begin_ms到当前的饱和最大HAL毫秒耗时，无返回值。 */
static void RecordStageTime(uint32_t begin_ms)
{ const uint32_t elapsed=BSP_Console_GetTickMs()-begin_ms;
  if(elapsed>diagnostics.max_stage_ms)diagnostics.max_stage_ms=elapsed; }
/** @brief 记录Process入口begin_ms到当前的饱和最大HAL毫秒耗时，无返回值。 */
static void RecordProcessTime(uint32_t begin_ms)
{ const uint32_t elapsed=BSP_Console_GetTickMs()-begin_ms;
  if(elapsed>diagnostics.max_process_ms)diagnostics.max_process_ms=elapsed; }
/**
 * @brief 将窗口内双精度位置拆成绝对整数帧和Q16分数，不把绝对帧转double。
 * @param anchor 窗口起点零基绝对PCM帧。
 * @param fraction 估算器输出的窗口内位置，含整数部分。
 * @param out 非NULL输出；成功为VALID，否则MISSING/INVALID。
 * @return true为位置有效；false为NaN/Inf/负数/越窗/溢出。
 * @note 仅使用窗口内相对位置做double拆分；绝对锚点用整数加法和减法上界检查。
 */
static bool QuantizeToa(uint64_t anchor,double fraction,SoundToaPosition *out)
{
    out->status=SOUND_TOA_MISSING;out->frame=0U;out->fraction_q16=0U;
    out->candidate_count=0U;
    for(uint8_t i=0U;i<4U;++i)out->candidate_offset_q16[i]=0;
    /* NaN fails both comparisons; infinities/negative/窗口外值也被拒绝。 */
    if(!(fraction>=0.0 && fraction<256.0)){out->status=SOUND_TOA_INVALID;return false;}
    uint64_t integer=(uint64_t)fraction;
    const double remainder=fraction-(double)integer;
    uint32_t q16=(uint32_t)(remainder*65536.0+0.5);
    if(q16==65536U){q16=0U;++integer;}
    /* 事件服务还要把零基帧加1转模型进度；此处先保证两者都不溢出。 */
    if(anchor>UINT64_MAX-integer-1U){out->status=SOUND_TOA_INVALID;return false;}
    out->frame=anchor+integer;out->fraction_q16=(uint16_t)q16;
    out->status=SOUND_TOA_VALID;return true;
}

/**
 * @brief 仅在候选onset后的完整2400帧可得时执行原波形分类。
 * @param block 非NULL当前借用块，样本仅本次读取。
 * @param event 非NULL工作候选，仅更新event_class/quality，onset为会话内绝对帧。
 * @return true为分类完成或窗口永不可得（UNKNOWN）；false为等待后继块。
 * @note 当前块完整窗直接借用；跨块窗只拼已提交尾部和当前PCM到未提交槽，不保留PCM指针。
 *       起点已丢失或坐标溢出时不伪造窗口，保留UNKNOWN能量诊断；TOA复用已提交尾部但不补零。
 */
static bool ClassifyCandidate(const BSP_AudioBlock *block,SoundCandidate *event)
{
    event->event_class=EVENT_CLASS_UNKNOWN;event->quality=0U;
    if(event->session!=block->session || event->onset_frame>UINT64_MAX-CLASSIFY_FRAMES)return true;
    const uint64_t end_frame=event->onset_frame+CLASSIFY_FRAMES;
    const uint64_t block_end=block->first_frame+block->frame_count;
    if(event->onset_frame<block->first_frame &&
        (boundary || !class_history_valid || class_session!=block->session ||
         event->onset_frame<class_first_frame ||
         block->first_frame-class_first_frame!=CLASSIFY_FRAMES))return true;
    if(end_frame>block_end)return false;
    const int16_t *first;
    uint16_t stride;
    if(event->onset_frame>=block->first_frame){
        const size_t local=(size_t)(event->onset_frame-block->first_frame);
        first=(const int16_t *)&block->samples[2U*local+diagnostics.channel];stride=2U;
    }else{
        int16_t *scratch=sound_class_pcm[1U-class_committed_slot];
        const uint16_t prefix=(uint16_t)(block->first_frame-event->onset_frame);
        const size_t history_offset=(size_t)(event->onset_frame-class_first_frame);
        for(uint16_t i=0U;i<prefix;++i)
            scratch[i]=sound_class_pcm[class_committed_slot][history_offset+i];
        for(uint16_t i=prefix;i<CLASSIFY_FRAMES;++i)
            scratch[i]=block->samples[2U*(size_t)(i-prefix)+diagnostics.channel];
        first=scratch;stride=1U;
    }
    EventClassResult classified;
    (void)EventClassifier_ClassifyStrided(first,stride,CLASSIFY_FRAMES,&classified);
    event->event_class=(uint8_t)classified.type;event->quality=classified.quality;
    return true;
}

/** @brief 一次TOA窗口构建/估算结果；ready=false表示等待后继块。 */
typedef struct { bool ready,valid; uint64_t anchor; double fraction;
                 ToaBackwardCandidates candidates; SoundToaPosition position; } ToaEstimate;
typedef enum { TOA_WINDOW_READY,TOA_WINDOW_WAIT,TOA_WINDOW_LOST } ToaWindowStatus;
/**
 * @brief 从已提交CPU尾部历史和当前借用块拼出候选完整256帧窗。
 * @param block 非NULL当前借用块，只在调用期间读取。
 * @param anchor 窗口绝对首帧，已由估算器约定确定。
 * @param channel 选定声道0或1。
 * @param window 非NULL256点输出，成功时完成填充。
 * @return READY为窗口完整；WAIT为右侧帧尚未到；LOST为历史不足/坐标越界。
 * @note 历史只来自上一块End成功提交的2400帧尾部；不补零、不读取DMA未来数据。
 */
static ToaWindowStatus BuildToaWindow(const BSP_AudioBlock *block,uint64_t anchor,
    uint8_t channel,int16_t window[TOA_SEARCH_WINDOW])
{
    if(anchor>UINT64_MAX-(TOA_SEARCH_WINDOW-1U))return TOA_WINDOW_LOST;
    const uint64_t block_end=block->first_frame+block->frame_count;
    bool wait=false;
    for(uint16_t i=0U;i<TOA_SEARCH_WINDOW;++i)
    {
        const uint64_t frame=anchor+i;
        if(frame>=block->first_frame && frame<block_end)
        {
            window[i]=block->samples[2U*(size_t)(frame-block->first_frame)+channel];
        }
        else if(frame<block->first_frame)
        {
            if(!class_history_valid || class_session!=block->session ||
               frame<class_first_frame || frame>=class_first_frame+CLASSIFY_FRAMES)
            { return TOA_WINDOW_LOST; }
            window[i]=sound_class_pcm[class_committed_slot][frame-class_first_frame];
        }
        else { wait=true; }
    }
    return wait?TOA_WINDOW_WAIT:TOA_WINDOW_READY;
}
/**
 * @brief 估算一个候选的TOA；不完整右窗返回ready=false等待下一块。
 * @param block 非NULL当前借用块，样本仅本次读取。
 * @param event 非NULL候选，onset_frame为会话内绝对帧。
 * @param channel 选定声道0或1。
 * @param out 非NULL结果；ready=false时其余字段无意义。
 * @return true表示已得到终态（成功或永久缺失）；false表示等待后继块。
 * @note 只有完整窗才调用原估算器；失败计toa_miss，不伪造成FIRST+1。
 */
static bool EstimateToaCore(const BSP_AudioBlock *block,const SoundCandidate *event,
    uint8_t channel,ToaEstimate *out)
{
    out->ready=true;out->valid=false;out->anchor=0U;out->fraction=0.0;
    out->candidates=(ToaBackwardCandidates){0U,{0.0}};
    out->position=(SoundToaPosition){SOUND_TOA_MISSING,0U,0U,0U,{0,0,0,0}};
    const uint64_t anchor=event->onset_frame>TOA_LEFT_MARGIN?
        event->onset_frame-TOA_LEFT_MARGIN:0U;
    int16_t window[TOA_SEARCH_WINDOW];
    const ToaWindowStatus status=BuildToaWindow(block,anchor,channel,window);
    if(status==TOA_WINDOW_WAIT){out->ready=false;return false;}
    if(status==TOA_WINDOW_LOST){Add(&diagnostics.toa_miss,1U);return true;}
    double fraction=0.0;
    if(!ToaEstimator_EstimateBackward(&ToaBackward_DefaultConfig,window,&fraction))
    { Add(&diagnostics.toa_miss,1U);return true; }
    out->anchor=anchor;out->fraction=fraction;out->valid=true;
    Add(&diagnostics.toa_ok,1U);
    if(!QuantizeToa(anchor,fraction,&out->position))
    { out->valid=false;Add(&diagnostics.toa_invalid,1U);return true; }
    if(ToaEstimator_EstimateCandidates(&ToaBackward_DefaultConfig,window,&out->candidates))
    {
        out->position.candidate_count=out->candidates.count;
        for(uint8_t i=0U;i<out->candidates.count;++i)
        {
            const double delta=(out->candidates.fractions[i]-fraction)*65536.0;
            const double rounded=delta>=0.0?delta+0.5:delta-0.5;
            if(rounded>2147483647.0)out->position.candidate_offset_q16[i]=INT32_MAX;
            else if(rounded<-2147483648.0)out->position.candidate_offset_q16[i]=INT32_MIN;
            else out->position.candidate_offset_q16[i]=(int32_t)rounded;
        }
    }
    return true;
}
/**
 * @brief 将新暂存候选的TOA结果写入槽位。
 * @param block 非NULL当前借用块。
 * @param event 非NULL暂存候选。
 * @param slot 槽位0..3。
 * @param channel 选定声道。
 * @return 无返回值；待后继块时ready=false，Complete后队列保留重试。
 */
static void EstimateToa(const BSP_AudioBlock *block,const SoundCandidate *event,
    uint8_t slot,uint8_t channel)
{
    ToaEstimate estimate;
    (void)EstimateToaCore(block,event,channel,&estimate);
    staged_toa_ready[slot]=estimate.ready;
    staged_toa_ok[slot]=estimate.valid;
    staged_toa[slot]=estimate.fraction;
    staged_toa_anchor[slot]=estimate.anchor;
    staged_toa_position[slot]=estimate.position;
}
/** @brief 清除不能继续信任的确认历史和队列；无参数，无返回值，空历史重复失效不增加重置计数。 */
static void ResetHistory(void)
{
#ifdef SBCG_USE_SOUND_TIME
    if(current.have_input || count!=0U || active)SoundEventTime_Invalidate(SOUND_TIME_STREAM);
#endif
    if(current.have_input || count!=0U || active)Add(&diagnostics.resets,1U);
    (void)SoundDetector_Reset(&current);DropQueue();active=stage_valid=false;staged_count=0U;staged_total=0U;
    class_history_valid=false;
}
/**
 * @brief 初始化实验配置、工作副本和发送限速。
 * @param now_ms 当前HAL毫秒。
 * @param config 只读非NULL配置，复制使用。
 * @param channel 输入声道0/1。
 * @param log 可空日志，NULL使服务停用。
 * @return true为就绪，false为停用或参数非法。
 * @note 软件状态整体复位，不启动或停止采集；参数在初始化行报告，未经标准音频标定。
 */
bool SoundDetectorService_Init(uint32_t now_ms,const SoundDetectorConfig *config,
    uint8_t channel,void (*log)(const char *,int))
{
    diagnostics=(SoundDetectorServiceSnapshot){0};diagnostics.channel=channel;
    memset(toa_cache,0,sizeof toa_cache);toa_cache_next=0U;
    head=count=staged_count=0U;staged_total=0U;active=stage_valid=boundary=false;
    class_history_valid=false;class_committed_slot=0U;
    class_first_frame=staged_class_first_frame=0U;class_session=staged_class_session=0U;
    emit_line=log;last_event_ms=last_state_ms=now_ms;
    const bool valid=SoundDetector_Init(&current,config);
    diagnostics.configured=valid && channel<2U && log!=NULL;
    if(!diagnostics.configured)return false;
    char line[256];
    Emit(line,snprintf(line,sizeof line,
        "$SOUND_INIT,RESULT=OK,CH=%u,WIN_FRAMES=%u,ON_RMS=%u,OFF_RMS=%u,ON_WIN=%u,QUIET_WIN=%u,"
        "HOLDOFF_FRAMES=%" PRIu32 ",PROFILE=EXPERIMENTAL,SYNC=0\r\n",
        (unsigned)channel,(unsigned)config->window_frames,(unsigned)config->on_rms_pcm,
        (unsigned)config->off_rms_pcm,(unsigned)config->on_windows,(unsigned)config->quiet_windows,
        config->refractory_frames));
    return true;
}
/**
 * @brief 在已有Begin认领内暂算整个交错PCM半块；64点批量入口优先，其他配置自动回退。
 * @param now_ms 当前HAL毫秒，校验完成块年龄。
 * @param block 非NULL借用描述符，仅本次调用访问其中PCM。
 * @return true为暂存完成，false为元数据/生命周期错误，之后都须Complete。
 * @note 工作副本推进全部8192帧；先补旧候选完整分类窗，最后复制本块末2400点到未提交NOLOAD槽。
 *       未提交槽也用于跨块分类拼接；不修改已提交PCM/队列，不保存DMA指针。
 */
bool SoundDetectorService_Stage(uint32_t now_ms,const BSP_AudioBlock *block)
{
    if(!diagnostics.configured)return false;
    if(active){Add(&diagnostics.errors,1U);ResetHistory();return false;}
    active=true;stage_valid=false;staged_count=0U;staged_total=0U;boundary=false;
    if(block==NULL || block->samples==NULL || block->frame_count!=BSP_AUDIO_HALF_FRAMES ||
        !block->index_valid || block->session==0U ||
        block->first_frame>UINT64_MAX-block->frame_count ||
        (uint32_t)(now_ms-block->completed_ms)>=BLOCK_MAX_AGE_MS)return false;
    working=current;staged_ms=block->completed_ms;
    boundary=current.have_input && (!block->contiguous || block->gap_frames!=0U ||
        block->session!=current.session || current.last_frame==UINT64_MAX ||
        block->first_frame!=current.last_frame+1U);
    if(boundary)(void)SoundDetector_Reset(&working);
    const uint32_t begin_ms=BSP_Console_GetTickMs();
    memcpy(queue_working,queue,sizeof queue);
    if(!boundary){
        for(uint8_t i=0U;i<count;++i){
            QueuedCandidate *item=&queue_working[(head+i)%SOUND_DETECTOR_QUEUE_CAPACITY];
            if(!item->class_ready)item->class_ready=ClassifyCandidate(block,&item->event);
            if(!item->toa_ready){
                ToaEstimate estimate;
                if(EstimateToaCore(block,&item->event,diagnostics.channel,&estimate)){
                    item->toa_ready=true;item->toa_valid=estimate.valid;
                    item->toa_anchor=estimate.anchor;item->toa_fraction=estimate.fraction;
                    item->toa=estimate.position;
                }
            }
        }
    }
    int16_t batch[SOUND_DETECTOR_BATCH_MAX_FRAMES];
    uint32_t offset=0U;
    while(offset<block->frame_count)
    {
        const uint32_t remaining=block->frame_count-offset;
        const uint16_t available=(uint16_t)(remaining<SOUND_DETECTOR_BATCH_MAX_FRAMES ?
            remaining:SOUND_DETECTOR_BATCH_MAX_FRAMES);
        for(uint16_t i=0U;i<available;++i)
            batch[i]=block->samples[2U*(offset+i)+diagnostics.channel];
        SoundCandidate event;uint16_t consumed=0U;
        const SoundDetectorResult result=SoundDetector_PushBatch(&working,block->session,
            block->first_frame+offset,batch,available,&consumed,&event);
        if(consumed==0U || consumed>available){RecordStageTime(begin_ms);return false;}
        offset+=consumed;
        if(result==SOUND_DETECTOR_EVENT)
        {
            ++staged_total;
            if(staged_count<SOUND_DETECTOR_QUEUE_CAPACITY){
                const uint8_t slot=staged_count;
                staged[slot]=event;
                EstimateToa(block,&event,slot,diagnostics.channel);
                staged_class_ready[slot]=ClassifyCandidate(block,&staged[slot]);
                ++staged_count;
            }
        }
        else if(result!=SOUND_DETECTOR_NONE){RecordStageTime(begin_ms);return false;}
    }
    const uint32_t tail_offset=block->frame_count-CLASSIFY_FRAMES;
    for(uint16_t i=0U;i<CLASSIFY_FRAMES;++i)
        sound_class_pcm[1U-class_committed_slot][i]=
            block->samples[2U*(size_t)(tail_offset+i)+diagnostics.channel];
    staged_class_first_frame=block->first_frame+tail_offset;staged_class_session=block->session;
    RecordStageTime(begin_ms);
    stage_valid=true;return true;
}
/**
 * @brief 仅在真实End成功后提交工作副本及候选。
 * @param accepted BSP_Audio_EndBlock真实返回值，不得由暂算成功替代。
 * @return true表示已验证提交；false清历史且拒绝本块。
 * @note 不读取PCM；未验证候选不增加有效候选数，验证后两级队列损失均计入dropped。
 */
bool SoundDetectorService_Complete(bool accepted)
{
    if(!diagnostics.configured)return false;
    if(!active){Add(&diagnostics.errors,1U);ResetHistory();return false;}
    if(!accepted || !stage_valid){Add(&diagnostics.rejected_blocks,1U);ResetHistory();return false;}
    if(boundary){Add(&diagnostics.resets,1U);DropQueue();
#ifdef SBCG_USE_SOUND_TIME
        SoundEventTime_Invalidate(SOUND_TIME_STREAM);
#endif
    }
    else memcpy(queue,queue_working,sizeof queue);
    class_committed_slot=(uint8_t)(1U-class_committed_slot);
    class_first_frame=staged_class_first_frame;class_session=staged_class_session;class_history_valid=true;
    current=working;Add(&diagnostics.valid_blocks,1U);
    uint32_t sequence=diagnostics.candidates;
    Add(&diagnostics.candidates,staged_total);Add(&diagnostics.dropped,staged_total-staged_count);
    for(uint8_t i=0U;i<staged_count;++i)
    {
        Add(&sequence,1U);
        if(count==SOUND_DETECTOR_QUEUE_CAPACITY){Add(&diagnostics.dropped,1U);continue;}
        const uint8_t tail=(uint8_t)((head+count)%SOUND_DETECTOR_QUEUE_CAPACITY);
        queue[tail]=(QueuedCandidate){staged[i],staged_ms,sequence,
            staged_toa_anchor[i],staged_toa[i],staged_toa_ok[i],staged_toa_ready[i],
            staged_toa_position[i],staged_class_ready[i]};++count;
    }
    active=stage_valid=false;staged_count=0U;staged_total=0U;return true;
}
/**
 * @brief 复制软件状态。
 * @param out 非NULL调用方输出；NULL不写。
 * @return 无返回值。
 * @note 不授予PCM访问权，不读硬件或改变发送队列。
 */
void SoundDetectorService_GetSnapshot(SoundDetectorServiceSnapshot *out)
{
    if(out==NULL)return;
    *out=diagnostics;out->pending=count;out->armed=current.armed;out->staging=active;
}
/**
 * @brief 归还后检查流健康、候选年龄，并按固定上限发送。
 * @param now_ms 当前HAL毫秒，使用无符号差。
 * @param snapshot 当前非NULL只读采集快照，函数不保留指针。
 * @return 无返回值。
 * @note staging时不执行任何发布；故障清旧历史，不补发日志。候选仅含本地帧，SYNC始终0。
 *       名义TOA微秒为负数、非有限数或舍入后达到2^64时输出NA；不更改局部估算成功计数。
 */
void SoundDetectorService_Process(uint32_t now_ms,const BSP_AudioSnapshot *snapshot)
{
    if(!diagnostics.configured || active)return;
    const uint32_t begin_ms=BSP_Console_GetTickMs();
    if(snapshot==NULL || !snapshot->running || snapshot->index_faults!=0U ||
        snapshot->session==0U || snapshot->callback_count==0U ||
        (uint32_t)(now_ms-snapshot->last_callback_ms)>=STREAM_MAX_AGE_MS ||
        (current.have_input && current.session!=snapshot->session))
    { ResetHistory(); }
    for(uint8_t i=0U;i<SOUND_DETECTOR_QUEUE_CAPACITY && count!=0U;++i)
    {
        if((uint32_t)(now_ms-queue[head].completed_ms)<STREAM_MAX_AGE_MS)break;
        head=(uint8_t)((head+1U)%SOUND_DETECTOR_QUEUE_CAPACITY);--count;Add(&diagnostics.dropped,1U);
    }
    char line[256];
    if(count!=0U && queue[head].class_ready && queue[head].toa_ready && (uint32_t)(now_ms-last_event_ms)>=EVENT_LOG_PERIOD_MS)
    {
        const QueuedCandidate item=queue[head];head=(uint8_t)((head+1U)%SOUND_DETECTOR_QUEUE_CAPACITY);--count;
        last_event_ms=now_ms;char first[21],confirmed[21],toa_us[21]="NA";
        Decimal(item.event.onset_frame,first);Decimal(item.event.confirmed_frame,confirmed);
        if(item.toa_valid){
            /* 保持名义48000Hz换算；先拒绝负数/非有限数，再保证舍入结果可转为uint64。 */
            const double frames=(double)item.toa_anchor+item.toa_fraction;
            const double microseconds=frames*1000000.0/(double)AUDIO_RATE_HZ;
            const double rounded_microseconds=microseconds+0.5;
            if(0.0<=microseconds && rounded_microseconds<0x1p64)
                Decimal((uint64_t)rounded_microseconds,toa_us);
        }
        Emit(line,snprintf(line,sizeof line,
            "$SOUND_CAND,SEQ=%" PRIu32 ",SESSION=%" PRIu32 ",CH=%u,FIRST=%s,CONFIRMED=%s,E_AC=%" PRIu32
            ",PEAK=%" PRIu32 ",CLIP=%u,CAND=%u,LOCAL=1,SYNC=0,TOA_US=%s\r\n",
            item.sequence,item.event.session,(unsigned)diagnostics.channel,first,confirmed,item.event.mean_square_ac,
            item.event.peak_abs,item.event.clipped ? 1U:0U,(unsigned)item.toa.candidate_count,toa_us));
#ifdef SBCG_USE_SOUND_TIME
        /* 已归还并验证且已发布的元数据副本；时间服务不访问PCM。 */
        CacheToaCandidates(item.sequence,&item.toa);
        (void)SoundEventTime_SubmitToa(now_ms,item.sequence,&item.event,&item.toa);
#endif
    }
    if((uint32_t)(now_ms-last_state_ms)<STATE_LOG_PERIOD_MS){RecordProcessTime(begin_ms);return;}
    last_state_ms=now_ms;
    Emit(line,snprintf(line,sizeof line,
        "$SOUND_STATE,VALID_BLOCKS=%" PRIu32 ",REJECTED=%" PRIu32 ",RESETS=%" PRIu32 ",CANDIDATES=%" PRIu32
        ",DROPPED=%" PRIu32 ",ERRORS=%" PRIu32 ",PENDING=%u,ARMED=%u,CH=%u,SYNC=0"
        ",STAGE_MS=%" PRIu32 ",PROC_MS=%" PRIu32 "\r\n",
        diagnostics.valid_blocks,diagnostics.rejected_blocks,diagnostics.resets,diagnostics.candidates,
        diagnostics.dropped,diagnostics.errors,(unsigned)count,current.armed ? 1U:0U,
        (unsigned)diagnostics.channel,diagnostics.max_stage_ms,diagnostics.max_process_ms));
    RecordProcessTime(begin_ms);
}
