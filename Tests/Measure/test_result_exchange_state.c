#include "Measure/result_exchange_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    RangingExchangeState status;
    RangingExchange_Init(&status);
    RangingStatusAck key={RANGING_STATUS_ACK_OK,7U,41U,5U,0U};
    assert(RangingExchange_ShouldSend(&status,0U,&key));
    RangingExchange_MarkAttempt(&status,0U,&key,false);
    assert(!RangingExchange_ShouldSend(&status,499U,&key));
    assert(!RangingExchange_OnAck(&status,&key));
    assert(RangingExchange_ShouldSend(&status,500U,&key));
    RangingExchange_MarkAttempt(&status,500U,&key,true);
    RangingStatusAck wrong=key;wrong.authority_clock_epoch++;
    assert(!RangingExchange_OnAck(&status,&wrong));
    wrong=key;wrong.measurement_id=1U;
    assert(!RangingExchange_OnAck(&status,&wrong));
    assert(RangingExchange_OnAck(&status,&key));
    assert(!RangingExchange_ShouldSend(&status,10000U,&key));
    key.state_revision++;
    assert(RangingExchange_ShouldSend(&status,10001U,&key));
    RangingExchange_MarkAttempt(&status,UINT32_MAX-100U,&key,false);
    assert(!RangingExchange_ShouldSend(&status,398U,&key));
    assert(RangingExchange_ShouldSend(&status,399U,&key));
    puts("PASS result exchange: retry cadence, exact ACK stop, revision key and time wraparound");
    return 0;
}
