#include "Detector/sound_event_time.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
static void Log(const char *s,int n){(void)n;if(s[0] && s[11]==',')fputs(s,stdout);}
int main(void){uint64_t base,first;uint32_t sec,width,seq=0;char b0[32],b1[32],b2[32],b3[32];
 while(scanf("%31s %31s %31s %31s",b0,b1,b2,b3)==4){
  base=strtoull(b0,NULL,10);first=strtoull(b1,NULL,10);sec=(uint32_t)strtoul(b2,NULL,10);width=(uint32_t)strtoul(b3,NULL,10);
  ++seq;SoundEventTime_Init(Log);
  AudioTimeAnchorObservation a={1,1000,{true,1,base,base+width,sec,0}},b={2,2000,{true,1,base+48000,base+48000+width,sec+1,0}};
  SoundEventTime_Observe(1000,&a);SoundEventTime_Observe(2000,&b);
  SoundCandidate c={.session=1,.onset_frame=first,.confirmed_frame=first+127};
  if(!SoundEventTime_Submit(2000,seq,&c))return 2;
  BSP_AudioSnapshot snapshot={.running=true,.session=1,.callback_count=2,.last_callback_ms=2250};SoundEventTime_Process(2250,&snapshot);
 }
 return 0;
}
