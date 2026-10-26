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
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool valid;
    uint32_t id;
} TimeDomainSnapshot;

/** @brief 清空域；上电/重初始化调用。 */
void TimeDomain_Init(void);

/** @brief 由同步快照更新域；A要求配置且未FAULT，B还要求offset_valid/internal_locked。 */
void TimeDomain_Update(const TimeSyncSnapshot *sync);

/** @brief 显式撤销域；校时/频率/断链边界使用。 */
void TimeDomain_Invalidate(void);

/** @brief 复制当前域；NULL不操作。 */
void TimeDomain_Get(TimeDomainSnapshot *out);

#endif
