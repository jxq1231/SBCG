#include "Detector/event_classifier.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int16_t samples[2400];
static void Chirp(void)
{
    const double rate=48000.0;
    double phase=0.0;
    for(unsigned i=0;i<2400U;++i){
        const double t=(double)i/rate;
        const double f=2000.0+4000.0*t/0.05;
        phase+=2.0*3.14159265358979323846*f/rate;
        const double envelope=i<48U?(double)i/48.0:(i>2352U?(double)(2400U-i)/48.0:1.0);
        samples[i]=(int16_t)(sin(phase)*envelope*12000.0);
    }
}
static void Clap(void)
{
    memset(samples,0,sizeof samples);
    for(unsigned i=0;i<600U;++i)samples[i]=(int16_t)((i%2U?1:-1)*(12000-16*i));
}
int main(void)
{
    EventClassResult result;
    Chirp();
    assert(EventClassifier_Classify(samples,2400,&result));
    assert(result.valid && result.type==EVENT_CLASS_CHIRP && result.quality>=50U);
    assert(result.active_frames>1200U && result.zcr_late>result.zcr_early);
    Clap();
    assert(EventClassifier_Classify(samples,2400,&result));
    assert(result.valid && result.type==EVENT_CLASS_CLAP);
    memset(samples,0,sizeof samples);
    assert(!EventClassifier_Classify(samples,2400,&result));
    assert(!EventClassifier_Classify(samples,479,&result));
    assert(!EventClassifier_Classify(NULL,2400,&result));
    puts("PASS event classifier: chirp envelope/upward ZCR vs short clap; silence/length rejection");
    return 0;
}
