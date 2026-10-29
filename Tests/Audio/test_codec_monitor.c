#include "Audio/codec_monitor.h"
#include "BSP/bsp_audio_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned reads, logs;
static BSP_AudioCodecStatus result;
static bool valid;
static char line[256];
BSP_AudioCodecStatus BSP_AudioCodec_ReadId(BSP_AudioCodecResult *r)
{
    ++reads; *r=(BSP_AudioCodecResult){0x8994U,valid,4U,2U}; return result;
}
static void Log(const char *text,int length)
{
    CHECK(length>0 && length<(int)sizeof line && text[length-2]=='\r' && text[length-1]=='\n');
    memcpy(line,text,(size_t)length); line[length]=0; ++logs;
}
static void Reset(uint32_t now_ms)
{ reads=logs=0; valid=true; result=BSP_CODEC_OK; CodecMonitor_Init(now_ms,Log); }
int main(void)
{
    Reset(0); CHECK(reads==0 && logs==0);
    CodecMonitor_Process(1999,true); CHECK(reads==0);
    CodecMonitor_Process(2000,true); CHECK(reads==1 && logs==1);
    CHECK(strstr(line,"$CODEC,SEQ=1,TICK_MS=2000,ADDR7=1A,RESULT=OK,ID=8994,"));
    CHECK(strstr(line,"HAL_ERR=00000004,READ_MS=2,OK=1,ERRORS=0\r\n"));
    CodecMonitor_Process(2000,true); CHECK(reads==1);
    CodecMonitor_Process(3999,true); CHECK(reads==1);
    CodecMonitor_Process(4000,false); CodecMonitor_Process(4020,false); CHECK(reads==1 && logs==1);
    CodecMonitor_Process(4021,true); CHECK(reads==2 && logs==2);
    CodecMonitor_Process(4021,true); CHECK(reads==2);
    puts("PASS: initialization has no I/O, two-second rate limit, DHT-low deferral and no burst");
    result=BSP_CODEC_IO_ERROR; valid=false;
    CodecMonitor_Process(6021,true);
    CHECK(!strstr(line,",ID=") && strstr(line,"RESULT=IO_ERROR,"));
    CHECK(strstr(line,"OK=2,ERRORS=1\r\n"));
    result=BSP_CODEC_BUSY; CodecMonitor_Process(8021,true);
    CHECK(strstr(line,"RESULT=BUSY,") && strstr(line,"OK=2,ERRORS=2\r\n"));
    result=BSP_CODEC_ID_MISMATCH; valid=true; CodecMonitor_Process(10021,true);
    CHECK(strstr(line,"RESULT=ID_MISMATCH,ID=8994,") && strstr(line,"OK=2,ERRORS=3\r\n"));
    result=BSP_CODEC_OK; CodecMonitor_Process(12021,true);
    CHECK(strstr(line,"SEQ=6,") && strstr(line,"OK=3,ERRORS=3\r\n"));
    CodecMonitor_Process(99000,true); CodecMonitor_Process(99000,true); CHECK(reads==7);
    puts("PASS: failed read omits ID, persistent counters survive recovery and long loop delays");
    Reset(UINT32_MAX-999U); CodecMonitor_Process(999,true); CHECK(reads==0);
    CodecMonitor_Process(1000,true); CHECK(reads==1 && strstr(line,"SEQ=1,"));
    Reset(1000); CodecMonitor_Process(2999,true); CHECK(reads==0);
    CodecMonitor_Process(3000,true); CHECK(reads==1 && strstr(line,"OK=1,ERRORS=0\r\n"));
    CodecMonitor_Init(0,NULL); CodecMonitor_Process(2000,true); CHECK(reads==1);
    puts("PASS: tick wrap, reinitialization and disabled logger are safe");
    return 0;
}
