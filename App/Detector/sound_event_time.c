/**
 * @file sound_event_time.c
 * @brief 用已完成传输组坐标为声音候选查询本地PTP模型时间。
 * @details 固定队列等右锚，复用真实插值内核；任何时域/流边界撤销旧配对，失败省略所有时间。
 * @note 仅串行前台，无PCM/HAL/动态分配；FIRST+1不是声学时刻，所有输出LOCAL=1/SYNC=0。
 */
#include "sound_event_time.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define ANCHOR_AGE_MS 250U
#define MODEL_MAX_AGE_MS 1500U
#define EVENT_WAIT_MS 2500U
#define STREAM_AGE_MS 1000U
#define RESULT_PERIOD_MS 250U
#define STATE_PERIOD_MS 1000U
typedef struct { SoundCandidate candidate; uint32_t sequence,submitted_ms; const char *failure; } Pending;
static AudioTimeAnchorObservation anchors[SOUND_TIME_CAPACITY];
static Pending pending[SOUND_TIME_CAPACITY];
static uint8_t anchor_count,count;
static bool have_anchor_sequence,have_event_sequence,have_stream;
static uint32_t last_anchor_sequence,last_event_sequence,last_result_ms,last_state_ms,last_dropped,stream_session;
static SoundEventTimeSnapshot stats;
static void (*logger)(const char *,int);
_Static_assert(sizeof("$SOUND_TIME,SEQ=4294967295,SESSION=4294967295,FIRST=18446744073709551615,STATUS=OK,L=4294967295,R=4294967295,EST_NS=18446744073709551615,LO_NS=18446744073709551615,HI_NS=18446744073709551615,LOCAL=1,SYNC=0\r\n")<=256,"event model line too long");
/** @brief 饱和增加非NULL计数p；无返回值，仅前台。 */
static void Increment(uint32_t *p){if(*p!=UINT32_MAX)++*p;}
/** @brief 将value转为至少21字节的非NULL out十进制文本，无返回值，不依赖nano printf的64位支持。 */
static void Decimal(uint64_t value,char out[21]){char r[20];unsigned n=0;do{r[n++]=(char)('0'+value%10U);value/=10U;}while(value);for(unsigned i=0;i<n;++i)out[i]=r[n-i-1U];out[n]=0;}
/** @brief 发送临时line的n字节，非正/截断不发送；回调返回前复制，无返回值。 */
static void Emit(const char *line,int n){if(logger!=NULL && n>0 && n<256)logger(line,n);}
/** @brief reason为静态原因字符串；清锚并将尚未判定队项标拒绝，无返回值、不打印。 */
static void Invalidate(const char *reason){anchor_count=0;have_anchor_sequence=false;for(uint8_t i=0;i<count;++i)if(pending[i].failure==NULL)pending[i].failure=reason;}
/** @brief 初始化前台软件状态；log可NULL停用，回调返回前消费字节；无返回值，不碰硬件。 */
void SoundEventTime_Init(void (*log)(const char *,int))
{
 logger=log;anchor_count=count=0;have_anchor_sequence=have_event_sequence=have_stream=false;
 last_anchor_sequence=last_event_sequence=last_result_ms=last_state_ms=last_dropped=stream_session=0;
 stats=(SoundEventTimeSnapshot){0};memset(anchors,0,sizeof anchors);memset(pending,0,sizeof pending);
}
/** @brief 按公开reason撤销旧模型；无返回值，不输出日志，队列中的身份保留供失败报告。 */
void SoundEventTime_Invalidate(SoundTimeReset reason){Invalidate(reason==SOUND_TIME_CLOCK ? "CLOCK":"STREAM");}
/** @brief 复制非NULL candidate和当前now_ms/严格递增sequence；排队true，非法/满false且累计丢弃，不访问PCM。 */
bool SoundEventTime_Submit(uint32_t now_ms,uint32_t sequence,const SoundCandidate *candidate)
{
 if(logger==NULL)return false;
 if(candidate==NULL || candidate->session==0U || candidate->onset_frame==UINT64_MAX || candidate->confirmed_frame<candidate->onset_frame || sequence==0U || (have_event_sequence && sequence<=last_event_sequence))
 {Increment(&stats.dropped);return false;}
 have_event_sequence=true;last_event_sequence=sequence;
 if(count==SOUND_TIME_CAPACITY){Increment(&stats.dropped);return false;}
 pending[count++]=(Pending){*candidate,sequence,now_ms,NULL};Increment(&stats.queued);return true;
}
/** @brief 检查非NULL a局部字段；true为有效且可组成完整相位区间，无副作用。 */
static bool Valid(const AudioTimeMapAnchor *a){return a->valid && a->session!=0U && a->nsec<1000000000U && a->frame_hi!=UINT64_MAX && a->frame_hi>=a->frame_lo && a->frame_hi-a->frame_lo<=AUDIO_TIME_MAP_MAX_WIDTH_FRAMES;}
/** @brief 非NULL有效a的本地PTP秒/纳秒合成uint64纳秒，无硬件读取。 */
static uint64_t Time(const AudioTimeMapAnchor *a){return (uint64_t)a->sec*UINT64_C(1000000000)+a->nsec;}
/** @brief 保存now_ms下新鲜的observation副本；非法/缺序/会话变更清旧模型，无返回值不打印。 */
void SoundEventTime_Observe(uint32_t now_ms,const AudioTimeAnchorObservation *observation)
{
 if(logger==NULL)return;
 if(observation==NULL || !Valid(&observation->anchor) || (uint32_t)(now_ms-observation->captured_ms)>ANCHOR_AGE_MS){Invalidate("ANCHOR");return;}
 if(have_anchor_sequence && observation->sequence!=last_anchor_sequence+1U)Invalidate("GAP");
 if(anchor_count!=0U){const AudioTimeMapAnchor *last=&anchors[anchor_count-1U].anchor;
  if(last->session!=observation->anchor.session)Invalidate("SESSION");
  else if(Time(&observation->anchor)<=Time(last) || observation->anchor.frame_lo<=last->frame_hi){Invalidate("ORDER");return;}}
 have_anchor_sequence=true;last_anchor_sequence=observation->sequence;
 if(anchor_count==SOUND_TIME_CAPACITY){memmove(anchors,anchors+1,(SOUND_TIME_CAPACITY-1U)*sizeof anchors[0]);--anchor_count;}
 anchors[anchor_count++]=*observation;
}
/**
 * @brief 查询非NULL item；返回NULL表示等待右锚，否则为结果/拒绝原因静态文本。
 * @param now_ms 当前HAL毫秒，用于2500ms截止。
 * @param item 队首只读候选及可选失效原因。
 * @param result 非NULL输出，调用前清零；仅OK有效。
 * @param left 输出左锚序号；right输出右锚序号；core输出真实核心状态；均非NULL。
 * @return 静态状态字符串或NULL；不外推，FIRST+1仅为传输组完成模型坐标。
 */
