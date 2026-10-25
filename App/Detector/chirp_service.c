/**
 * @file        chirp_service.c
 * @brief       在已复制并通过End校验的音频上分批搜索固定扫频。
 * @details     沿用原有限搜索和缓存，增加分类计数及分段HAL耗时；状态与诊断交替，不增加周期日志行数。
 * @note        单前台所有权；复制缓冲不被DMA访问，Stage不相关/打印；不输出PTP时间或距离。
 */
#include "chirp_service.h"
#include "chirp_template.h"
#include "chirp_window.h"
#include "sound_detector.h"
#include "BSP/bsp_console.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define JOB_CAPACITY 4U
#define BLOCK_AGE_MS 250U
#define JOB_AGE_MS 1500U
#define STREAM_AGE_MS 1000U
#define STATE_PERIOD_MS 1000U
static const SoundDetectorConfig gate_config={64U,128U,64U,2U,8U,24000U};
/* CPU独占普通BSS，链接检查强制不进入0x20010000的DMA保留区。 */
static int16_t copied[CHIRP_SERVICE_HISTORY_FRAMES+BSP_AUDIO_HALF_FRAMES];
/* 活动任务完整输入独立于下一块复制，防止慢调度时搜索半途丢失数据。 */
static int16_t job_samples[CHIRP_SERVICE_JOB_SAMPLES];
typedef struct {
    uint64_t next,last;
    uint32_t completed_ms;
    ChirpMatch best;
    bool matched;
} SearchJob;
static SearchJob jobs[JOB_CAPACITY];
static ChirpWindowTemplate reference;
static SoundDetector gate;
static ChirpServiceSnapshot state;
static void (*logger)(const char *,int);
static uint64_t copied_first,copied_end,staged_first;
static uint32_t copied_session,copied_ms,staged_session,staged_ms,last_state_ms,sequence;
static uint16_t scan_offset,prefix_count;
static uint8_t head,count,channel_index;
static bool have_copy,stage_valid,stage_contiguous,job_cached;
static uint64_t job_first;
static uint32_t last_process_ms;
static bool have_process_time,diagnostic_next;
/** @brief 搜索丢弃的互斥主原因；NONE表示完整完成，不是丢弃。 */
typedef enum { DROP_NONE,DROP_EXPIRED,DROP_HISTORY,DROP_FULL,DROP_RANGE,DROP_RESET } DropReason;
_Static_assert(sizeof copied==22528U,"Chirp CPU copy buffer size changed");
_Static_assert(sizeof job_samples==5312U,"Chirp job snapshot size changed");
_Static_assert(CHIRP_SERVICE_JOB_SAMPLES==CHIRP_TEMPLATE_FRAMES+
    CHIRP_SERVICE_SEARCH_BEFORE+CHIRP_SERVICE_SEARCH_AFTER,"Job input must cover entire search");

/** @brief 饱和增加非NULL计数value，amount为条数，无返回值，仅前台。 */
static void Add(uint32_t *value,uint32_t amount)
{*value=UINT32_MAX-*value<amount ? UINT32_MAX:*value+amount;}
/**
 * @brief 同步增加丢弃总数及一个互斥原因，计数分别饱和。
 * @param reason 非NONE的原因，按实际拒绝分支分类，不推断后续原因。
 * @param amount 丢弃任务数量，允许0。
 * @return 无返回值。
 * @note 仅前台；未饱和时原因之和等于dropped，不改变任务处置。
 */
