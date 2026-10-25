/**
 * @file test_service.c
 * @brief 真实服务/内核的暂存提交、无效归还、队列和日志边界测试。
 * @note 合成PCM及状态快照，不访问实物。
 */
#include "Detector/sound_detector_service.h"
#include "BSP/bsp_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS];
static BSP_AudioBlock block;
static BSP_AudioSnapshot health;
static unsigned candidate_logs,state_logs,all_logs;
static bool borrowed;
static char last_candidate[256];
static char last_state[256];
static SoundDetectorConfig config={4,8,2,2,2,0};
static uint32_t service_tick,service_step;
/** @brief 受控HAL毫秒；service_step非零时每次读取推进，用于验证计时字段语义。 */
uint32_t BSP_Console_GetTickMs(void){service_tick+=service_step;return service_tick;}
/** @brief 验证只在归还后发送有界完整行；text为借用文本、length为字节数，无返回值。 */
static void Log(const char *text,int length)
{
 CHECK(!borrowed && length>1 && length<256 && text[length-2]=='\r' && text[length-1]=='\n');++all_logs;
 if(strncmp(text,"$SOUND_CAND,",12)==0){memcpy(last_candidate,text,(size_t)length);last_candidate[length]=0;++candidate_logs;}
 if(strncmp(text,"$SOUND_STATE,",13)==0){memcpy(last_state,text,(size_t)length);last_state[length]=0;++state_logs;}
}
/** @brief 复制公开状态；无参数，返回只读测试副本。 */
static SoundDetectorServiceSnapshot Snap(void){SoundDetectorServiceSnapshot s;SoundDetectorService_GetSnapshot(&s);return s;}
/** @brief 初始化已定义对象/8192帧输入，now为当前毫秒，channel选声道；无返回值。 */
static void Setup(uint32_t now,uint8_t channel)
{
 borrowed=false;candidate_logs=state_logs=all_logs=0;last_candidate[0]=0;memset(pcm,0,sizeof pcm);
 block=(BSP_AudioBlock){.samples=pcm,.frame_count=8192,.session=1,.completed_ms=now,.index_valid=true,.contiguous=true};
 health=(BSP_AudioSnapshot){.running=true,.session=1,.callback_count=1,.last_callback_ms=now};
 CHECK(SoundDetectorService_Init(now,&config,channel,Log));CHECK(all_logs==1);
}
/** @brief 在指定帧范围填交替PCM，channel为声道0/1；无返回值。 */
static void Tone(unsigned begin,unsigned end,unsigned channel)
{for(unsigned i=begin;i<end;++i)pcm[2*i+channel]=(i%2 ? 8:-8);}
/** @brief 模拟在唯一认领中暂算，再用真实End结果提交；now为毫秒，accepted为注入的归还结果。 */
static bool Deliver(uint32_t now,bool accepted)
{borrowed=true;bool staged=SoundDetectorService_Stage(now,&block);borrowed=false;bool committed=SoundDetectorService_Complete(accepted);CHECK(committed==(staged&&accepted));return committed;}
/** @brief 验证候选先暂存、只在归还成功后输出，及跨块连续确认；无参数，无返回值。 */
static void TestCommit(void)
{
 Setup(0,0);Tone(8,8192,0);borrowed=true;CHECK(SoundDetectorService_Stage(0,&block));
 unsigned before=all_logs;SoundDetectorService_Process(1000,&health);CHECK(all_logs==before && Snap().candidates==0);
 borrowed=false;CHECK(SoundDetectorService_Complete(true));CHECK(Snap().candidates==1 && Snap().pending==1);
 memset(pcm,0,sizeof pcm);SoundDetectorService_Process(250,&health);
 CHECK(candidate_logs==1 && strstr(last_candidate,",FIRST=8,CONFIRMED=15,E_AC=64,PEAK=8,CLIP=0,LOCAL=1,SYNC=0\r\n"));
 Setup(0,0);Tone(8188,8192,0);CHECK(Deliver(0,true));CHECK(Snap().candidates==0);
 block.first_frame=8192;block.completed_ms=170;health.last_callback_ms=170;Tone(0,8192,0);CHECK(Deliver(170,true));
 SoundDetectorService_Process(250,&health);CHECK(candidate_logs==1 && strstr(last_candidate,"FIRST=8188,CONFIRMED=8195,"));
 puts("PASS service commit: no publish during claim, no PCM pointer after End, exact cross-block confirmation");
}
/** @brief 归还失败不能沿用旧布防/确认或统计未验证候选；无参数，无返回值。 */
static void TestRejected(void)
{
 Setup(0,0);CHECK(Deliver(0,true));CHECK(Snap().armed);
 block.first_frame=8192;block.completed_ms=170;Tone(0,8192,0);CHECK(!Deliver(170,false));
 CHECK(Snap().candidates==0 && Snap().pending==0 && Snap().rejected_blocks==1 && !Snap().armed);
 block.first_frame=16384;block.completed_ms=340;CHECK(Deliver(340,true));CHECK(Snap().candidates==0);
 SoundDetectorService_Process(500,&health);CHECK(candidate_logs==0);
 for(unsigned bad=0;bad<6;++bad){Setup(0,0);Tone(8,8192,0);
  if(bad==0)block.samples=NULL;else if(bad==1)block.frame_count=8191;else if(bad==2)block.session=0;
  else if(bad==3)block.index_valid=false;else if(bad==4)block.first_frame=UINT64_MAX-8191U;else block.completed_ms=UINT32_MAX-249U;
  CHECK(!Deliver(0,true));CHECK(Snap().rejected_blocks==1 && Snap().candidates==0);
 }
 puts("PASS rejection: End failure clears old evidence; metadata/age/overflow rejects before sample access");
}
/** @brief 验证两级队列容量、溢出可见、限速和过期丢弃；无参数，无返回值。 */
static void TestQueue(void)
{
 Setup(0,0);for(unsigned i=0;i<8192;i+=16)Tone(i+8,i+16,0);
 CHECK(Deliver(0,true));CHECK(Snap().candidates==512 && Snap().dropped==508 && Snap().pending==4);
 SoundDetectorService_Process(249,&health);CHECK(candidate_logs==0);SoundDetectorService_Process(250,&health);CHECK(candidate_logs==1 && Snap().pending==3);
 SoundDetectorService_Process(250,&health);CHECK(candidate_logs==1);
 block.first_frame=8192;block.completed_ms=200;CHECK(Deliver(200,true));CHECK(Snap().candidates==1024 && Snap().dropped==1019 && Snap().pending==4);
 health.last_callback_ms=1300;SoundDetectorService_Process(1300,&health);CHECK(Snap().pending==0 && Snap().dropped==1023 && candidate_logs==1);
 CHECK(state_logs==1);SoundDetectorService_Process(1300,&health);CHECK(state_logs==1);
 puts("PASS queues: stage4/FIFO4, exact drop accounting, bounded log cadence, stale queue discarded");
}
/** @brief 检查元数据断流及快照故障会清队列且只记一次历史重置；无参数，无返回值。 */
static void TestBoundaries(void)
{
 for(unsigned boundary=0;boundary<3;++boundary){Setup(0,0);Tone(8,8192,0);CHECK(Deliver(0,true));memset(pcm,0,sizeof pcm);
  block.first_frame=8192;block.completed_ms=170;
  if(boundary==0){block.first_frame+=8;block.gap_frames=8;block.contiguous=false;}
  else if(boundary==1)block.session=2;else block.contiguous=false;
  CHECK(Deliver(170,true));CHECK(Snap().pending==0 && Snap().dropped==1 && Snap().resets==1);
 }
 for(unsigned fault=0;fault<4;++fault){Setup(0,0);Tone(8,8192,0);CHECK(Deliver(0,true));
  if(fault==0)health.running=false;else if(fault==1)health.session=2;else if(fault==2)health.index_faults=1;
  uint32_t now=fault==3 ? 1000:250;SoundDetectorService_Process(now,&health);CHECK(Snap().pending==0 && !Snap().armed);
  uint32_t resets=Snap().resets;SoundDetectorService_Process(now,&health);CHECK(Snap().resets==resets);
 }
 puts("PASS boundaries: gap/session/explicit discontinuity purge queued old events; unhealthy stream resets once");
}
/** @brief 验证声道、tick回绕、未配对生命周期和完整64位格式；无参数，无返回值。 */
static void TestOther(void)
{
 Setup(0,1);Tone(8,8192,0);CHECK(Deliver(0,true));CHECK(Snap().candidates==0);
 Setup(UINT32_MAX-99U,1);Tone(8,8192,1);CHECK(Deliver(UINT32_MAX-99U,true));SoundDetectorService_Process(149,&health);CHECK(candidate_logs==0);
 SoundDetectorService_Process(150,&health);CHECK(candidate_logs==1 && strstr(last_candidate,",CH=1,"));
 Setup(0,0);block.first_frame=UINT64_MAX-16383U;Tone(8,8192,0);CHECK(Deliver(0,true));SoundDetectorService_Process(250,&health);
 CHECK(candidate_logs==1 && strstr(last_candidate,"FIRST=18446744073709535240,CONFIRMED=18446744073709535247,"));
 Setup(0,0);CHECK(!SoundDetectorService_Complete(true) && Snap().errors==1);
 borrowed=true;CHECK(SoundDetectorService_Stage(0,&block));CHECK(!SoundDetectorService_Stage(0,&block));borrowed=false;
 CHECK(!SoundDetectorService_Complete(true) && Snap().pending==0 && Snap().errors>=2);
 CHECK(!SoundDetectorService_Init(0,&config,2,Log));CHECK(!SoundDetectorService_Init(0,&config,0,NULL));
 SoundDetectorService_GetSnapshot(NULL);
 puts("PASS other: selected channel, tick wrap, bounded uint64 logs, invalid init and unpaired transaction reject");
}
/** @brief 验证Stage/Process最大毫秒计数只在真实调用后更新、初始化归零；无参数，无返回值。 */
static void TestTiming(void)
{
 service_tick=100U;service_step=0U;Setup(100U,0U);
 SoundDetectorServiceSnapshot s=Snap();
 CHECK(s.max_stage_ms==0U && s.max_process_ms==0U);
 service_step=2U;Tone(8,8192,0);borrowed=true;CHECK(SoundDetectorService_Stage(100,&block));
 borrowed=false;CHECK(SoundDetectorService_Complete(true));service_step=0U;
 s=Snap();CHECK(s.max_stage_ms==2U && s.max_process_ms==0U);
 service_step=3U;SoundDetectorService_Process(250,&health);service_step=0U;
 s=Snap();CHECK(s.max_process_ms==3U && s.max_stage_ms==2U);
 /* 回绕与“先大后小”仍保持最大值。 */
 service_tick=UINT32_MAX-1U;service_step=2U;borrowed=true;CHECK(SoundDetectorService_Stage(100,&block));
 borrowed=false;CHECK(SoundDetectorService_Complete(true));service_step=0U;
 s=Snap();CHECK(s.max_stage_ms==2U);
 service_step=1U;borrowed=true;CHECK(SoundDetectorService_Stage(100,&block));
 borrowed=false;CHECK(SoundDetectorService_Complete(true));service_step=0U;
 s=Snap();CHECK(s.max_stage_ms==2U);
 Setup(500U,0U);s=Snap();CHECK(s.max_stage_ms==0U && s.max_process_ms==0U);
 puts("PASS timing: bounded stage/process max HAL ms, wrap/max-hold and reset on reinit");
}
/** @brief 验证$SOUND_STATE始终包含计时字段，且持续故障快照仍按原1000ms节流输出状态；无参数，无返回值。 */
static void TestStateLogging(void)
{
 Setup(0U,0U);
 SoundDetectorService_Process(1000U,&health);
 CHECK(state_logs==1U && strstr(last_state,",STAGE_MS=")!=NULL && strstr(last_state,",PROC_MS=")!=NULL);
 health.running=false;
 const unsigned before=state_logs;
 SoundDetectorService_Process(2000U,&health);CHECK(state_logs==before+1U);
 SoundDetectorService_Process(3000U,&health);CHECK(state_logs==before+2U);
 puts("PASS state log: timing fields present and fault path keeps periodic $SOUND_STATE");
}
/** @brief 运行真实服务边界测试；无参数，成功0。 */
int main(void){TestCommit();TestRejected();TestQueue();TestBoundaries();TestOther();TestTiming();TestStateLogging();puts("PASS sound service: 7 groups + timing");return 0;}
