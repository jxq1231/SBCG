/**
 * @file        time_domain.c
 * @brief       本地同步资格 + A/B双向域握手门控。
 * @details     A在本地有效时发REQ；B采用令牌并回ACK；A收到同令牌ACK后才确认。
 * @note        纯状态复制，无硬件/网络/动态内存；双向确认不等于外部精度验收。
 */
#include "time_domain.h"
#include <stddef.h>
#include <string.h>

#define TIME_DOMAIN_DIAGNOSTIC_ID 1U
static TimeDomainSnapshot domain;
static uint32_t token_id;
static bool local_valid, peer_seen, handshake_required;
static uint32_t peer_ms, last_now_ms;

static void Apply(void)
{
    domain.local_valid=local_valid;
    domain.peer_seen=peer_seen;
    domain.id=0U;
    if(handshake_required)
    {
        domain.valid=local_valid && peer_seen && token_id!=0U;
        /* Keep the token visible while unconfirmed so A can send REQ;
           event/measurement code must still require valid. */
        domain.id=token_id;
    }
    else
    {
        domain.valid=local_valid;
        if(domain.valid)domain.id=token_id!=0U?token_id:TIME_DOMAIN_DIAGNOSTIC_ID;
    }
    domain.peer_age_ms=peer_seen?(uint32_t)(last_now_ms-peer_ms):0U;
}

static void ClearState(void)
{
    memset(&domain,0,sizeof domain);
    token_id=0U;local_valid=false;peer_seen=false;handshake_required=false;
    peer_ms=0U;last_now_ms=0U;
}

void TimeDomain_Init(void)
{
    ClearState();
}

void TimeDomain_InitWithId(uint32_t local_id)
{
    ClearState();
    token_id=local_id;
    handshake_required=true;
}

static void UpdateCore(const TimeSyncSnapshot *sync,const PpsServiceSnapshot *pps)
{
    if(sync==NULL || !sync->configured || sync->faulted ||
       (sync->role!=BOARD_ROLE_A && sync->role!=BOARD_ROLE_B))
    {
        /* Local qualification gone: drop the peer confirmation too. */
        local_valid=false;peer_seen=false;
        domain.valid=false;domain.local_valid=false;domain.peer_seen=false;
        domain.id=0U;domain.peer_age_ms=0U;
        return;
    }
    bool valid=true;
    if(sync->role==BOARD_ROLE_B)
    {
        valid=sync->offset_valid && sync->internal_locked;
    }
    if(pps!=NULL)
    {
        valid=valid && pps->configured && !pps->faulted &&
              pps->state==PPS_SERVICE_QUALIFIED;
    }
    local_valid=valid;
    if(!valid)peer_seen=false;
    Apply();
}

void TimeDomain_Update(const TimeSyncSnapshot *sync)
{
    UpdateCore(sync,NULL);
}

void TimeDomain_UpdateWithPps(const TimeSyncSnapshot *sync,const PpsServiceSnapshot *pps)
{
    UpdateCore(sync,pps);
}

bool TimeDomain_OnPeerRequest(uint32_t token,uint32_t now_ms)
{
    if(token==0U)return false;
    if(handshake_required)token_id=token;
    peer_seen=true;peer_ms=now_ms;last_now_ms=now_ms;
    Apply();
    return domain.valid;
}

bool TimeDomain_OnPeerAck(uint32_t token,uint32_t now_ms)
{
    if(token==0U || (token_id!=0U && token!=token_id))return false;
    peer_seen=true;peer_ms=now_ms;last_now_ms=now_ms;
    Apply();
    return domain.valid;
}

void TimeDomain_Tick(uint32_t now_ms)
{
    last_now_ms=now_ms;
    if(peer_seen && (uint32_t)(now_ms-peer_ms)>=TIME_DOMAIN_PEER_TIMEOUT_MS)
    { peer_seen=false; }
    Apply();
}

void TimeDomain_Invalidate(void)
{
    local_valid=false;peer_seen=false;
    domain.valid=false;domain.local_valid=false;domain.peer_seen=false;
    domain.id=0U;domain.peer_age_ms=0U;
}

void TimeDomain_Get(TimeDomainSnapshot *out)
{
    if(out!=NULL)*out=domain;
}