static void RecordDrop(DropReason reason,uint32_t amount)
{
    uint32_t *counter=NULL;
    switch(reason){
    case DROP_EXPIRED:counter=&state.drop_expired;break;
    case DROP_HISTORY:counter=&state.drop_history;break;
    case DROP_FULL:counter=&state.drop_full;break;
    case DROP_RANGE:counter=&state.drop_range;break;
    case DROP_RESET:counter=&state.drop_reset;break;
    case DROP_NONE:return;
    }
    if(counter!=NULL){Add(&state.dropped,amount);Add(counter,amount);}
}
/** @brief 将value转为output至少21字节十进制缓冲，无返回值，避免nano的64位printf。 */
static void Decimal(uint64_t value,char output[21])
{
    char reversed[20];unsigned n=0U;
    do{reversed[n++]=(char)('0'+value%10U);value/=10U;}while(value!=0U);
    for(unsigned i=0U;i<n;++i)output[i]=reversed[n-i-1U];
    output[n]='\0';
}
/** @brief 发送长度为length的临时line，回调返回前消费；无返回值，无效长度计错误。 */
static void Emit(const char *line,int length)
{
    if(logger!=NULL && length>0 && length<256)logger(line,length);
    else if(logger!=NULL)Add(&state.errors,1U);
}
/** @brief 清采集/门控/队列历史，保留软件配置和诊断计数；无参数，无返回值，不读取PCM。 */
static void ClearHistory(void)
{
    RecordDrop(DROP_RESET,count);head=count=0U;have_copy=false;prefix_count=scan_offset=0U;
    job_cached=false;
    (void)SoundDetector_Reset(&gate);
}
/** @brief 取当前CPU副本最早有效绝对帧，无参数；调用前须have_copy。 */
static uint64_t FirstAvailable(void){return copied_first-prefix_count;}
/** @brief 移除队首，reason为互斥丢弃原因或NONE完整完成；无返回值，不访问PCM。 */
static void Pop(DropReason reason)
{RecordDrop(reason,1U);head=(uint8_t)((head+1U)%JOB_CAPACITY);--count;job_cached=false;}
/**
 * @brief 初始化固定模板及独立能量门控。
 * @param now_ms 当前HAL毫秒，设定日志节流起点。
 * @param channel 声道0或1。
 * @param log 可空日志回调，返回前消费文本；NULL停用。
 * @return true为服务可用；false为停用/配置错误。
 * @note 不清大缓冲：未验证部分由有效范围隔离，不会读取，避免启动长阻塞。
 */
bool ChirpService_Init(uint32_t now_ms,uint8_t channel,void (*log)(const char *,int))
{
    state=(ChirpServiceSnapshot){0};logger=log;channel_index=channel;
    head=count=0U;have_copy=stage_valid=stage_contiguous=false;
    job_cached=false;
    have_process_time=diagnostic_next=false;last_process_ms=now_ms;
    prefix_count=scan_offset=0U;last_state_ms=now_ms;sequence=0U;
    const bool ready=ChirpWindow_Init(&reference,ChirpTemplate_PCM,CHIRP_TEMPLATE_FRAMES) &&
        SoundDetector_Init(&gate,&gate_config);
    state.configured=ready && channel<2U && log!=NULL;
    if(!state.configured)return false;
    char line[256];
    Emit(line,snprintf(line,sizeof line,
        "$CHIRP_INIT,RESULT=OK,CH=%u,TEMPLATE=2K6K50MS,SCAN=%u,SCORE=8,BEFORE=192,AFTER=64,"
        "CACHE_FRAMES=2656,JOB_MS=1500,RMS=64,CORR_PPM=800000,GATE_RMS=128,PROFILE=EXPERIMENTAL,SYNC=0,DIAG=1\r\n",
        (unsigned)channel,(unsigned)CHIRP_SERVICE_SCAN_BUDGET));
    return true;
}
/**
 * @brief 在原借用中仅复制选定声道，旧已验证末尾移到前缀。
 * @param now_ms 当前HAL毫秒，年龄<250ms。
 * @param block 本次Begin非NULL只读描述符，PCM仅在本调用访问。
 * @return true为复制完成，false为参数/生命周期错误；均须Complete。
 * @note 不保存描述符/PCM指针、不发布；复制用时按HAL毫秒差统计，之后仍须End验证。
 */
