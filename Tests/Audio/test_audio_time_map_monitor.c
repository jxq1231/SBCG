#include "Audio/audio_time_map_monitor.h"
#include "Audio/audio_time_anchor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1); } } while (0)
static AudioTimeAnchorObservation pending;
static bool available;
static unsigned takes, lines;
static char output[32][256];
bool AudioTimeAnchor_TakeObservation(AudioTimeAnchorObservation *out)
{
    ++takes;
    if (!available) { return false; }
    *out=pending;available=false;return true;
}
static void Log(const char *text,int length)
{
    CHECK(lines<32 && length>0 && length<256 && text[length-2]=='\r' && text[length-1]=='\n');
    memcpy(output[lines],text,(size_t)length);output[lines++][length]=0;
}
static void Init(void)
{ AudioTimeMapMonitor_Init(Log);lines=takes=0;available=false; }
static void Feed(uint32_t seq,uint32_t now,uint32_t session,uint64_t frame,uint32_t sec)
{
    pending=(AudioTimeAnchorObservation){seq,now,{true,session,frame,frame,sec,0}};
    available=true;AudioTimeMapMonitor_Process(now,true);CHECK(!available);
}
static void Triple(uint32_t seq,uint32_t session,uint64_t base,uint32_t sec)
{
    Feed(seq,1000,session,base,sec);
    Feed(seq+1U,2000,session,base+48000U,sec+1U);
    Feed(seq+2U,3000,session,base+96000U,sec+2U);
}
int main(void)
{
    Init();AudioTimeMapMonitor_Process(1000,true);CHECK(lines==0 && takes==1);
    Triple(1,1,0,0);CHECK(lines==2);
    CHECK(strstr(output[0],"$AUDIO_MAP,SEQ=1,STATUS=OK,CORE=0,VALID=1,SESSION=1,L=1,M=2,R=3,FRAME=48000,"));
    CHECK(strstr(output[0],"EST_NS=999989583,LO_NS=999979166,HI_NS=1000000000,SYNC=0\r\n"));
    CHECK(strstr(output[1],"$AUDIO_MAP_CHECK,SEQ=1,OBS_NS=1000000000,RES_NS=10417,SYNC=0\r\n"));
    AudioTimeMapMonitor_Process(4000,true);CHECK(lines==2);
    Triple(4,1,144000,3);CHECK(lines==4 && strstr(output[2],"SEQ=2,STATUS=OK"));
    Init();Feed(1,1000,1,0,0);
    pending=(AudioTimeAnchorObservation){2,2000,{true,1,48000,48000,0,999980000}};available=true;
    AudioTimeMapMonitor_Process(2000,true);Feed(3,3000,1,96000,2);
    CHECK(lines==2 && strstr(output[1],"RES_NS=-9583,"));
    Init();Feed(1,1000,1,0,0);AudioTimeMapMonitor_Init(Log);
    Feed(2,2000,1,48000,1);Feed(3,3000,1,96000,2);CHECK(lines==0);
    Feed(4,4000,1,144000,3);CHECK(lines==2 && strstr(output[0],"L=2,M=3,R=4,"));
    Init();Feed(1,1000,1,0,0);
    pending=(AudioTimeAnchorObservation){2,2000,{false,0,0,0,0,0}};available=true;
    const unsigned before=takes;AudioTimeMapMonitor_Process(2000,false);CHECK(available && takes==before && lines==0);
    AudioTimeMapMonitor_Process(2000,true);CHECK(lines==1 && strstr(output[0],"STATUS=ANCHOR,CORE=0,VALID=0,SRC=2,"));
    CHECK(!strstr(output[0],"FRAME=") && !strstr(output[0],"_NS="));
    Triple(3,1,96000,2);CHECK(lines==3 && strstr(output[1],"L=3,M=4,R=5"));
    Init();Feed(1,1000,1,0,0);Feed(3,3000,1,96000,2);
    CHECK(lines==1 && strstr(output[0],"STATUS=SEQUENCE"));
    Feed(4,4000,1,144000,3);Feed(5,5000,1,192000,4);CHECK(lines==3);
    Init();Feed(1,1000,1,0,0);Feed(2,2000,2,48000,1);
    CHECK(lines==1 && strstr(output[0],"STATUS=SESSION"));
    Feed(3,3000,2,96000,2);Feed(4,4000,2,144000,3);CHECK(lines==3);
    Init();Feed(1,1000,1,0,0);
    pending=(AudioTimeAnchorObservation){2,1000,{true,1,48000,48000,1,0}};available=true;
    AudioTimeMapMonitor_Process(1251,true);CHECK(lines==1 && strstr(output[0],"STATUS=STALE"));
    Triple(3,1,96000,2);CHECK(lines==3);
    Init();Feed(1,1000,1,0,5);Feed(2,2000,1,48000,4);
    CHECK(lines==1 && strstr(output[0],"STATUS=ORDER"));
    Triple(3,1,96000,6);CHECK(lines==3);
    Init();Feed(1,1000,1,0,0);Feed(2,2000,1,48000,1);Feed(3,3000,1,192000,2);
    CHECK(lines==1 && strstr(output[0],"STATUS=CORE,CORE=6,VALID=0"));
    Triple(4,1,240000,3);CHECK(lines==3);
    Init();Triple(UINT32_MAX-1U,UINT32_MAX,UINT64_MAX-200000U,UINT32_MAX-2U);
    CHECK(lines==2 && strstr(output[0],"L=4294967294,M=4294967295,R=0"));
    CHECK(strstr(output[0],"SESSION=4294967295") && strlen(output[0])<256);
    Init();pending=(AudioTimeAnchorObservation){1,UINT32_MAX-99U,{true,1,0,0,0,0}};available=true;
    AudioTimeMapMonitor_Process(150,true);CHECK(lines==0 && !available);
    pending=(AudioTimeAnchorObservation){2,UINT32_MAX-99U,{true,1,48000,48000,1,0}};available=true;
    AudioTimeMapMonitor_Process(151,true);CHECK(lines==1 && strstr(output[0],"STATUS=STALE"));
    Init();AudioTimeMapMonitor_Init(NULL);available=true;AudioTimeMapMonitor_Process(1,true);CHECK(takes==0 && available);
    puts("PASS map monitor: real core, fixed triples, resets, DHT gating, stale/seq wrap and bounded logs");
    return 0;
}
