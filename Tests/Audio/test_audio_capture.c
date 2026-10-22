#include "Audio/audio_capture.h"
#include "BSP/bsp_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static BSP_AudioSnapshot snapshot;
static unsigned stops,starts,begins,ends,logs;
static bool stop_ok=true,start_ok=true,available,end_ok=true;
static char line[256];
static int16_t data[BSP_AUDIO_HALF_ELEMENTS];
bool BSP_Audio_Stop(void){++stops;return stop_ok;}
bool BSP_Audio_Start(void){++starts;snapshot.running=start_ok;return start_ok;}
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *out){*out=snapshot;}
bool BSP_Audio_BeginBlock(BSP_AudioBlock *block)
{
    ++begins;if(!available)return false;available=false;
    *block=(BSP_AudioBlock){data,snapshot.callback_count,snapshot.last_callback_ms,0};return true;
}
bool BSP_Audio_EndBlock(const BSP_AudioBlock *block,bool processed)
{CHECK(block->samples==data && processed);++ends;return end_ok;}
static void Log(const char *text,int size)
{CHECK(size>0 && size<256 && text[size-2]=='\r' && text[size-1]=='\n');memcpy(line,text,(size_t)size);line[size]=0;++logs;}
static void Init(uint32_t now)
{
    snapshot=(BSP_AudioSnapshot){0};snapshot.derived_milli_hz=47991071;stops=starts=begins=ends=logs=0;
    stop_ok=start_ok=end_ok=true;available=false;
    AudioCapture_Init(now,Log);
}
int main(void)
{
    Init(0);CHECK(stops==1 && starts==1 && logs==1 && strstr(line,"$AUDIO_INIT,RESULT=OK,"));
    AudioCapture_Process(999,false);CHECK(begins==0 && logs==1);
    for(unsigned i=0;i<BSP_AUDIO_HALF_ELEMENTS;i+=2){data[i]=100;data[i+1]=-200;}
    snapshot.half_count=3;snapshot.full_count=2;snapshot.callback_count=5;
    snapshot.first_callback_ms=171;snapshot.last_callback_ms=854;snapshot.processed_blocks=5;available=true;
    AudioCapture_Process(999,true);CHECK(ends==1 && logs==1);
    AudioCapture_Process(1000,true);CHECK(logs==2);
    CHECK(strstr(line,"STATE=RUN,FRAMES=40960,H=3,F=2,OK=5,DROP=0,ERR=0,"));
    CHECK(strstr(line,"RATE_HZ=47976,VALID=1,P0=100,P1=200,R0=100,R1=200\r\n"));
    AudioCapture_Process(1000,true);CHECK(logs==2);
    snapshot.callback_count=11;snapshot.half_count=6;snapshot.full_count=5;snapshot.last_callback_ms=1878;
    AudioCapture_Process(2000,false);CHECK(logs==2);
    AudioCapture_Process(2020,true);CHECK(logs==3 && strstr(line,"RATE_HZ=48000,VALID=0\r\n"));
    CHECK(!strstr(line,",P0="));
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
    snapshot.hal_error=snapshot.dma_error=UINT32_MAX;snapshot.first_callback_ms=1;snapshot.last_callback_ms=999;
    available=true;AudioCapture_Process(1000,true);CHECK(strlen(line)<256 && strstr(line,"FRAMES=70368744161280,"));
    puts("PASS: tick wrap, full-width counters and bounded status formatting");
    stop_ok=false;unsigned before=starts;AudioCapture_Init(0,Log);CHECK(starts==before && strstr(line,"RESULT=ERROR,"));
    stop_ok=true;start_ok=false;AudioCapture_Init(0,Log);CHECK(strstr(line,"RESULT=ERROR,"));
    AudioCapture_Init(0,NULL);before=begins;AudioCapture_Process(1000,true);CHECK(begins==before);
    puts("PASS: stop/start failures and null logger do not start an unobserved stream");
    return 0;
}
