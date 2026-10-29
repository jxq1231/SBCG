/**
 * @file        bsp_itcm_probe.h
 * @brief       一次性验证CPU能否对16KB ITCM做普通数据load/store。
 * @details     仅用于诊断镜像；不参与测距、不访问DMA/网络/UI，不改变正式内存布局。
 * @note        ITCM段由用户链接扩展提供；失败时可能HardFault，不能当作已通过。
 */
#ifndef SBCG_BSP_ITCM_PROBE_H
#define SBCG_BSP_ITCM_PROBE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool ran;
    bool passed;
    uint32_t address;
    uint32_t bytes;
    uint32_t errors;
    uint32_t crc;
} BSP_ITCMProbeSnapshot;

/** @brief 执行ITCM读写/回读模式；返回是否全部匹配。 */
bool BSP_ITCM_ProbeRun(void);

/** @brief 复制最近一次探针快照；NULL不操作。 */
void BSP_ITCM_ProbeGetSnapshot(BSP_ITCMProbeSnapshot *out);

#endif