bool ChirpService_Stage(uint32_t now_ms,const BSP_AudioBlock *block)
{
    if(!state.configured)return false;
    if(state.staging){Add(&state.errors,1U);ClearHistory();state.staging=false;stage_valid=false;return false;}
    state.staging=true;stage_valid=false;
    if(block==NULL || block->samples==NULL || block->frame_count!=BSP_AUDIO_HALF_FRAMES ||
       block->session==0U || !block->index_valid || block->first_frame>UINT64_MAX-BSP_AUDIO_HALF_FRAMES ||
       (uint32_t)(now_ms-block->completed_ms)>=BLOCK_AGE_MS)return false;
    staged_first=block->first_frame;staged_session=block->session;staged_ms=block->completed_ms;
    stage_contiguous=have_copy && block->contiguous && block->gap_frames==0U &&
        block->session==copied_session && block->first_frame==copied_end;
    const uint32_t begin=BSP_Console_GetTickMs();
    if(stage_contiguous)memmove(copied,copied+BSP_AUDIO_HALF_FRAMES,
        CHIRP_SERVICE_HISTORY_FRAMES*sizeof copied[0]);
    for(uint32_t i=0U;i<BSP_AUDIO_HALF_FRAMES;++i)
        copied[CHIRP_SERVICE_HISTORY_FRAMES+i]=block->samples[2U*i+channel_index];
    const uint32_t elapsed=BSP_Console_GetTickMs()-begin;
    if(elapsed>state.max_copy_ms)state.max_copy_ms=elapsed;
    stage_valid=true;return true;
}
/**
 * @brief 在真实End成功时提交有效范围，失败清掉可疑历史。
 * @param accepted 原BSP End真实返回值。
 * @return true为新副本已验证，false为拒绝。
 * @note 不读取DMA；来不及扫描的旧块明确计SKIP并重置能量确认，不拼接缺失样本。
 */
bool ChirpService_Complete(bool accepted)
{
    if(!state.configured)return false;
    if(!state.staging){Add(&state.errors,1U);ClearHistory();return false;}
    state.staging=false;
    if(!accepted || !stage_valid){Add(&state.rejected_blocks,1U);ClearHistory();stage_valid=false;return false;}
    if(!stage_contiguous)ClearHistory();
    else if(scan_offset<BSP_AUDIO_HALF_FRAMES){Add(&state.skipped_blocks,1U);(void)SoundDetector_Reset(&gate);}
    prefix_count=stage_contiguous ? CHIRP_SERVICE_HISTORY_FRAMES:0U;
    copied_first=staged_first;copied_end=copied_first+BSP_AUDIO_HALF_FRAMES;
    copied_session=staged_session;copied_ms=staged_ms;scan_offset=0U;have_copy=true;stage_valid=false;
    while(count!=0U && !job_cached && jobs[head].next<FirstAvailable())Pop(DROP_HISTORY);
    Add(&state.valid_blocks,1U);return true;
}
/**
 * @brief 从已验证能量候选建立有限窗口搜索。
 * @param event 非NULL本次会话候选元数据，来自CPU副本扫描。
 * @return 无返回值；容量/范围拒绝计DROP。
 * @note 不裁掉未知历史来冒充完整搜索；仅帧0之前自然截到0。
 */
static void Queue(const SoundCandidate *event)
{
    Add(&state.candidates,1U);
    const uint64_t first=event->onset_frame>CHIRP_SERVICE_SEARCH_BEFORE ?
        event->onset_frame-CHIRP_SERVICE_SEARCH_BEFORE:0U;
    if(count==JOB_CAPACITY){RecordDrop(DROP_FULL,1U);return;}
    if(first<FirstAvailable()){RecordDrop(DROP_HISTORY,1U);return;}
    if(event->onset_frame>UINT64_MAX-(CHIRP_SERVICE_SEARCH_AFTER+CHIRP_TEMPLATE_FRAMES)){
        RecordDrop(DROP_RANGE,1U);return;
    }
    const uint8_t tail=(uint8_t)((head+count)%JOB_CAPACITY);
    jobs[tail]=(SearchJob){.next=first,.last=event->onset_frame+CHIRP_SERVICE_SEARCH_AFTER,
        .completed_ms=copied_ms};++count;
}
/**
 * @brief 对当前8192帧块最多扫描一次，建立已验证输入的能量候选。
 * @return 无返回值；仅作用于当前CPU副本与门控状态。
 * @note 无硬件/日志访问，不读取上一个已被覆盖块的扫描位置。
 */
