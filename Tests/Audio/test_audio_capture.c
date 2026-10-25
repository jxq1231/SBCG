#include "Audio/audio_capture.h"
#include "BSP/bsp_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static BSP_AudioSnapshot snapshot;
static unsigned stops,starts,begins,ends,logs;
static bool stop_ok=true,start_ok=true,available,end_ok=true;
static char line[256], position[256];
static unsigned position_logs;
static uint64_t gap;
static int16_t data[BSP_AUDIO_HALF_ELEMENTS];
static uint32_t profile_tick;
static bool profile_step;
/** @brief 提供可推进的诊断HAL毫秒；测试既可用显式now_ms，也可让分段计时产生非零差。 */
uint32_t BSP_Console_GetTickMs(void)
{
    const uint32_t now=profile_tick;
    if(profile_step){++profile_tick;}
    return now;
}
bool BSP_Audio_Stop(void){++stops;return stop_ok;}
bool BSP_Audio_Start(void){++starts;snapshot.running=start_ok;return start_ok;}
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *out){*out=snapshot;}
bool BSP_Audio_BeginBlock(BSP_AudioBlock *block)
{
    ++begins;if(!available)return false;available=false;
    *block=(BSP_AudioBlock){.samples=data,.generation=snapshot.callback_count,
        .completed_ms=snapshot.last_callback_ms,.session=snapshot.session,
        .frame_count=8192U,.first_frame=snapshot.observed_frames-8192U,
        .gap_frames=gap,.index_valid=snapshot.index_faults==0U,
        .contiguous=snapshot.index_faults==0U && gap==0U};return true;
}
bool BSP_Audio_EndBlock(const BSP_AudioBlock *block,bool processed)
{CHECK(block->samples==data && processed);++ends;return end_ok;}
static void Log(const char *text,int size)
{CHECK(size>0 && size<256 && text[size-2]=='\r' && text[size-1]=='\n');if(strncmp(text,"$AUDIO_POS,",11)==0){memcpy(position,text,(size_t)size);position[size]=0;++position_logs;}
 else{memcpy(line,text,(size_t)size);line[size]=0;++logs;}}