static const char *Resolve(uint32_t now_ms,const Pending *item,AudioTimeMapResult *result,uint32_t *left,uint32_t *right,AudioTimeMapStatus *core)
{
 if(item->failure!=NULL)return item->failure;
 if((uint32_t)(now_ms-item->submitted_ms)>=EVENT_WAIT_MS)return "TIMEOUT";
 const uint64_t position=item->candidate.onset_frame+1U;
 if(anchor_count!=0U && (uint32_t)(now_ms-anchors[anchor_count-1U].captured_ms)>MODEL_MAX_AGE_MS)return "STALE";
 if(anchor_count!=0U){if(item->candidate.session!=anchors[anchor_count-1U].anchor.session)return "SESSION";
  if(position<anchors[0].anchor.frame_hi+1U)return "NO_LEFT";}
 for(uint8_t i=1;i<anchor_count;++i){const AudioTimeMapAnchor *a=&anchors[i-1U].anchor,*b=&anchors[i].anchor;
  if(position>=a->frame_hi+1U && position<=b->frame_lo){*left=anchors[i-1U].sequence;*right=anchors[i].sequence;
   *core=AudioTimeMap_Estimate(a,b,item->candidate.session,position,result);return *core==AUDIO_TIME_MAP_OK ? "OK":"CORE";}}
 return NULL;
}
/** @brief 在now_ms和非NULL snapshot健康流下处理队首；异常清旧配对，限速输出模型及状态，无返回值。 */
void SoundEventTime_Process(uint32_t now_ms,const BSP_AudioSnapshot *snapshot)
{
 if(logger==NULL)return;
 if(snapshot==NULL || !snapshot->running || snapshot->session==0U || snapshot->index_faults!=0U || snapshot->callback_count==0U || (uint32_t)(now_ms-snapshot->last_callback_ms)>=STREAM_AGE_MS)Invalidate("STREAM");
 else {
  if(have_stream && (stream_session!=snapshot->session || last_dropped!=snapshot->dropped_blocks))Invalidate("STREAM");
  if(anchor_count!=0U && anchors[anchor_count-1U].anchor.session!=snapshot->session)Invalidate("SESSION");
  have_stream=true;stream_session=snapshot->session;last_dropped=snapshot->dropped_blocks;
 }
 char line[256];
 if(count!=0U && (uint32_t)(now_ms-last_result_ms)>=RESULT_PERIOD_MS){AudioTimeMapResult result={0};uint32_t left=0,right=0;AudioTimeMapStatus core=AUDIO_TIME_MAP_OK;
  const char *status=Resolve(now_ms,&pending[0],&result,&left,&right,&core);
  if(status!=NULL){const Pending item=pending[0];memmove(pending,pending+1,(count-1U)*sizeof pending[0]);--count;last_result_ms=now_ms;
   char frame[21];Decimal(item.candidate.onset_frame,frame);
   if(result.valid && core==AUDIO_TIME_MAP_OK && strcmp(status,"OK")==0){char est[21],lo[21],hi[21];Decimal(result.estimate_ns,est);Decimal(result.model_lower_ns,lo);Decimal(result.model_upper_ns,hi);Increment(&stats.ok);
    Emit(line,snprintf(line,sizeof line,"$SOUND_TIME,SEQ=%" PRIu32 ",SESSION=%" PRIu32 ",FIRST=%s,STATUS=OK,L=%" PRIu32 ",R=%" PRIu32 ",EST_NS=%s,LO_NS=%s,HI_NS=%s,LOCAL=1,SYNC=0\r\n",item.sequence,item.candidate.session,frame,left,right,est,lo,hi));}
   else {Increment(&stats.rejected);Emit(line,snprintf(line,sizeof line,"$SOUND_TIME,SEQ=%" PRIu32 ",SESSION=%" PRIu32 ",FIRST=%s,STATUS=%s,CORE=%u,LOCAL=1,SYNC=0\r\n",item.sequence,item.candidate.session,frame,status,(unsigned)core));}
  }
 }
 if((uint32_t)(now_ms-last_state_ms)>=STATE_PERIOD_MS){last_state_ms=now_ms;Emit(line,snprintf(line,sizeof line,"$SOUND_TIME_STATE,QUEUED=%" PRIu32 ",OK=%" PRIu32 ",REJECT=%" PRIu32 ",DROPPED=%" PRIu32 ",PENDING=%u,LOCAL=1,SYNC=0\r\n",stats.queued,stats.ok,stats.rejected,stats.dropped,(unsigned)count));}
}
/** @brief 将前台软件状态复制到非NULL out，NULL不操作，无返回值，不读硬件。 */
void SoundEventTime_GetSnapshot(SoundEventTimeSnapshot *out){if(out!=NULL){*out=stats;out->pending=count;}}
