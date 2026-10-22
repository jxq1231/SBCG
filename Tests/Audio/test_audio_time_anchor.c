#include "Audio/audio_time_anchor.h"
#include "BSP/bsp_audio.h"
#include "BSP/bsp_ptp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static BSP_AudioCursor cursors[2];
static bool cursor_ok[2];
static BSP_PtpTime stamp;
static BSP_PtpStatus ptp_status;
static unsigned cursor_reads,ptp_reads,logs;
static char line[256];
bool BSP_Audio_ReadCursor(BSP_AudioCursor *out)
{
    CHECK(cursor_reads<2 && ptp_reads==cursor_reads);
    *out=cursors[cursor_reads];return cursor_ok[cursor_reads++];
}
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *out)
{CHECK(cursor_reads==1 && ptp_reads==0);++ptp_reads;*out=stamp;return ptp_status;}
static void Log(const char *text,int length)
{
    CHECK(length>0 && length<256 && text[length-2]=='\r' && text[length-1]=='\n');
    memcpy(line,text,(size_t)length);line[length]=0;++logs;
}
static void Prepare(uint64_t lo,uint64_t hi,uint32_t seconds)
{
    cursors[0]=(BSP_AudioCursor){lo,1,1};cursors[1]=(BSP_AudioCursor){hi,1,1};
    cursor_ok[0]=cursor_ok[1]=true;stamp=(BSP_PtpTime){seconds,0};ptp_status=BSP_PTP_OK;
    cursor_reads=ptp_reads=0;
}
static void Failure(const char *reason)
{CHECK(strstr(line,reason) && strstr(line,",VALID=0,") && !strstr(line,"F_LO=") && !strstr(line,",SEC="));}
int main(void)
{
    Prepare(47991,47992,1);AudioTimeAnchor_Init(0,Log);
    CHECK(cursor_reads==0 && ptp_reads==0 && logs==0);
    AudioTimeAnchor_Process(999,true);AudioTimeAnchor_Process(1000,false);CHECK(logs==0 && cursor_reads==0);
    AudioTimeAnchor_Process(1000,true);CHECK(logs==1 && cursor_reads==2 && ptp_reads==1);
    CHECK(strstr(line,"$AUDIO_ANCHOR,SEQ=1,STATUS=OK,VALID=1,SESSION=1,F_LO=47991,F_HI=47992,SEC=1,NSEC=0,WIDTH=1,SYNC=0,OK=1,REJECTS=0\r\n"));
    AudioTimeAnchorObservation observation;
    CHECK(!AudioTimeAnchor_TakeObservation(NULL));
    CHECK(AudioTimeAnchor_TakeObservation(&observation));
    CHECK(observation.sequence==1 && observation.captured_ms==1000 && observation.anchor.valid && observation.anchor.frame_lo==47991);
    CHECK(!AudioTimeAnchor_TakeObservation(&observation) && !observation.anchor.valid);
    AudioTimeAnchor_Process(1000,true);CHECK(logs==1);
    Prepare(95982,95984,2);AudioTimeAnchor_Process(2000,true);CHECK(strstr(line,",OK=2,REJECTS=0"));
    Prepare(143973,143978,3);AudioTimeAnchor_Process(3000,true);Failure("STATUS=WIDE");
    CHECK(AudioTimeAnchor_TakeObservation(&observation) && observation.sequence==3 && !observation.anchor.valid);
    CHECK(observation.anchor.session==0 && observation.anchor.frame_lo==0 && observation.anchor.sec==0);
    Prepare(191964,191965,4);cursor_ok[0]=false;AudioTimeAnchor_Process(4000,true);Failure("STATUS=CURSOR");CHECK(ptp_reads==0);
    Prepare(239955,239956,5);ptp_status=BSP_PTP_BAD_CONFIG;AudioTimeAnchor_Process(5000,true);Failure("STATUS=PTP");CHECK(strstr(line,"PTP_ERR=5"));
    Prepare(287946,287947,6);cursor_ok[1]=false;AudioTimeAnchor_Process(6000,true);Failure("STATUS=CURSOR");
    Prepare(335937,335938,7);cursors[1].session=2;AudioTimeAnchor_Process(7000,true);Failure("STATUS=SESSION");
    Prepare(383928,383929,8);++cursors[1].boundary_generation;AudioTimeAnchor_Process(8000,true);Failure("STATUS=BOUNDARY");
    Prepare(431919,431918,9);AudioTimeAnchor_Process(9000,true);Failure("STATUS=ORDER");
    Prepare(479910,479911,10);stamp.nanoseconds=1000000000;AudioTimeAnchor_Process(10000,true);Failure("STATUS=PTP");CHECK(strstr(line,"PTP_ERR=6"));
    Prepare(527901,527902,11);AudioTimeAnchor_Process(11000,true);CHECK(strstr(line,"STATUS=OK,VALID=1"));
    Prepare(575892,575893,10);AudioTimeAnchor_Process(12000,true);Failure("STATUS=BACKWARD");
    Prepare(623883,623884,13);AudioTimeAnchor_Process(13000,true);Failure("STATUS=BACKWARD");
    Prepare(671874,671875,14);cursors[0].session=cursors[1].session=2;AudioTimeAnchor_Process(14000,true);CHECK(strstr(line,"STATUS=OK,VALID=1,SESSION=2"));
    Prepare(671874,671875,15);cursors[0].session=cursors[1].session=2;AudioTimeAnchor_Process(15000,true);Failure("STATUS=ORDER");
    Prepare(767856,767857,16);cursors[0].session=cursors[1].session=2;AudioTimeAnchor_Process(16000,true);CHECK(strstr(line,",OK=5,REJECTS=11\r\n"));
    puts("PASS: bracket order, one-second cadence/DHT deferral, every invalid path, stale omission and session clock-fault recovery");
    AudioTimeAnchor_Init(UINT32_MAX-499U,Log);Prepare(1,1,1);unsigned before=logs;
    AudioTimeAnchor_Process(499,true);CHECK(logs==before);AudioTimeAnchor_Process(500,true);CHECK(logs==before+1);
    AudioTimeAnchor_Init(0,Log);Prepare(UINT64_MAX-4,UINT64_MAX,UINT32_MAX);
    cursors[0].session=cursors[1].session=UINT32_MAX;stamp.nanoseconds=999999999;
    AudioTimeAnchor_Process(1000,true);CHECK(strstr(line,"F_LO=18446744073709551611,F_HI=18446744073709551615") && strlen(line)<256);
    AudioTimeAnchor_Init(0,Log);
    CHECK(!AudioTimeAnchor_TakeObservation(&observation) && !observation.anchor.valid && observation.sequence==0);
    AudioTimeAnchor_Init(0,NULL);Prepare(0,0,0);before=logs;AudioTimeAnchor_Process(1000,true);CHECK(logs==before && cursor_reads==0);
    puts("PASS: tick wrap, zero-width frame quantization, full-width fields and null logger");
    return 0;
}
