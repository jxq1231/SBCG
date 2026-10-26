#include "Sync/time_domain.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static TimeSyncSnapshot Sync(BoardRole role,bool configured,bool faulted,bool offset,bool locked)
{
    TimeSyncSnapshot s={0};s.role=role;s.configured=configured;s.faulted=faulted;
    s.offset_valid=offset;s.internal_locked=locked;return s;
}
int main(void)
{
    TimeDomainSnapshot out;
    TimeDomain_Init();TimeDomain_Get(&out);assert(!out.valid && out.id==0U);
    TimeSyncSnapshot s=Sync(BOARD_ROLE_A,true,false,false,false);
    TimeDomain_Update(&s);TimeDomain_Get(&out);assert(out.valid && out.id==1U);
    s.faulted=true;TimeDomain_Update(&s);TimeDomain_Get(&out);assert(!out.valid && out.id==0U);
    s=Sync(BOARD_ROLE_B,true,false,true,false);
    TimeDomain_Update(&s);TimeDomain_Get(&out);assert(!out.valid);
    s.internal_locked=true;TimeDomain_Update(&s);TimeDomain_Get(&out);assert(out.valid && out.id==1U);
    s.offset_valid=false;TimeDomain_Update(&s);TimeDomain_Get(&out);assert(!out.valid);
    s=Sync(BOARD_ROLE_UNCONFIGURED,true,false,true,true);
    TimeDomain_Update(&s);TimeDomain_Get(&out);assert(!out.valid);
    TimeDomain_Update(NULL);TimeDomain_Get(&out);assert(!out.valid);
    s=Sync(BOARD_ROLE_B,true,false,true,true);TimeDomain_Update(&s);
    TimeDomain_Invalidate();TimeDomain_Get(&out);assert(!out.valid && out.id==0U);
    TimeDomain_Get(NULL);
    TimeSyncSnapshot a=Sync(BOARD_ROLE_A,true,false,false,false);
    PpsServiceSnapshot pps={0};
    pps.configured=true;pps.qualified=true;pps.state=PPS_SERVICE_QUALIFIED;
    TimeDomain_UpdateWithPps(&a,&pps);TimeDomain_Get(&out);assert(out.valid);
    pps.qualified=false;pps.state=PPS_SERVICE_UNQUALIFIED;
    TimeDomain_UpdateWithPps(&a,&pps);TimeDomain_Get(&out);assert(!out.valid);
    pps.qualified=true;pps.state=PPS_SERVICE_QUALIFIED;pps.faulted=true;
    TimeDomain_UpdateWithPps(&a,&pps);TimeDomain_Get(&out);assert(!out.valid);
    pps.faulted=false;pps.configured=false;
    TimeDomain_UpdateWithPps(&a,&pps);TimeDomain_Get(&out);assert(!out.valid);
    TimeSyncSnapshot b=Sync(BOARD_ROLE_B,true,false,true,true);
    pps.configured=true;pps.qualified=true;pps.faulted=false;pps.state=PPS_SERVICE_QUALIFIED;
    TimeDomain_UpdateWithPps(&b,&pps);TimeDomain_Get(&out);assert(out.valid);
    pps.state=PPS_SERVICE_RATE_REQUALIFY;TimeDomain_UpdateWithPps(&b,&pps);
    TimeDomain_Get(&out);assert(!out.valid);
    TimeDomain_UpdateWithPps(&b,NULL);TimeDomain_Get(&out);assert(out.valid);
    TimeDomain_UpdateWithPps(NULL,&pps);TimeDomain_Get(&out);assert(!out.valid);
    /* A-derived token is shared with B; invalidation retains it for re-lock. */
    pps.state=PPS_SERVICE_QUALIFIED;pps.qualified=true;pps.faulted=false;pps.configured=true;
    TimeDomain_InitWithId(0x12345678U);
    TimeDomain_UpdateWithPps(&a,&pps);TimeDomain_Get(&out);
    assert(!out.valid && out.local_valid && !out.peer_seen);
    assert(TimeDomain_OnPeerAck(0x12345678U,100U));
    TimeDomain_Get(&out);assert(out.valid && out.id==0x12345678U);
    TimeDomain_Invalidate();TimeDomain_Get(&out);assert(!out.valid && out.id==0U);
    TimeDomain_UpdateWithPps(&a,&pps);TimeDomain_Get(&out);assert(!out.valid);
    assert(TimeDomain_OnPeerAck(0xBADU,101U)==false);
    assert(TimeDomain_OnPeerAck(0x12345678U,102U));
    assert(TimeDomain_OnPeerAck(0x12345678U,103U));
    TimeDomain_Tick(103U+TIME_DOMAIN_PEER_TIMEOUT_MS);
    TimeDomain_Get(&out);assert(!out.valid && out.local_valid && !out.peer_seen);
    TimeDomain_InitWithId(0U);
    TimeDomain_UpdateWithPps(&a,&pps);TimeDomain_Get(&out);
    assert(out.local_valid && !out.valid && out.id==0U);
    puts("PASS time domain: A/B sync+PPS gating, bidirectional token and timeout");
    return 0;
}
