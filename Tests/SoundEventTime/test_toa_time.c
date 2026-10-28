#include "Detector/sound_event_time.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static char output[32768];
static size_t used;
static void Log(const char*s,int n){assert(n>0 && n<256);assert(used+(size_t)n<sizeof output);memcpy(output+used,s,(size_t)n);used+=(size_t)n;output[used]=0;}
static void Init(void){used=0;output[0]=0;SoundEventTime_Init(Log);}
static AudioTimeAnchorObservation Anchor(uint32_t seq,uint32_t ms,uint32_t session,uint64_t frame,uint32_t sec){return (AudioTimeAnchorObservation){seq,ms,{true,session,frame,frame,sec,0}};}
static SoundCandidate Candidate(uint64_t first){SoundCandidate c={0};c.session=1;c.onset_frame=first;c.confirmed_frame=first+127;return c;}
static void Observe(uint32_t seq,uint32_t ms,uint64_t frame,uint32_t sec){AudioTimeAnchorObservation a=Anchor(seq,ms,1,frame,sec);SoundEventTime_Observe(ms,&a);}
static void Pair(void){Observe(1,1000,48000,1);Observe(2,2000,96000,2);}
static void Process(uint32_t ms){const BSP_AudioSnapshot s={.running=true,.callback_count=20,.session=1,.last_callback_ms=ms};SoundEventTime_Process(ms,&s);}
static SoundToaPosition Position(uint64_t frame,uint16_t q16){return (SoundToaPosition){SOUND_TOA_VALID,frame,q16,0U,{0,0,0,0}};}
static bool Has(const char *text){return strstr(output,text)!=NULL;}

int main(void)
{
    const SoundCandidate c=Candidate(71999);
    /* Q16=0 must keep FIRST+1 output and add the same model value in the independent line. */
    Init();Pair();SoundToaPosition p=Position(71999U,0U);
    assert(SoundEventTime_SubmitToa(2000,1,&c,&p));Process(2250);
    assert(Has("$SOUND_TIME,SEQ=1,SESSION=1,FIRST=71999,STATUS=OK"));
    assert(Has("$SOUND_TOA_TIME,SEQ=1,SESSION=1,FRAME=72000,Q16=0,STATUS=OK"));
    assert(Has("EST_NS=1499989583,LO_NS=1499979166,HI_NS=1500000000"));
    assert(Has("$SOUND_TOA_STATE,") && Has("OK=1"));
    SoundEventTimeEvent event;
    assert(SoundEventTime_TakeEvent(&event));
    assert(event.sequence==1U && event.session==1U && event.frame==72000U &&
           event.fraction_q16==0U && event.time_ns==1499989583U);
    assert(!SoundEventTime_TakeEvent(&event) && event.time_ns==0U);

    /* Fractional Q16 coordinate is mapped by the real core, not by a double absolute frame. */
    Init();Pair();p=Position(71999U,16384U);
    assert(SoundEventTime_SubmitToa(2000,1,&c,&p));Process(2250);
    assert(Has("FRAME=72000,Q16=16384,STATUS=OK"));
    assert(Has("EST_NS=1499994792,LO_NS=1499984375,HI_NS=1500005209"));

    /* Missing TOA is explicit: no model/anchors, and no FIRST+1 fallback in the TOA line. */
    Init();Pair();p=(SoundToaPosition){SOUND_TOA_MISSING,0U,0U,0U,{0,0,0,0}};
    assert(SoundEventTime_SubmitToa(2000,1,&c,&p));Process(2250);
    assert(Has("$SOUND_TIME,SEQ=1") && Has("STATUS=OK"));
    assert(Has("$SOUND_TOA_TIME,SEQ=1,SESSION=1,STATUS=TOA_MISSING,CORE=0"));
    assert(!Has("$SOUND_TOA_TIME") || !strstr(output,"$SOUND_TOA_TIME,SEQ=1,SESSION=1,FRAME="));

    /* Invalid and overflowing positions are rejected without a model time. */
    Init();Pair();p=(SoundToaPosition){SOUND_TOA_INVALID,0U,0U,0U,{0,0,0,0}};
    assert(SoundEventTime_SubmitToa(2000,1,&c,&p));Process(2250);
    assert(Has("STATUS=TOA_INVALID") && !Has("$SOUND_TOA_TIME,SEQ=1,SESSION=1,FRAME="));
    Init();Pair();p=Position(UINT64_MAX,0U);
    assert(SoundEventTime_SubmitToa(2000,1,&c,&p));Process(2250);
    assert(Has("STATUS=TOA_INVALID"));

    /* Clock/stream invalidation must suppress both legacy and TOA success fields. */
    Init();Pair();assert(SoundEventTime_SubmitToa(2000,1,&c,&p));
    SoundEventTime_Invalidate(SOUND_TIME_CLOCK);Process(2250);
    assert(Has("$SOUND_TIME,SEQ=1") && Has("STATUS=CLOCK"));
    assert(Has("$SOUND_TOA_TIME,SEQ=1") && Has("STATUS=CLOCK") && !Has("EST_NS="));
    assert(!SoundEventTime_TakeEvent(&event));

    /* Queue capacity and null position remain explicit errors. */
    Init();Pair();for(uint32_t i=1U;i<=4U;++i)assert(SoundEventTime_SubmitToa(2000,i,&c,&p));
    assert(!SoundEventTime_SubmitToa(2000,5,&c,&p));
    assert(!SoundEventTime_SubmitToa(2000,6,&c,NULL));
    SoundEventTimeSnapshot snap;SoundEventTime_GetSnapshot(&snap);
    assert(snap.toa_queued==4U && snap.dropped==2U && snap.pending==4U);

    Init();assert(!SoundEventTime_SubmitToa(2000,1,NULL,&p));
    SoundEventTime_GetSnapshot(NULL);
    puts("PASS TOA time: Q16, missing/invalid, legacy preservation, invalidation and bounded queue");
    return 0;
}
