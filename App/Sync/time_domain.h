/**
 * @file        time_domain.h
 * @brief       由本地同步状态和A/B域握手派生的共同事件时间域门控。
 * @details     A派生令牌；B收到REQ后采用；A收到同令牌ACK后才确认。本地TimeSync/PPS
 *              严格QUALIFIED是前提；超时、FAULT、UNQUALIFIED立即撤域。
 * @note        双向确认只证明双方看到同一会话令牌，不是PTP epoch/外部±10µs验收。
 */
#ifndef SBCG_TIME_DOMAIN_H
#define SBCG_TIME_DOMAIN_H

#include "Sync/time_sync.h"
#include "Sync/pps_service.h"
#include <stdbool.h>
#include <stdint.h>

#define TIME_DOMAIN_PEER_TIMEOUT_MS 3000U

/** @brief 门控快照；valid才允许事件UDP/测量，id为A派生会话令牌。 */
typedef struct
{
    bool valid;
    bool local_valid;
    bool peer_seen;
    uint32_t id;
    uint32_t peer_age_ms;
} TimeDomainSnapshot;

/** @brief 清空域；兼容旧路径，不要求双向握手。 */
void TimeDomain_Init(void);

/** @brief 设置A派生令牌并启用双向握手；0表示尚未获得令牌。 */
void TimeDomain_InitWithId(uint32_t local_id);

/** @brief 由同步快照更新本地资格；兼容无PPS输入路径。 */
void TimeDomain_Update(const TimeSyncSnapshot *sync);

/** @brief 同步+PPS严格QUALIFIED更新本地资格；pps==NULL时退化。 */
void TimeDomain_UpdateWithPps(const TimeSyncSnapshot *sync,const PpsServiceSnapshot *pps);

/** @brief B收到A的REQ：采用/核对令牌并记录对端时间；返回当前是否可放行。 */
bool TimeDomain_OnPeerRequest(uint32_t token,uint32_t now_ms);

/** @brief A收到B的ACK：令牌一致且本地有效时确认；返回当前是否可放行。 */
bool TimeDomain_OnPeerAck(uint32_t token,uint32_t now_ms);

/** @brief 前台周期超时检查；对端超过TIME_DOMAIN_PEER_TIMEOUT_MS撤域。 */
void TimeDomain_Tick(uint32_t now_ms);

/** @brief 显式撤销域；保留令牌供重新握手。 */
void TimeDomain_Invalidate(void);

/** @brief 复制当前域；NULL不操作。 */
void TimeDomain_Get(TimeDomainSnapshot *out);

#endif
