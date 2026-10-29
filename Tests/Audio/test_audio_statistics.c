#include "Audio/audio_statistics.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
int main(void)
{
    AudioStatistics s, saved;
    int16_t zero[8]={0};
    CHECK(AudioStatistics_Compute(zero,4,&s));
    for(unsigned c=0;c<2;++c) { CHECK(s.channel[c].minimum==0 && s.channel[c].maximum==0 && s.channel[c].peak==0 && s.channel[c].rms==0); }
    int16_t stereo[]={3,-4,-3,4,3,-4,-3,4};
    CHECK(AudioStatistics_Compute(stereo,4,&s));
    CHECK(s.channel[0].minimum==-3 && s.channel[0].maximum==3 && s.channel[0].peak==3 && s.channel[0].rms==3);
    CHECK(s.channel[1].minimum==-4 && s.channel[1].maximum==4 && s.channel[1].peak==4 && s.channel[1].rms==4);
    int16_t extremes[]={INT16_MIN,INT16_MAX,INT16_MIN,INT16_MAX};
    CHECK(AudioStatistics_Compute(extremes,2,&s));
    CHECK(s.channel[0].peak==32768 && s.channel[0].rms==32768 && s.channel[0].minimum==INT16_MIN);
    CHECK(s.channel[1].peak==32767 && s.channel[1].rms==32767);
    int16_t impulse[]={0,1,4,2,0,3,0,4};
    CHECK(AudioStatistics_Compute(impulse,4,&s));
    CHECK(s.channel[0].rms==2 && s.channel[1].rms==2);
    static int16_t large[16384];
    for(unsigned i=0;i<16384;++i) { large[i]=INT16_MIN; }
    CHECK(AudioStatistics_Compute(large,8192,&s));
    CHECK(s.channel[0].rms==32768 && s.channel[1].rms==32768);
    saved=s;
    CHECK(!AudioStatistics_Compute(NULL,1,&s));
    CHECK(!AudioStatistics_Compute(zero,0,&s));
    CHECK(!AudioStatistics_Compute(zero,65536,&s));
    CHECK(!AudioStatistics_Compute(zero,1,NULL));
    CHECK(memcmp(&saved,&s,sizeof s)==0);
    puts("PASS: stereo order, zero/DC/full-scale/impulse, integer RMS, 64-bit energy and invalid input");
    return 0;
}