static void Scan(void)
{
    while(scan_offset<BSP_AUDIO_HALF_FRAMES && state.last_scanned<CHIRP_SERVICE_SCAN_BUDGET){
        SoundCandidate event;
        const SoundDetectorResult result=SoundDetector_Push(&gate,copied_session,
            copied_first+scan_offset,copied[CHIRP_SERVICE_HISTORY_FRAMES+scan_offset],&event);
        ++scan_offset;++state.last_scanned;
        if(result==SOUND_DETECTOR_EVENT)Queue(&event);
        else if(result!=SOUND_DETECTOR_NONE){Add(&state.errors,1U);ClearHistory();return;}
    }
}
/**
 * @brief 评分至多8个窗口，完成一个任务后复制结果给调用方。
 * @param now_ms 最新HAL毫秒，任务1500ms过期。
 * @param publish 非NULL独占输出；返回true时含最高评分完整搜索结果。
 * @return true为一个任务完整匹配成功；false为未完成/未匹配。
 * @note 首次评分前必须完整缓存全部搜索输入；缺尾等待新块，缓存后不再依赖主复制缓冲。
 */
static bool Search(uint32_t now_ms,ChirpMatch *publish)
{
    while(count!=0U && state.last_scores<CHIRP_SERVICE_SCORE_BUDGET){
        SearchJob *job=&jobs[head];
        if((uint32_t)(now_ms-job->completed_ms)>=JOB_AGE_MS){Pop(DROP_EXPIRED);continue;}
        if(!job_cached && job->next<FirstAvailable()){Pop(DROP_HISTORY);continue;}
        if(!job_cached){
            if(job->last>copied_end-CHIRP_TEMPLATE_FRAMES)return false;
            const uint64_t offset=job->next>=copied_first ?
                CHIRP_SERVICE_HISTORY_FRAMES+(job->next-copied_first):
                CHIRP_SERVICE_HISTORY_FRAMES-(copied_first-job->next);
            const uint32_t length=(uint32_t)(job->last-job->next)+CHIRP_TEMPLATE_FRAMES;
            if(length>CHIRP_SERVICE_JOB_SAMPLES){Add(&state.errors,1U);Pop(DROP_RANGE);continue;}
            memcpy(job_samples,copied+(uint32_t)offset,length*sizeof job_samples[0]);
            job_first=job->next;job_cached=true;
        }
        ChirpMatch match;
        const bool found=ChirpWindow_Score(&reference,job_samples+(uint32_t)(job->next-job_first),copied_session,
            job->next,&ChirpMatcher_DefaultConfig,&match);
        ++state.last_scores;Add(&state.evaluations,1U);
        if(found && (!job->matched || match.correlation>job->best.correlation)){
            job->best=match;job->matched=true;
        }
        if(job->next==job->last){
            const bool matched=job->matched;
            /* 本服务没有观察搜索完成瞬间的采样游标，不填造confirmed_frame。 */
            if(matched){*publish=job->best;Add(&state.matches,1U);}
            Add(&state.completed_jobs,1U);Pop(DROP_NONE);return matched;
        }
        ++job->next;
    }
    return false;
}
/**
 * @brief 在原音频归还后执行一次有限工作，并最多发送一行事件或状态。
 * @param now_ms 最新HAL毫秒，使用无符号差处理正常回绕。
 * @param snapshot 非NULL只读采集快照，本次调用不保留。
 * @return 无返回值。
 * @note 无新IRQ/DMA；staging不操作，DHT门控由AudioCapture入口负责。
 */
