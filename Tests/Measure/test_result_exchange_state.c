#include "Measure/result_exchange_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    ResultExchangeState s;
    ResultExchange_Init(&s);
    assert(ResultExchange_ShouldSend(&s,0U,1U));
    ResultExchange_MarkSent(&s,0U,1U);
    assert(!ResultExchange_ShouldSend(&s,499U,1U));
    assert(ResultExchange_ShouldSend(&s,500U,1U));
    ResultExchange_OnAck(&s,1U);
    assert(!ResultExchange_ShouldSend(&s,10000U,1U));
    /* New result ID is sent immediately despite previous ACK. */
    assert(ResultExchange_ShouldSend(&s,10001U,2U));
    ResultExchange_MarkSent(&s,10001U,2U);
    ResultExchange_OnAck(&s,2U);
    assert(!ResultExchange_ShouldSend(&s,20000U,2U));
    /* Normal B ACK sequence progress does not clear the ACK. */
    ResultExchange_OnPeerAckSequence(&s,1U);
    ResultExchange_OnPeerAckSequence(&s,2U);
    assert(!ResultExchange_ShouldSend(&s,20001U,2U));
    /* B reboot: ACK sequence regresses to 1; current result must be resent. */
    ResultExchange_OnPeerAckSequence(&s,1U);
    assert(ResultExchange_ShouldSend(&s,20002U,2U));
    ResultExchange_MarkSent(&s,20002U,2U);
    ResultExchange_OnAck(&s,2U);
    /* Domain restore also clears the ACK. */
    ResultExchange_OnDomainRestored(&s);
    assert(ResultExchange_ShouldSend(&s,20003U,2U));
    /* ID 0 is never sent. */
    assert(!ResultExchange_ShouldSend(&s,20004U,0U));
    puts("PASS result exchange: retry cadence, ACK stop, peer-sequence regression and domain restore");
    return 0;
}
