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
    /* Duplicate same-sequence ACK is not a peer restart. */
    ResultExchange_MarkSent(&s,20003U,2U);
    ResultExchange_OnAck(&s,2U);
    ResultExchange_OnPeerAckSequence(&s,1U);
    ResultExchange_OnPeerAckSequence(&s,2U);
    ResultExchange_OnPeerAckSequence(&s,2U);
    assert(!ResultExchange_ShouldSend(&s,20004U,2U));
    /* A backward ACK sequence (out-of-order older packet) is handled as reset. */
    ResultExchange_OnPeerAckSequence(&s,1U);
    assert(ResultExchange_ShouldSend(&s,20005U,2U));
    /* ID 0 is never sent. */
    assert(!ResultExchange_ShouldSend(&s,20004U,0U));
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
    puts("PASS result exchange: retry cadence, ACK stop, peer-sequence regression and domain restore");
    return 0;
}
