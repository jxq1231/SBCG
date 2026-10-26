#include "UI/status_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint16_t full[480*272+2],bands[480*272+2];
int main(int argc,char **argv){
 UI_StatusSnapshot s={.captured_ms=100,.role=UI_ROLE_A,.network=UI_NET_DOWN,.sync=UI_SYNC_LOCAL,.audio_valid=true,.audio_running=true,.candidates=10};UI_StatusText text;
 CHECK(UI_StatusFormat(&s,100,&text));CHECK(strcmp(text.distance,"-- M")==0 && strcmp(text.temperature,"-- C")==0 && strcmp(text.offset,"-- US")==0);CHECK(strcmp(text.precision,"EXT: NOT VERIFIED")==0);
 s.candidates=UINT64_MAX;CHECK(UI_StatusFormat(&s,100,&text) && strcmp(text.counts,"EVENTS 999999999999+")==0);
 s.distance_mm=999;s.measurement_valid=false;CHECK(UI_StatusFormat(&s,100,&text) && strcmp(text.distance,"-- M")==0);
 s.temperature_valid=true;s.temperature_milli_c=-500;s.sound_speed_mm_s=331000;CHECK(UI_StatusFormat(&s,100,&text) && strcmp(text.temperature,"-0.5 C")==0);
 s.temperature_milli_c=INT32_MIN;s.offset_valid=true;s.offset_x2_ns=INT64_MIN;CHECK(UI_StatusFormat(&s,100,&text));CHECK(strstr(text.offset,"RANGE"));
 CHECK(UI_StatusFormat(&s,2100,&text) && strcmp(text.network,"STALE")==0 && strcmp(text.distance,"-- M")==0);
 s.captured_ms=UINT32_MAX-100;CHECK(UI_StatusFormat(&s,99,&text) && strcmp(text.network,"NO LINK")==0);
 memset(&text,0x55,sizeof text);CHECK(!UI_StatusFormat(NULL,0,&text) && text.role[0]==0);CHECK(!UI_StatusFormat(&s,0,NULL));
 s=(UI_StatusSnapshot){.captured_ms=100,.temperature_manual=true};
 CHECK(UI_StatusFormat(&s,100,&text));CHECK(strcmp(text.temperature_source,"MANUAL")==0);
 CHECK(strcmp(text.direction,"SIDE --")==0 && strcmp(text.quality,"QUALITY --")==0);
 s.measurement_valid=true;s.distance_mm=1234;s.measurement_direction=2U;s.measurement_quality=87U;
 CHECK(UI_StatusFormat(&s,100,&text) && strcmp(text.direction,"SIDE B")==0 &&
       strcmp(text.quality,"QUALITY 87")==0);
 s.measurement_direction=1U;CHECK(UI_StatusFormat(&s,100,&text) && strcmp(text.direction,"SIDE A")==0);
 s.audio_dropped=3;s.audio_errors=2;
 CHECK(UI_StatusFormat(&s,100,&text) && strcmp(text.audio_detail,"DROP 3 ERR 2")==0);
 s.measurement_valid=true;s.distance_mm=1234;
 CHECK(UI_StatusFormat(&s,100,&text) && strcmp(text.distance,"1.234 M")==0);
 s.measurement_valid=false;CHECK(UI_StatusFormat(&s,100,&text) && strcmp(text.distance,"-- M")==0);
 CHECK(UI_RGB565(255,0,0)==0xf800 && UI_RGB565(0,255,0)==0x07e0 && UI_RGB565(0,0,255)==0x001f && UI_RGB565(255,255,255)==0xffff);
 UI_Canvas c,b;CHECK(!UI_CanvasInit(&c,full,10,480,272,480));CHECK(!UI_CanvasInit(&c,full,480*272,480,272,479));
 full[0]=full[480*272+1]=bands[0]=bands[480*272+1]=0xa55a;
 CHECK(UI_CanvasInit(&c,full+1,480*272,480,272,480));CHECK(UI_CanvasInit(&b,bands+1,480*272,480,272,480));
 s=(UI_StatusSnapshot){.captured_ms=100,.role=UI_ROLE_A,.network=UI_NET_DOWN,.sync=UI_SYNC_LOCAL,.audio_valid=true,.audio_running=true,.candidates=10};
 CHECK(UI_StatusRenderRows(&c,&s,100,0,272));for(unsigned y=0;y<272;y+=16)CHECK(UI_StatusRenderRows(&b,&s,100,(uint16_t)y,(uint16_t)(272-y<16?272-y:16)));
 CHECK(memcmp(full,bands,sizeof full)==0 && full[0]==0xa55a && full[480*272+1]==0xa55a);
 CHECK(!UI_StatusRenderRows(&c,&s,100,271,2));
 uint16_t padded[6*4];for(unsigned i=0;i<24;++i)padded[i]=0xabcd;CHECK(UI_CanvasInit(&c,padded,24,4,4,6));UI_FillRect(&c,-100,-100,UINT32_MAX,UINT32_MAX,0x1234);for(unsigned y=0;y<4;++y)for(unsigned x=0;x<6;++x)CHECK(padded[6*y+x]==(x<4?0x1234:0xabcd));
 if(argc==2){FILE*f=fopen(argv[1],"wb");CHECK(f);CHECK(fwrite(full+1,sizeof(uint16_t),480*272,f)==480*272);CHECK(fclose(f)==0);}
 puts("PASS UI: unknown/stale/negative/wrap semantics, RGB565, clipping, stride guards, split rows identical");return 0;
}
