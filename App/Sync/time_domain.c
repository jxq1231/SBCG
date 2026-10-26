/**
 * @file        time_domain.c
 * @brief       共同事件时间域门控实现。
 * @details     A参考/B内部锁定判定；域ID是协议常量，不是A/B原始PTP epoch比较。
 * @note        纯状态复制，无硬件/网络/动态内存。
 */
#include "time_domain.h"
#include <stddef.h>
#include <string.h>

#define TIME_DOMAIN_DIAGNOSTIC_ID 1U
static TimeDomainSnapshot domain;

void TimeDomain_Init(void)
{
    memset(&domain, 0, sizeof domain);
}

static void UpdateCore(const TimeSyncSnapshot *sync,const PpsServiceSnapshot *pps)
{
    if (sync == NULL || !sync->configured || sync->faulted ||
        (sync->role != BOARD_ROLE_A && sync->role != BOARD_ROLE_B))
    {
        TimeDomain_Invalidate();
        return;
    }
    bool valid = true;
    if (sync->role == BOARD_ROLE_B)
    {
        valid = sync->offset_valid && sync->internal_locked;
    }
    if (pps != NULL)
    {
        valid = valid && pps->configured && !pps->faulted && pps->qualified;
    }
    domain.valid = valid;
    domain.id = valid ? TIME_DOMAIN_DIAGNOSTIC_ID : 0U;
}

void TimeDomain_Update(const TimeSyncSnapshot *sync)
{
    UpdateCore(sync,NULL);
}

void TimeDomain_UpdateWithPps(const TimeSyncSnapshot *sync,const PpsServiceSnapshot *pps)
{
    UpdateCore(sync,pps);
}

void TimeDomain_Invalidate(void)
{
    domain.valid = false;
    domain.id = 0U;
}

void TimeDomain_Get(TimeDomainSnapshot *out)
{
    if (out != NULL) { *out = domain; }
}