static void Init(uint32_t now)
{
    snapshot=(BSP_AudioSnapshot){0};snapshot.derived_milli_hz=47991071;snapshot.session=1;stops=starts=begins=ends=logs=0;
    position_logs=0;position[0]=0;gap=0;
    stop_ok=start_ok=end_ok=true;available=false;
    AudioCapture_Init(now,Log);
}
int main(void)
{
    Init(0);CHECK(stops==1 && starts==1 && logs==1 && strstr(line,"$AUDIO_INIT,RESULT=OK,"));
    AudioCapture_Process(999,false);CHECK(begins==0 && logs==1);
    for(unsigned i=0;i<BSP_AUDIO_HALF_ELEMENTS;i+=2){data[i]=100;data[i+1]=-200;}
    snapshot.half_count=3;snapshot.full_count=2;snapshot.callback_count=5;
    snapshot.observed_frames=40960;
    snapshot.first_callback_ms=171;snapshot.last_callback_ms=854;snapshot.processed_blocks=5;available=true;
    AudioCapture_Process(999,true);CHECK(ends==1 && logs==1 && position_logs==0);
    AudioCapture_Process(1000,true);CHECK(logs==2 && position_logs==1);
    CHECK(strstr(line,"STATE=RUN,FRAMES=40960,H=3,F=2,OK=5,DROP=0,ERR=0,"));
    CHECK(strstr(line,"RATE_HZ=47976,VALID=1,P0=100,P1=200,R0=100,R1=200\r\n"));
    CHECK(position_logs==1 && strstr(position,"$AUDIO_POS,SEQ=1,SESSION=1,OBS_FRAMES=40960,FIRST=32768,COUNT=8192,GAP=0,INDEX_VALID=1,CONTIG=1,FAULTS=00000000,VALID=1\r\n"));
    AudioCapture_Process(1000,true);CHECK(logs==2 && position_logs==1);
    snapshot.callback_count=11;snapshot.half_count=6;snapshot.full_count=5;snapshot.last_callback_ms=1878;
    AudioCapture_Process(2000,false);CHECK(logs==2);
    AudioCapture_Process(2020,true);CHECK(logs==3 && strstr(line,"RATE_HZ=48000,VALID=0\r\n"));
    CHECK(!strstr(line,",P0="));
    CHECK(strstr(position,",FIRST=0,COUNT=0,GAP=0,INDEX_VALID=1,CONTIG=0,FAULTS=00000000,VALID=0\r\n"));
    puts("PASS: init ordering, DHT-low deferral, valid stereo levels, rate window, stale-level omission and rate limit");
    available=true;end_ok=false;snapshot.last_callback_ms=2100;AudioCapture_Process(2100,true);
    snapshot.last_callback_ms=2999;AudioCapture_Process(3020,true);CHECK(strstr(line,"VALID=0\r\n"));
    AudioCapture_Process(4020,true);CHECK(strstr(line,"STATE=STALLED,"));
    snapshot.running=false;snapshot.hal_error=16;snapshot.dma_error=8;
    AudioCapture_Process(5020,true);CHECK(strstr(line,"STATE=ERROR,") && strstr(line,"HAL=00000010,DMA=00000008,"));
    puts("PASS: overwritten levels never publish; stopped or stalled capture is explicit");
    Init(UINT32_MAX-499U);AudioCapture_Process(499,true);CHECK(logs==1);
    AudioCapture_Process(500,true);CHECK(logs==2);
    Init(0);snapshot.half_count=snapshot.full_count=snapshot.callback_count=snapshot.processed_blocks=snapshot.dropped_blocks=snapshot.error_count=UINT32_MAX;
    snapshot.observed_frames=UINT64_C(70368744161280);
    snapshot.hal_error=snapshot.dma_error=UINT32_MAX;snapshot.first_callback_ms=1;snapshot.last_callback_ms=999;
    available=true;AudioCapture_Process(1000,true);CHECK(strlen(line)<256 && strstr(line,"FRAMES=70368744161280,"));
    puts("PASS: tick wrap, full-width counters and bounded status formatting");
    Init(0);snapshot.observed_frames=UINT64_MAX-8191U;snapshot.callback_count=6;
    snapshot.last_callback_ms=999;snapshot.session=UINT32_MAX;gap=UINT64_MAX-16383U;
    available=true;AudioCapture_Process(1000,true);
    CHECK(strlen(position)<256 && strstr(position,"SESSION=4294967295,"));
    CHECK(strstr(position,"OBS_FRAMES=18446744073709543424,FIRST=18446744073709535232,"));
    CHECK(strstr(position,",INDEX_VALID=1,CONTIG=0,FAULTS=00000000,VALID=1\r\n"));
    snapshot.index_faults=2;available=true;snapshot.last_callback_ms=1999;AudioCapture_Process(2000,true);
    CHECK(strstr(position,",FIRST=0,COUNT=0,GAP=0,INDEX_VALID=0,CONTIG=0,FAULTS=00000002,VALID=0\r\n"));
    snapshot.index_faults=0;available=true;snapshot.last_callback_ms=2999;gap=0;AudioCapture_Process(3000,true);
    snapshot.session=1;snapshot.last_callback_ms=3999;AudioCapture_Process(4000,true);
    CHECK(strstr(position,",FIRST=0,COUNT=0,") && strstr(position,"VALID=0\r\n"));
    puts("PASS: numbered position, known gap, bounded 64-bit output, invalid/stale/cross-session position omitted");
    stop_ok=false;unsigned before=starts;AudioCapture_Init(0,Log);CHECK(starts==before && strstr(line,"RESULT=ERROR,"));
    stop_ok=true;start_ok=false;AudioCapture_Init(0,Log);CHECK(strstr(line,"RESULT=ERROR,"));
    AudioCapture_Init(0,NULL);before=begins;AudioCapture_Process(1000,true);CHECK(begins==before);
    puts("PASS: stop/start failures and null logger do not start an unobserved stream");
    /* 让每次HAL读取前进1ms，覆盖GetProfile、最大轮更新与分段求和。 */
    Init(0);available=true;snapshot.last_callback_ms=999;profile_tick=1000;profile_step=true;
    AudioCapture_Process(1000,true);
    AudioCaptureProfile profile;AudioCapture_GetProfile(&profile);
    CHECK(profile.total_ms>0U && profile.total_ms>=profile.stats_ms);
    CHECK(profile.total_ms>=profile.io_ms && profile.total_ms>=profile.chirp_stage_ms);
    CHECK(profile.total_ms==profile.io_ms+profile.stats_ms+profile.sound_stage_ms+
        profile.sound_process_ms+profile.chirp_stage_ms+profile.chirp_complete_ms+
        profile.chirp_process_ms+profile.log_ms+profile.other_ms);
    profile_step=false;
    puts("PASS: audio profile getter and max-loop breakdown are readable and consistent");
    return 0;
}