void ChirpService_Process(uint32_t now_ms,const BSP_AudioSnapshot *snapshot)
{
    state.last_scanned=state.last_scores=0U;
    if(!state.configured || state.staging)return;
    if(have_process_time){
        const uint32_t gap=now_ms-last_process_ms;
        if(gap>state.max_process_gap_ms)state.max_process_gap_ms=gap;
    }
    last_process_ms=now_ms;have_process_time=true;
    if(snapshot==NULL || !snapshot->running || snapshot->session==0U || snapshot->index_faults!=0U ||
       snapshot->callback_count==0U || (uint32_t)(now_ms-snapshot->last_callback_ms)>=STREAM_AGE_MS ||
       (have_copy && (snapshot->session!=copied_session || (uint32_t)(now_ms-copied_ms)>=JOB_AGE_MS))){
        ClearHistory();return;
    }
    const uint32_t begin=BSP_Console_GetTickMs();
    uint32_t scan_end=begin;
    ChirpMatch publish={0};bool matched=false,searched=false;
    if(have_copy){
        Scan();scan_end=BSP_Console_GetTickMs();
        if(have_copy){searched=true;matched=Search(now_ms,&publish);}
    }
    const uint32_t end=BSP_Console_GetTickMs();
    const uint32_t elapsed=end-begin;
    if(scan_end-begin>state.max_scan_ms)state.max_scan_ms=scan_end-begin;
    if(searched && end-scan_end>state.max_score_ms)state.max_score_ms=end-scan_end;
    if(elapsed>state.max_work_ms)state.max_work_ms=elapsed;
    if(state.last_scanned>state.max_scanned)state.max_scanned=state.last_scanned;
    if(state.last_scores>state.max_scores)state.max_scores=state.last_scores;
    char line[256];
    if(matched){
        char first[21],last[21];Decimal(publish.first_frame,first);Decimal(publish.last_frame,last);
        Add(&sequence,1U);
        Emit(line,snprintf(line,sizeof line,
            "$CHIRP,SEQ=%" PRIu32 ",SESSION=%" PRIu32 ",FIRST=%s,LAST=%s,CORR_PPM=%" PRIu32
            ",RMS=%u,CLIP=%u,INVERTED=%u,LOCAL=1,SYNC=0\r\n",sequence,publish.session,first,last,
            (uint32_t)(publish.correlation*1000000.0f+0.5f),(unsigned)publish.rms_pcm,
            publish.clipped ? 1U:0U,publish.inverted ? 1U:0U));return;
    }
    if((uint32_t)(now_ms-last_state_ms)<STATE_PERIOD_MS)return;
    last_state_ms=now_ms;
    if(diagnostic_next){
        diagnostic_next=false;
        Emit(line,snprintf(line,sizeof line,
            "$CHIRP_DIAG,SCAN_MS=%" PRIu32 ",SCORE_MS=%" PRIu32 ",GAP_MS=%" PRIu32
            ",TIMEOUT=%" PRIu32 ",HISTORY=%" PRIu32 ",FULL=%" PRIu32 ",RANGE=%" PRIu32
            ",RESET=%" PRIu32 ",DONE=%" PRIu32 "\r\n",state.max_scan_ms,state.max_score_ms,
            state.max_process_gap_ms,state.drop_expired,state.drop_history,state.drop_full,
            state.drop_range,state.drop_reset,state.completed_jobs));return;
    }
    diagnostic_next=true;
    Emit(line,snprintf(line,sizeof line,
        "$CHIRP_STATE,OK=%" PRIu32 ",BAD=%" PRIu32 ",CAND=%" PRIu32 ",MATCH=%" PRIu32
        ",DROP=%" PRIu32 ",SKIP=%" PRIu32 ",ERR=%" PRIu32 ",EVAL=%" PRIu32
        ",COPY_MS=%" PRIu32 ",WORK_MS=%" PRIu32 ",S_MAX=%u,E_MAX=%u,PENDING=%u\r\n",state.valid_blocks,
        state.rejected_blocks,state.candidates,state.matches,state.dropped,state.skipped_blocks,
        state.errors,state.evaluations,state.max_copy_ms,state.max_work_ms,
        (unsigned)state.max_scanned,(unsigned)state.max_scores,(unsigned)count));
}
/**
 * @brief 复制计数、预算和当前生命周期状态。
 * @param out 非NULL独占输出，NULL不写。
 * @return 无返回值。
 * @note 不授予副本或DMA指针，不清计数。
 */
void ChirpService_GetSnapshot(ChirpServiceSnapshot *out)
{if(out!=NULL){*out=state;out->pending=count;}}
