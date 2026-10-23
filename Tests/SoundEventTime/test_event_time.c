#include "Detector/sound_event_time.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#undef assert
#define assert(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static char output[16384];
static size_t used;
static void Log(const char *s,int n){assert(n>0 && n<256);assert(used+(size_t)n<sizeof output);memcpy(output+used,s,(size_t)n);used+=(size_t)n;output[used]=0;}
static AudioTimeAnchorObservation Anchor(uint32_t seq,uint32_t ms,uint32_t session,uint64_t frame,uint32_t sec){return (AudioTimeAnchorObservation){seq,ms,{true,session,frame,frame,sec,0}};}
static SoundCandidate Candidate(uint64_t first){SoundCandidate c={0};c.session=1;c.onset_frame=first;c.confirmed_frame=first+127;return c;}
static void Init(void){used=0;output[0]=0;SoundEventTime_Init(Log);}
static void Process(uint32_t ms){const BSP_AudioSnapshot s={.running=true,.callback_count=20,.session=1,.last_callback_ms=ms};SoundEventTime_Process(ms,&s);}
static void Observe(uint32_t seq,uint32_t ms,uint64_t frame,uint32_t sec){AudioTimeAnchorObservation a=Anchor(seq,ms,1,frame,sec);SoundEventTime_Observe(ms,&a);}
static void Pair(void){Observe(1,1000,48000,1);Observe(2,2000,96000,2);}
int main(void){
 Init();Observe(1,1000,48000,1);SoundCandidate c=Candidate(71999);assert(SoundEventTime_Submit(1250,1,&c));Process(1500);assert(strstr(output,"$SOUND_TIME,")==NULL);Observe(2,2000,96000,2);Process(2000);assert(strstr(output,"STATUS=OK") && strstr(output,"FIRST=71999") && strstr(output,"EST_NS=1499989583,LO_NS=1499979166,HI_NS=1500000000"));
 Init();Pair();assert(SoundEventTime_Submit(2000,1,&c));SoundEventTime_Invalidate(SOUND_TIME_CLOCK);assert(used==0);Process(2250);assert(strstr(output,"STATUS=CLOCK") && !strstr(output,"EST_NS="));
 Init();Pair();assert(SoundEventTime_Submit(2000,1,&c));SoundEventTime_Invalidate(SOUND_TIME_STREAM);Process(2250);assert(strstr(output,"STATUS=STREAM") && !strstr(output,"EST_NS="));
 Init();Observe(1,1000,48000,1);assert(SoundEventTime_Submit(1100,1,&c));Process(3600);assert(strstr(output,"STATUS=TIMEOUT") && !strstr(output,"EST_NS="));
 Init();Pair();c=Candidate(99);assert(SoundEventTime_Submit(2000,1,&c));Process(2250);assert(strstr(output,"STATUS=NO_LEFT"));
 Init();Pair();c=Candidate(71999);for(uint32_t i=1;i<=4;++i)assert(SoundEventTime_Submit(2000,i,&c));assert(!SoundEventTime_Submit(2000,5,&c));Process(2250);SoundEventTimeSnapshot d;SoundEventTime_GetSnapshot(&d);assert(d.queued==4 && d.ok==1 && d.pending==3 && d.dropped==1);Process(2251);SoundEventTime_GetSnapshot(&d);assert(d.pending==3);
 Init();Pair();assert(SoundEventTime_Submit(2000,2,&c));assert(!SoundEventTime_Submit(2000,2,&c));c.onset_frame=UINT64_MAX;assert(!SoundEventTime_Submit(2000,3,&c));assert(!SoundEventTime_Submit(2000,4,NULL));
 Init();Pair();c=Candidate(71999);assert(SoundEventTime_Submit(2000,1,&c));AudioTimeAnchorObservation a=Anchor(4,2500,1,120000,3);SoundEventTime_Observe(2500,&a);Process(2750);assert(strstr(output,"STATUS=GAP") && !strstr(output,"EST_NS="));
 Init();Pair();assert(SoundEventTime_Submit(2000,1,&c));a=Anchor(3,2500,2,144000,3);SoundEventTime_Observe(2500,&a);Process(2750);assert(strstr(output,"STATUS=SESSION") && !strstr(output,"EST_NS="));
 Init();Pair();assert(SoundEventTime_Submit(2000,1,&c));a=Anchor(3,2500,1,144000,3);SoundEventTime_Observe(2751,&a);Process(3000);assert(strstr(output,"STATUS=ANCHOR"));
 Init();Pair();assert(SoundEventTime_Submit(2000,1,&c));BSP_AudioSnapshot bad={.running=true,.callback_count=2,.session=1,.last_callback_ms=2250,.index_faults=1};SoundEventTime_Process(2250,&bad);assert(strstr(output,"STATUS=STREAM") && !strstr(output,"EST_NS="));
 Init();assert(SoundEventTime_Submit(UINT32_MAX-1000,1,&c));Process(500);assert(strstr(output,"$SOUND_TIME,")==NULL);Process(1600);assert(strstr(output,"STATUS=TIMEOUT"));
 Init();Pair();assert(SoundEventTime_Submit(10000,1,&c));Process(10250);assert(strstr(output,"STATUS=STALE") && !strstr(output,"EST_NS="));
 Init();SoundEventTime_Init(NULL);assert(!SoundEventTime_Submit(2000,1,&c));SoundEventTime_GetSnapshot(NULL);
 puts("PASS event time: FIRST+1, delayed right anchor, clock/stream/session/gap invalidation, timeout, no-left, queues, rate, wrap and bounded logs");return 0;
}
