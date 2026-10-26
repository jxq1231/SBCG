/**
 * @file        sound_detector_service.c
 * @brief       在PCM唯一借用中暂存检测，归还验证成功后发布有界本地帧候选。
 * @details     工作副本隔离未验证PCM，固定两级4条队列；失败/断流清历史，限速发送并统计损失。
 * @note        仅串行前台，不调用BSP Begin/End、不保留样本指针；无新IRQ/DMA/PTP时间查询。
 */
#include "sound_detector_service.h"
#ifdef SBCG_USE_SOUND_TIME
#include "sound_event_time.h"
#endif
#include "BSP/bsp_console.h"
#include "toa_estimator.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#define BLOCK_MAX_AGE_MS 250U
#define STREAM_MAX_AGE_MS 1000U
#define EVENT_LOG_PERIOD_MS 250U
#define STATE_LOG_PERIOD_MS 1000U
#define TOA_WINDOW_FRAMES 32U
#define AUDIO_RATE_HZ 48000U
const SoundDetectorConfig SoundDetectorService_DefaultConfig={64U,256U,128U,2U,8U,24000U};
typedef struct { SoundCandidate event; uint32_t completed_ms,sequence;
                 double toa_fraction; bool toa_valid; } QueuedCandidate;
static SoundDetector current,working;
static SoundDetectorServiceSnapshot diagnostics;
static SoundCandidate staged[SOUND_DETECTOR_QUEUE_CAPACITY];
static QueuedCandidate queue[SOUND_DETECTOR_QUEUE_CAPACITY];
static double staged_toa[SOUND_DETECTOR_QUEUE_CAPACITY];
static bool staged_toa_ok[SOUND_DETECTOR_QUEUE_CAPACITY];
static uint32_t staged_total,staged_ms,last_event_ms,last_state_ms;
static uint8_t staged_count,head,count;
static bool active,stage_valid,boundary;
static void (*emit_line)(const char *,int);

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
 * @brief 在Stage借用期内对候选窗做前沿子帧估算并写入暂存槽。
 * @param samples 非NULL借用交错PCM；first_frame为块首绝对帧；event为暂存的候选。
 * @param slot 暂存槽位，写入toa分数与有效性。
 * @return 无返回值；失败置toa_valid=false且计toa_miss。
 * @note 仅前台、仅在借用期间调用；窗取自onset起32帧，不足8帧或过阈失败不产出TOA。
 */
static void EstimateToa(const BSP_AudioBlock *block,const SoundCandidate *event,
    uint8_t slot,uint8_t channel)
{
    staged_toa_ok[slot]=false;staged_toa[slot]=0.0;
    if(event->onset_frame<block->first_frame)return;
    const uint64_t local=event->onset_frame-block->first_frame;
    if(local>=block->frame_count)return;
    const uint64_t available=block->frame_count-local;
    const uint16_t frames=(uint16_t)(available<TOA_WINDOW_FRAMES ?
        available:TOA_WINDOW_FRAMES);
    if(frames<8U){Add(&diagnostics.toa_miss,1U);return;}
    int16_t window[TOA_WINDOW_FRAMES];
    for(uint16_t i=0U;i<frames;++i)
        window[i]=block->samples[2U*(size_t)(local+i)+channel];
    double fraction=0.0;
    if(!ToaEstimator_Estimate(&ToaEstimator_DefaultConfig,window,&fraction)){
        Add(&diagnostics.toa_miss,1U);return;
    }
    staged_toa[slot]=fraction;staged_toa_ok[slot]=true;Add(&diagnostics.toa_ok,1U);
}
/** @brief 清除不能继续信任的确认历史和队列；无参数，无返回值，空历史重复失效不增加重置计数。 */
static void ResetHistory(void)
{
#ifdef SBCG_USE_SOUND_TIME
    if(current.have_input || count!=0U || active)SoundEventTime_Invalidate(SOUND_TIME_STREAM);
#endif
    if(current.have_input || count!=0U || active)Add(&diagnostics.resets,1U);
    (void)SoundDetector_Reset(&current);DropQueue();active=stage_valid=false;staged_count=0U;staged_total=0U;
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
    head=count=staged_count=0U;staged_total=0U;active=stage_valid=boundary=false;
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
 * @note 工作副本推进全部8192帧；每次从交错PCM抽取最多64点到128字节局部缓冲，不新增整块
 *       单声道BSS，不保存PCM指针，不改已提交计数或状态。
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
                staged[staged_count]=event;
                EstimateToa(block,&event,staged_count,diagnostics.channel);
                ++staged_count;
            }
        }
        else if(result!=SOUND_DETECTOR_NONE){RecordStageTime(begin_ms);return false;}
    }
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
    current=working;Add(&diagnostics.valid_blocks,1U);
    uint32_t sequence=diagnostics.candidates;
    Add(&diagnostics.candidates,staged_total);Add(&diagnostics.dropped,staged_total-staged_count);
    for(uint8_t i=0U;i<staged_count;++i)
    {
        Add(&sequence,1U);
        if(count==SOUND_DETECTOR_QUEUE_CAPACITY){Add(&diagnostics.dropped,1U);continue;}
        const uint8_t tail=(uint8_t)((head+count)%SOUND_DETECTOR_QUEUE_CAPACITY);
        queue[tail]=(QueuedCandidate){staged[i],staged_ms,sequence,
            staged_toa[i],staged_toa_ok[i]};++count;
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
    if(count!=0U && (uint32_t)(now_ms-last_event_ms)>=EVENT_LOG_PERIOD_MS)
    {
        const QueuedCandidate item=queue[head];head=(uint8_t)((head+1U)%SOUND_DETECTOR_QUEUE_CAPACITY);--count;
        last_event_ms=now_ms;char first[21],confirmed[21],toa_us[16];
        Decimal(item.event.onset_frame,first);Decimal(item.event.confirmed_frame,confirmed);
        if(item.toa_valid){
            /* 子帧到达时刻换算为微秒：帧坐标+插值分数再乘以(1e6/采样率)。 */
            const double frames=(double)item.event.onset_frame+item.toa_fraction;
            Decimal((uint64_t)(frames*1000000.0/(double)AUDIO_RATE_HZ+0.5),toa_us);
        }else{toa_us[0]='N';toa_us[1]='A';toa_us[2]='\0';}
        Emit(line,snprintf(line,sizeof line,
            "$SOUND_CAND,SEQ=%" PRIu32 ",SESSION=%" PRIu32 ",CH=%u,FIRST=%s,CONFIRMED=%s,E_AC=%" PRIu32
            ",PEAK=%" PRIu32 ",CLIP=%u,LOCAL=1,SYNC=0,TOA_US=%s\r\n",
            item.sequence,item.event.session,(unsigned)diagnostics.channel,first,confirmed,item.event.mean_square_ac,
            item.event.peak_abs,item.event.clipped ? 1U:0U,toa_us));
#ifdef SBCG_USE_SOUND_TIME
        /* 已归还并验证且已发布的元数据副本；时间服务不访问PCM。 */
        (void)SoundEventTime_Submit(now_ms,item.sequence,&item.event);
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
