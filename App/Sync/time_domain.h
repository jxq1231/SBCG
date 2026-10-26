/**
 * @file        time_domain.h
 * @brief       由本地同步状态派生的共同事件时间域门控。
 * @details     A参考在TimeSync配置且未FAULT时有本地参考域；B必须同时有新鲜offset和内部锁定。
 *              本模块不读取PTP寄存器、不改变伺服；它只是给事件/测量提供一个可撤销的共同域标记。
 * @note        内部锁定不等于外部PPS/示波器精度验收；SYNC仍为0。PPS硬件资格由同步服务负责，
 *              其失败会通过TimeSync fault/清域路径反映为无效。
 */
#ifndef SBCG_TIME_DOMAIN_H
#define SBCG_TIME_DOMAIN_H

#include "Sync/time_sync.h"
#include "Sync/pps_service.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool valid;
    uint32_t id;
} TimeDomainSnapshot;

/** @brief 清空域与令牌；上电/重初始化调用。 */
void TimeDomain_Init(void);

/** @brief 设置A派生域令牌；0表示尚未获得共享令牌。 */
void TimeDomain_InitWithId(uint32_t local_id);

/** @brief B收到A事件后采用A派生令牌；不改变TimeSync/PPS资格。 */
void TimeDomain_AdoptPeerId(uint32_t peer_id);

/** @brief 仅按同步快照更新域（无PPS输入时的兼容路径）。 */
void TimeDomain_Update(const TimeSyncSnapshot *sync);

/**
 * @brief 同步+PPS资格共同更新域；pps==NULL时退化为TimeDomain_Update。
 * @param sync TimeSync快照；A/B角色及offset/internal lock参与判定。
 * @param pps PPS只读快照；非NULL时要求已配置、未FAULT且QUALIFIED/RATE_REQUALIFY。
 * @note PPS资格仍是内部证据，不是示波器±10µs验收。
 */
void TimeDomain_UpdateWithPps(const TimeSyncSnapshot *sync,const PpsServiceSnapshot *pps);

/** @brief 显式撤销域；校时/频率/断链边界使用。 */
void TimeDomain_Invalidate(void);

/** @brief 复制当前域；NULL不操作。 */
void TimeDomain_Get(TimeDomainSnapshot *out);

#endif
