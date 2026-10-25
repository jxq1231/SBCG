/**
 * @file        test_service.c
 * @brief       验证复制/归还提交、分批预算及跨块匹配，End后擦除模拟DMA。
 * @details     使用冻结模板、真实纯C服务与可控采集元数据；不是板端耗时证明。
 * @note        不访问开发板；所有输入PCM由本测试拥有。
 */
#include "Detector/chirp_service.h"
#include "Detector/chirp_template.h"
#include "Detector/chirp_window.h"
#include "BSP/bsp_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS];
static uint32_t tick_ms;static unsigned events,lines;static bool borrowed;
static char last_event[256],last_any[256];
static uint32_t tick_step,measurement_tick;
static uint8_t input_channel;
static unsigned signal_kind;
/** @brief 条件求值一次，失败打印行号并结束；无返回值，测试专用。 */
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
/** @brief 默认返回受控HAL毫秒；tick_step非零时推进测量时钟，用于验证计时与回绕。 */
uint32_t BSP_Console_GetTickMs(void)
{if(tick_step!=0U){measurement_tick+=tick_step;return measurement_tick;}return tick_ms;}
/** @brief 检查归还后输出并复制当前行及匹配事件；text借用length字节，无返回值。 */
static void Log(const char *text,int length)
{
 CHECK(!borrowed && length>0 && length<256);++lines;
 memcpy(last_any,text,(size_t)length);last_any[length]=0;
 if(strncmp(text,"$CHIRP,",7)==0){memcpy(last_event,text,(size_t)length);last_event[length]=0;++events;}
}
/** @brief 生成session1的单块元数据并将范围内的冻结模板写入声道0；first/onset为绝对帧，无返回值。 */
static void Block(uint64_t first,uint64_t onset,bool accepted)
{
 static const int16_t tone[16]={0,3827,7071,9239,10000,9239,7071,3827,
  0,-3827,-7071,-9239,-10000,-9239,-7071,-3827};
 memset(pcm,0,sizeof pcm);
 for(uint32_t i=0U;i<8192U;++i)if(first+i>=onset && first+i-onset<2400U){
  const uint32_t index=(uint32_t)(first+i-onset);
  pcm[2U*i+input_channel]=signal_kind==1U ? tone[index%16U]:
   signal_kind==2U ? ChirpTemplate_PCM[2399U-index]:ChirpTemplate_PCM[index];
 }
 BSP_AudioBlock block={.samples=pcm,.session=1U,.frame_count=8192U,.first_frame=first,
  .completed_ms=tick_ms,.index_valid=true,.contiguous=true};
 borrowed=true;CHECK(ChirpService_Stage(tick_ms,&block));
 /* 未End时即使调用Process也不得处理/输出。 */
 BSP_AudioSnapshot snap={.running=true,.session=1U,.callback_count=1U,.last_callback_ms=tick_ms};
 unsigned old=lines;ChirpService_Process(tick_ms,&snap);CHECK(lines==old);
 borrowed=false;memset(pcm,0,sizeof pcm);CHECK(ChirpService_Complete(accepted)==accepted);
}
/** @brief 推进calls次调度并断言每次操作次数上限；无返回值，不模拟真实耗时。 */
static void Work(unsigned calls)
{
 for(unsigned i=0U;i<calls;++i){
  BSP_AudioSnapshot snap={.running=true,.session=1U,.callback_count=1U,.last_callback_ms=tick_ms};
  ChirpService_Process(tick_ms++,&snap);
  ChirpServiceSnapshot s;ChirpService_GetSnapshot(&s);
  CHECK(s.last_scanned<=CHIRP_SERVICE_SCAN_BUDGET && s.last_scores<=CHIRP_SERVICE_SCORE_BUDGET);
 }
}
/** @brief 执行归还和边界用例，成功返回0。 */
int main(void)
{
 ChirpWindowTemplate ref;ChirpMatch scored;
 CHECK(ChirpWindow_Init(&ref,ChirpTemplate_PCM,2400U));
 CHECK(ChirpWindow_Score(&ref,ChirpTemplate_PCM,1U,UINT64_C(5000000000),&ChirpMatcher_DefaultConfig,&scored));
 CHECK(scored.first_frame==UINT64_C(5000000000) && scored.correlation>0.99999f && scored.confirmed_frame==0U);
 CHECK(!ChirpWindow_Score(&ref,ChirpTemplate_PCM,0U,0U,&ChirpMatcher_DefaultConfig,&scored));
 CHECK(scored.session==0U);
 CHECK(!ChirpWindow_Score(&ref,ChirpTemplate_PCM,1U,UINT64_MAX,&ChirpMatcher_DefaultConfig,&scored));
 ChirpMatcherConfig bad=ChirpMatcher_DefaultConfig;bad.min_correlation=NAN;
 CHECK(!ChirpWindow_Score(&ref,ChirpTemplate_PCM,1U,0U,&bad,&scored));
 CHECK(!ChirpWindow_Init(NULL,ChirpTemplate_PCM,2400U));
 CHECK(!ChirpWindow_Init(&ref,NULL,2400U));CHECK(ref.samples==NULL);
 int16_t extreme[2400];
 for(unsigned i=0U;i<2400U;++i)extreme[i]=i%2U==0U ? INT16_MIN:INT16_MAX;
 CHECK(ChirpWindow_Init(&ref,extreme,2400U));
 CHECK(ChirpWindow_Score(&ref,extreme,1U,UINT64_MAX-2399U,&ChirpMatcher_DefaultConfig,&scored));
 CHECK(scored.last_frame==UINT64_MAX && scored.clipped && scored.correlation>0.99999f && scored.rms_pcm==32767U);
 CHECK(ChirpService_Init(0U,0U,Log));
 tick_ms=170U;Block(0U,2000U,true);CHECK(events==0U);Work(80U);
 CHECK(events==1U && strstr(last_event,",FIRST=2000,")!=NULL);
 ChirpServiceSnapshot s;ChirpService_GetSnapshot(&s);
 CHECK(s.max_scores<=8U && s.max_scanned<=CHIRP_SERVICE_SCAN_BUDGET && s.valid_blocks==1U && s.matches==1U && s.evaluations==257U);
 for(signal_kind=1U;signal_kind<=2U;++signal_kind){
  events=0U;CHECK(ChirpService_Init(tick_ms,0U,Log));Block(0U,2000U,true);Work(80U);
  ChirpService_GetSnapshot(&s);CHECK(events==0U && s.candidates==1U && s.evaluations==257U);
 }
 signal_kind=0U;
 events=0U;CHECK(ChirpService_Init(tick_ms,0U,Log));
 Block(0U,8100U,true);Work(20U);CHECK(events==0U);
 ChirpService_GetSnapshot(&s);CHECK(s.pending==1U && s.evaluations==0U);
 tick_ms+=100U;Block(8192U,8100U,true);Work(80U);
 CHECK(events==1U && strstr(last_event,",FIRST=8100,")!=NULL);
 ChirpService_GetSnapshot(&s);CHECK(s.evaluations==257U);
 events=0U;CHECK(ChirpService_Init(tick_ms,0U,Log));
 Block(0U,2000U,false);Work(80U);CHECK(events==0U);
 ChirpService_GetSnapshot(&s);CHECK(s.rejected_blocks==1U && s.valid_blocks==0U);
 /* 新块过快到达，只允许明确记录未扫描覆盖，不沿用旧确认历史。 */
 Block(0U,2000U,true);Block(8192U,UINT64_MAX,true);Work(20U);
 ChirpService_GetSnapshot(&s);CHECK(s.skipped_blocks==1U && events==0U);
 /* 已评分的任务完全保留在3072帧前缀内，可在替换后完整完成。 */
 events=0U;CHECK(ChirpService_Init(tick_ms,0U,Log));
 Block(0U,5600U,true);Work(12U);ChirpService_GetSnapshot(&s);
 CHECK(s.pending==1U && s.evaluations>0U && s.evaluations<257U);
 Block(8192U,UINT64_MAX,true);Work(80U);ChirpService_GetSnapshot(&s);
 CHECK(events==1U && strstr(last_event,",FIRST=5600,")!=NULL && s.evaluations==257U && s.skipped_blocks==0U);
 /* 已完整缓存的活动任务，即使原复制块被替换仍必须完整评分。 */
 events=0U;CHECK(ChirpService_Init(tick_ms,0U,Log));
 Block(0U,2000U,true);Work(5U);ChirpService_GetSnapshot(&s);
 CHECK(s.pending==1U && s.evaluations>0U && s.evaluations<257U);
 Block(8192U,UINT64_MAX,true);Work(80U);ChirpService_GetSnapshot(&s);
 CHECK(events==1U && s.dropped==0U && s.skipped_blocks==0U && s.pending==0U && s.evaluations==257U);
 /* 尚未取得完整搜索输入时连续覆盖，则必须明确丢弃，不读失去的窗口。 */
 events=0U;CHECK(ChirpService_Init(tick_ms,0U,Log));
 Block(0U,8100U,true);Work(1U);ChirpService_GetSnapshot(&s);CHECK(s.pending==1U && s.evaluations==0U);
 Block(8192U,8100U,true);Block(16384U,UINT64_MAX,true);Work(80U);ChirpService_GetSnapshot(&s);
 CHECK(events==0U && s.dropped==1U && s.skipped_blocks==1U && s.pending==0U);
 /* 有待定跨块任务时End失败，必须清掉旧任务与部分前缀。 */
 CHECK(ChirpService_Init(tick_ms,0U,Log));Block(0U,8100U,true);Work(20U);
 ChirpService_GetSnapshot(&s);CHECK(s.pending==1U);
 Block(8192U,8100U,false);Work(80U);ChirpService_GetSnapshot(&s);
 CHECK(events==0U && s.pending==0U && s.dropped==1U && s.rejected_blocks==1U);
 /* 已经独立缓存并部分评分的任务，也不能跨失败End或新会话发布。 */
 for(unsigned failure=0U;failure<2U;++failure){
  CHECK(ChirpService_Init(tick_ms,0U,Log));Block(0U,2000U,true);Work(5U);
  ChirpService_GetSnapshot(&s);CHECK(s.pending==1U && s.evaluations>0U && s.evaluations<257U);
  if(failure==0U)Block(8192U,UINT64_MAX,false);
  else{
   BSP_AudioSnapshot snap={.running=true,.session=2U,.callback_count=1U,.last_callback_ms=tick_ms};
   ChirpService_Process(tick_ms,&snap);
  }
  Work(80U);ChirpService_GetSnapshot(&s);CHECK(s.pending==0U && s.dropped==1U && events==0U);
 }
 /* 两个排队任务先后换头，必须重新缓存各自输入，不能复用前一个快照。 */
 CHECK(ChirpService_Init(tick_ms,0U,Log));Block(0U,2000U,true);Work(5U);
 Block(8192U,UINT64_MAX,true);Work(1U);Block(16384U,UINT64_MAX,true);Work(1U);
 Block(24576U,30000U,true);Work(1U);ChirpService_GetSnapshot(&s);CHECK(s.pending==2U);
 Block(32768U,UINT64_MAX,true);Work(80U);ChirpService_GetSnapshot(&s);
 CHECK(events==2U && s.matches==2U && s.evaluations==514U && s.dropped==0U && s.skipped_blocks==0U);
 CHECK(strstr(last_event,",FIRST=30000,")!=NULL);events=0U;
 /* 1499ms仍可推进，1500ms明确失效；时间差按无符号计算。 */
 CHECK(ChirpService_Init(tick_ms,0U,Log));const uint32_t started=tick_ms;
 Block(0U,2000U,true);Work(5U);tick_ms=started+1499U;Work(1U);
 ChirpService_GetSnapshot(&s);CHECK(s.pending==1U);Work(1U);ChirpService_GetSnapshot(&s);
 CHECK(s.pending==0U && s.dropped==1U && events==0U);
 /* 会话切换、观测缺口和流超时都丢弃待定任务。 */
 for(unsigned scenario=0U;scenario<3U;++scenario){
  CHECK(ChirpService_Init(tick_ms,0U,Log));Block(0U,8100U,true);Work(20U);
  ChirpService_GetSnapshot(&s);CHECK(s.pending==1U);
  if(scenario==1U)Block(16384U,UINT64_MAX,true);
  else{
   BSP_AudioSnapshot snap={.running=true,.session=scenario==0U ? 2U:1U,.callback_count=1U,
    .last_callback_ms=scenario==0U ? tick_ms:tick_ms-1000U};
   ChirpService_Process(tick_ms,&snap);
  }
  ChirpService_GetSnapshot(&s);CHECK(s.pending==0U && s.dropped==1U && events==0U);
 }
 /* 250ms的块在读取恶意占位地址前拒绝，249ms仍可接受真实CPU样本。 */
 CHECK(ChirpService_Init(tick_ms,0U,Log));
 BSP_AudioBlock aged={.samples=pcm,.session=1U,.frame_count=8192U,.index_valid=true,
  .completed_ms=tick_ms-249U};
 borrowed=true;CHECK(ChirpService_Stage(tick_ms,&aged));borrowed=false;CHECK(ChirpService_Complete(true));
 aged.samples=(const volatile int16_t *)(uintptr_t)1U;aged.completed_ms=tick_ms-250U;
 CHECK(!ChirpService_Stage(tick_ms,&aged));CHECK(!ChirpService_Complete(true));
 ChirpService_GetSnapshot(&s);CHECK(s.valid_blocks==1U && s.rejected_blocks==1U);
 /* tick回绕与声道1使用同一份模板，不把另一个声道混入识别。 */
 tick_ms=UINT32_MAX-20U;events=0U;input_channel=1U;
 CHECK(ChirpService_Init(tick_ms,1U,Log));Block(0U,2000U,true);Work(80U);
 CHECK(events==1U && strstr(last_event,",FIRST=2000,")!=NULL);input_channel=0U;
 CHECK(!ChirpService_Init(tick_ms,2U,Log));CHECK(!ChirpService_Init(tick_ms,0U,NULL));
 CHECK(!ChirpService_Stage(tick_ms,NULL));
 puts("PASS bounded chirp service: verified copy, cross-block search, rejected End, replacement and operation budgets");
 return 0;
}
