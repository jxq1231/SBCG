/**
 * @file test_sound_capture.c
 * @brief 用真实AudioCapture/服务/内核验证唯一认领及归还后的候选输出。
 * @note BSP替身在End后擦PCM，检查不会再借用旧指针；不是DMA实物验收。
 */
#include "Audio/audio_capture.h"
#include "Detector/sound_detector_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static BSP_AudioSnapshot snapshot;
static int16_t samples[BSP_AUDIO_HALF_ELEMENTS];
static bool available,borrowed,accepted=true;
static unsigned begins,ends,candidates,logs;
static char event[256];
static uint32_t tick_ms;
static bool advance_begin,advance_snapshot,advance_summary;
static unsigned snapshot_reads;
static char audio_line[256];
/** @brief 返回模拟的新鲜HAL毫秒值；无参数，返回当前tick。 */
uint32_t BSP_Console_GetTickMs(void){return tick_ms;}

/** @brief 模拟停止，当前状态由测试拥有；无参数，返回true。 */
bool BSP_Audio_Stop(void){snapshot.running=false;return true;}
/** @brief 模拟启动；无参数，返回true并标记运行。 */
bool BSP_Audio_Start(void){snapshot.running=true;return true;}
/** @brief 复制模拟快照到非NULL out，无返回值。 */
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot*out)
{++snapshot_reads;if(advance_snapshot || (advance_summary && snapshot_reads==2U)){snapshot.last_callback_ms=++tick_ms;advance_summary=false;++snapshot.callback_count;snapshot.observed_frames+=8192;advance_snapshot=false;}*out=snapshot;}
/** @brief 唯一借用交错PCM，block为输出，返回是否有新块。 */
bool BSP_Audio_BeginBlock(BSP_AudioBlock*block)
{
 ++begins;if(!available)return false;CHECK(!borrowed);borrowed=true;available=false;
 if(advance_begin){snapshot.last_callback_ms=++tick_ms;advance_begin=false;}
 *block=(BSP_AudioBlock){.samples=samples,.session=snapshot.session,.frame_count=8192,
 .first_frame=snapshot.observed_frames-8192,.completed_ms=snapshot.last_callback_ms,.index_valid=true,.contiguous=true};return true;
}
/** @brief 返回模拟归还结果后擦除PCM；block是原借用，processed必须保留统计成功，返回真实注入结果。 */
bool BSP_Audio_EndBlock(const BSP_AudioBlock*block,bool processed)
{CHECK(borrowed && processed && block->samples==samples);borrowed=false;++ends;memset(samples,0,sizeof samples);return accepted;}
/** @brief 检查无认领中日志；text借用length字节，返回前复制事件行，无返回值。 */
static void Log(const char*text,int length)
{CHECK(!borrowed && length>1 && length<256);++logs;if(strncmp(text,"$AUDIO,",7)==0){memcpy(audio_line,text,(size_t)length);audio_line[length]=0;}if(strncmp(text,"$SOUND_CAND,",12)==0){memcpy(event,text,(size_t)length);event[length]=0;++candidates;}}
/** @brief 在第begin帧之后生成声道0测试音，不改变声道1；无返回值。 */
static void Tone(unsigned begin){for(unsigned i=begin;i<8192;++i)samples[2*i]=i%2 ? 1024:-1024;}
/** @brief 以入口now调度，允许中途模拟IRQ推进tick；allowed为DHT门控，无返回值。 */
static void Process(uint32_t now,bool allowed){tick_ms=now;AudioCapture_Process(now,allowed);}
/** @brief 验证真实入口安全接入，成功返回0。 */
int main(void)
{
 snapshot=(BSP_AudioSnapshot){.session=1};AudioCapture_Init(0,Log);CHECK(snapshot.running);
 snapshot.callback_count=1;snapshot.observed_frames=8192;snapshot.last_callback_ms=170;available=true;Tone(1024);
 Process(200,true);CHECK(ends==1 && candidates==0);
 unsigned calls=begins,lines=logs;Process(250,false);CHECK(begins==calls && logs==lines && candidates==0);
 Process(251,true);CHECK(candidates==1 && strstr(event,",FIRST=1024,CONFIRMED=1151,E_AC=1048576,PEAK=1024,"));
 snapshot.callback_count=2;snapshot.observed_frames=16384;snapshot.last_callback_ms=340;available=true;Tone(0);accepted=false;
 Process(340,true);SoundDetectorServiceSnapshot state;SoundDetectorService_GetSnapshot(&state);CHECK(state.rejected_blocks==1 && !state.armed && state.pending==0);
 snapshot.callback_count=3;snapshot.observed_frames=24576;snapshot.last_callback_ms=510;available=true;Tone(0);accepted=true;
 Process(510,true);SoundDetectorService_GetSnapshot(&state);CHECK(state.candidates==1 && candidates==1 && ends==3);
 AudioCapture_Init(600,NULL);Process(1000,true);SoundDetectorService_GetSnapshot(&state);CHECK(!state.configured);
 for(unsigned race=0;race<2;++race)
 {
  snapshot=(BSP_AudioSnapshot){.session=1};available=false;candidates=0;AudioCapture_Init(0,Log);
  snapshot.callback_count=1;snapshot.observed_frames=8192;snapshot.last_callback_ms=169;available=true;Tone(1024);
  if(race==0)advance_begin=true;else advance_snapshot=true;
  Process(169,true);SoundDetectorService_GetSnapshot(&state);
  CHECK(state.valid_blocks==1 && state.candidates==1 && state.pending==1 && state.resets==0 && state.rejected_blocks==0);
 }
 snapshot=(BSP_AudioSnapshot){.session=1};available=false;AudioCapture_Init(0,Log);
 snapshot.callback_count=1;snapshot.observed_frames=8192;snapshot.last_callback_ms=999;available=true;Tone(1024);
 snapshot_reads=0;advance_summary=true;Process(1000,true);
 CHECK(strstr(audio_line,"STATE=RUN,") && strstr(audio_line,",VALID=1,"));
 puts("PASS summary time race: second snapshot cannot falsely report stalled audio");
 puts("PASS IRQ time race: fresh tick after Begin and after snapshot preserves valid candidates");
 puts("PASS real capture integration: one Begin/End, no pre-End output, post-End PCM erased, DHT gate and failed-End evidence reset");return 0;
}
