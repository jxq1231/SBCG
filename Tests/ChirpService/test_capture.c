/**
 * @file        test_capture.c
 * @brief       用真实AudioCapture接入与20秒WAV逐块验证唯一借用、归还后匹配和工作预算。
 * @details     主机BSP在End时立即擦掉DMA样本；匹配必须使用已验证CPU副本。
 * @note        文件只含测试用单声道PCM16，不连接或播放声音，不是板端实时证据。
 */
#include "Audio/audio_capture.h"
#include "Detector/chirp_service.h"
#include "Detector/chirp_template.h"
#include "BSP/bsp_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static BSP_AudioSnapshot snapshot;
static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS],mono[BSP_AUDIO_HALF_FRAMES];
static uint32_t tick_ms;
static bool available,borrowed,accepted=true;
static unsigned begin_calls,end_calls,matches,logs;
/** @brief 测试断言，条件只求值一次，失败打印行号并结束。 */
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
/** @brief 返回当前受控HAL毫秒；不改变时间。 */
uint32_t BSP_Console_GetTickMs(void){return tick_ms;}
/** @brief 模拟停止采集，返回true并更新测试快照。 */
bool BSP_Audio_Stop(void){snapshot.running=false;return true;}
/** @brief 模拟启动采集，返回true并更新测试快照。 */
bool BSP_Audio_Start(void){snapshot.running=true;return true;}
/** @brief 将测试拥有的快照复制到非NULL out，无返回值。 */
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *out){*out=snapshot;}
/** @brief 建立唯一模拟借用，block为独占输出，返回是否有新块。 */
bool BSP_Audio_BeginBlock(BSP_AudioBlock *block)
{
 ++begin_calls;if(!available)return false;CHECK(!borrowed);available=false;borrowed=true;
 *block=(BSP_AudioBlock){.samples=pcm,.session=snapshot.session,.frame_count=8192U,
  .first_frame=snapshot.observed_frames-8192U,.completed_ms=snapshot.last_callback_ms,
  .index_valid=true,.contiguous=true};return true;
}
/** @brief 检查原借用与统计结果后擦PCM；accepted为注入的真实归还结果。 */
bool BSP_Audio_EndBlock(const BSP_AudioBlock *block,bool processed)
{
 CHECK(borrowed && processed && block->samples==pcm);borrowed=false;++end_calls;
 memset(pcm,0,sizeof pcm);return accepted;
}
/** @brief 验证非借用期间的有界日志并核对10次WAV事件坐标；text借用length字节，无返回值。 */
static void Log(const char *text,int length)
{
 CHECK(!borrowed && length>0 && length<256);++logs;
 if(strncmp(text,"$CHIRP,",7)==0){
  char line[256];memcpy(line,text,(size_t)length);line[length]=0;
  const char *first=strstr(line,",FIRST=");CHECK(first!=NULL);
  CHECK(strtoull(first+7,NULL,10)==48000ULL+96000ULL*matches);
  CHECK(strstr(line,",LOCAL=1,SYNC=0")!=NULL);++matches;
 }
}
/**
 * @brief 回放命令行给定的PCM文件，通过真实入口检验全部匹配与归还边界。
 * @param argc 为2或3，第三参数可指定每171ms块内的前台调用次数。
 * @param argv argv[1]为原始小端PCM16文件路径，argv[2]可为1..64的调度次数，均为进程期借用。
 * @return 0表示全部断言通过；失败立即退出非零。
 * @note Windows原生测试；不以模拟tick作为真实耗时。
 */
int main(int argc,char **argv)
{
 CHECK(argc==2 || argc==3);
 const unsigned foreground_calls=argc==3 ? (unsigned)strtoul(argv[2],NULL,10):64U;
 CHECK(foreground_calls>=1U && foreground_calls<=64U);
 FILE *file=fopen(argv[1],"rb");CHECK(file!=NULL);
 snapshot=(BSP_AudioSnapshot){.session=1U};AudioCapture_Init(0U,Log);
 ChirpServiceSnapshot state;ChirpService_GetSnapshot(&state);
 CHECK(state.configured);
 for(unsigned block=0U;;++block){
  const size_t count=fread(mono,sizeof mono[0],8192U,file);if(count==0U)break;
  memset(pcm,0,sizeof pcm);for(size_t i=0U;i<count;++i)pcm[2U*i]=mono[i];
  tick_ms=171U*(block+1U);snapshot.last_callback_ms=tick_ms;
  ++snapshot.callback_count;snapshot.observed_frames+=8192U;available=true;
  const unsigned old_begin=begin_calls,old_logs=logs;
  AudioCapture_Process(tick_ms,false);CHECK(begin_calls==old_begin && logs==old_logs);
  AudioCapture_Process(tick_ms,true);CHECK(end_calls==block+1U);
  for(unsigned call=1U;call<foreground_calls;++call){
   tick_ms=171U*(block+1U)+171U*call/foreground_calls;
   AudioCapture_Process(tick_ms,true);ChirpService_GetSnapshot(&state);
   CHECK(state.last_scanned<=CHIRP_SERVICE_SCAN_BUDGET && state.last_scores<=8U);
  }
 }
 CHECK(!ferror(file));CHECK(fclose(file)==0);CHECK(matches==10U);
 ChirpService_GetSnapshot(&state);
 CHECK(state.rejected_blocks==0U && state.skipped_blocks==0U && state.dropped==0U && state.errors==0U);
 CHECK(state.matches==10U && state.max_scores==8U && state.max_scanned==CHIRP_SERVICE_SCAN_BUDGET);
 const unsigned valid_end_calls=end_calls;
 /* 失败End不能把刚复制的模板变成匹配；结束后不再依赖已擦DMA。 */
 AudioCapture_Init(tick_ms,Log);matches=0U;accepted=false;memset(pcm,0,sizeof pcm);
 for(unsigned i=0U;i<2400U;++i)pcm[2U*(2000U+i)]=ChirpTemplate_PCM[i];
 snapshot.last_callback_ms=++tick_ms;++snapshot.callback_count;snapshot.observed_frames+=8192U;available=true;
 AudioCapture_Process(tick_ms,true);
 for(unsigned i=0U;i<80U;++i)AudioCapture_Process(++tick_ms,true);
 ChirpService_GetSnapshot(&state);CHECK(state.rejected_blocks==1U && matches==0U);
 printf("PASS real AudioCapture: 10/10 exact file positions, %u validated End calls, %u calls/171ms, wiped DMA, DHT gate and rejected End\n",valid_end_calls,foreground_calls);
 return 0;
}
