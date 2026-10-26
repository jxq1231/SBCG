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
    puts("PASS time domain: A reference/B internal-lock gating, fault/invalid and no cross-epoch claim");
    return 0;
}
