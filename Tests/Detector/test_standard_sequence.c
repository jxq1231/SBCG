#include "Detector/standard_sequence.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    const StandardSequenceConfig config={STANDARD_SEQUENCE_PERIOD_NS,20000000U,5000000000U,3U,50U};
    StandardSequence sequence;StandardSequenceSnapshot snapshot;
    assert(StandardSequence_Init(&sequence,&config));
    assert(!StandardSequence_Push(&sequence,1000000000U,80U));
    assert(!StandardSequence_Push(&sequence,3000000000U,80U));
    assert(StandardSequence_Push(&sequence,5000000000U,80U));
    StandardSequence_Get(&sequence,&snapshot);
    assert(snapshot.detected && snapshot.events==3U && snapshot.quality==80U);
    assert(StandardSequence_Push(&sequence,7010000000U,70U));
    StandardSequence_Get(&sequence,&snapshot);
    assert(snapshot.events==4U && snapshot.quality==70U && snapshot.max_period_error_ns==10000000U);
    /* A low-quality event breaks the accepted sequence. */
    assert(!StandardSequence_Push(&sequence,9000000000U,40U));
    StandardSequence_Get(&sequence,&snapshot);
    assert(!snapshot.detected && snapshot.events==0U);
    assert(!StandardSequence_Push(&sequence,11000000000U,80U));
    StandardSequence_Get(&sequence,&snapshot);assert(snapshot.events==1U);
    /* Irregular interval resets; a single clap cannot become a standard sequence. */
    assert(!StandardSequence_Push(&sequence,13500000000U,80U));
    StandardSequence_Get(&sequence,&snapshot);assert(!snapshot.detected && snapshot.events==1U);
    /* One missing event is allowed as a 2-period gap and still counts as a step. */
    assert(!StandardSequence_Push(&sequence,15500000000U,80U));
    assert(StandardSequence_Push(&sequence,19500000000U,80U));
    assert(StandardSequence_Push(&sequence,21500000000U,80U));
    StandardSequence_Get(&sequence,&snapshot);assert(snapshot.detected && snapshot.events==4U);
    /* Long silence resets the run. */
    StandardSequence_Push(&sequence,31000000000U,80U);
    StandardSequence_Get(&sequence,&snapshot);assert(!snapshot.detected && snapshot.events==1U);
    StandardSequence_Reset(&sequence);StandardSequence_Get(&sequence,&snapshot);
    assert(!snapshot.detected && snapshot.events==0U);
    assert(!StandardSequence_Init(&sequence,NULL));
    assert(!StandardSequence_Init(&sequence,&(StandardSequenceConfig){0,0,0,0,0}));
    puts("PASS standard sequence: periodicity, jitter, low quality, missing step, clap and reset");
    return 0;
}
