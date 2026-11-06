/**
 * @file        eth_link_guard.c
 * @brief       ETH PHY/MAC 链路一致性检查与有界恢复实现。
 * @details     纯决策函数比较 PHY 速率/双工与 MACCR 的 FES/DM 位，并在 PHY 保持 UP
 *              但收包计数连续 2 s 无增量时请求 REINIT；10 s 窗口内最多执行 2 次动作。
 *              固件部分按 1 Hz 读取 BSP_Ethernet_ReadLink、heth MACCR 与 UDP 收包计数，
 *              RECONFIG_MAC 走 Stop/SetMACConfig/Start，REINIT 走
 *              Stop→DeInit→Init→SetMACConfig→Start→etharp_gratuitous。
 * @note        仅前台调用。REINIT 在 Stop 后回收旧 Rx 描述符持有的 pbuf、探测并
 *              归还一次 RX 池缓冲，避免重建描述符时耗尽或锁死 ethernetif 固定 4 缓冲
 *              的 RX_POOL；不改 CubeMX 生成的 ETH 初始化、IRQ、DMA 或引脚配置。
 */

#include "eth_link_guard.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef SBCG_USE_LWIP
#include "main.h"
#include "lwip.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "BSP/bsp_ethernet.h"

/* 该访问器只读、无副作用；若当前构建未登记 UDP 声音事件模块，弱符号为空，
   此时禁止用“无收包”推断链路故障。 */
extern uint32_t UDPSoundEvent_GetRxOkCount(void) __attribute__((weak));
/* lwip.c 中由 CubeMX 生成代码定义并初始化的全局网卡。 */
extern struct netif gnetif;
/* ethernetif.c 的零拷贝 Rx pbuf 释放回调，用于 REINIT 前回收描述符缓冲。 */
extern void pbuf_free_custom(struct pbuf *p);
#endif

/**
 * @brief   在当前 10 s 窗口内清零动作预算并结束冷却。
 * @param   state 调用方拥有的非空状态。
 * @param   now_ms 新的窗口起点毫秒计数。
 * @return  无返回值。
 * @note    仅在 PHY 断开或确认收包恢复时调用；动作计数与冷却同时复位。
 */
static void ResetBudget(EthLinkGuardState *state, uint32_t now_ms)
{
    state->window_start_ms = now_ms;
    state->actions_in_window = 0U;
    state->have_last_action = false;
}

/**
 * @brief   清零限次状态并把 10 s 计数窗口起点设为当前时刻。
 * @param   state 调用方拥有的非空状态；NULL 时直接返回。
 * @param   now_ms 当前毫秒计数，允许自然回绕。
 * @return  无返回值。
 * @note    只由前台初始化/测试使用；不访问 HAL，不改变网络状态。
 */
void EthLinkGuard_StateInit(EthLinkGuardState *state, uint32_t now_ms)
{
    if (state == NULL)
    {
        return;
    }
    state->window_start_ms = now_ms;
    state->actions_in_window = 0U;
    state->last_action_ms = now_ms;
    state->have_last_action = false;
}

/**
 * @brief   根据一次健康采样给出恢复动作，并更新限次窗口。
 * @param   input 非空只读采样输入；NULL 时按 NONE/OK 处理。
 * @param   state 调用方拥有的非空限次状态；NULL 时按 NONE/OK 处理。
 * @param   now_ms 当前毫秒计数，差值以 uint32_t 处理回绕。
 * @param   decision 调用方拥有的非空输出；函数先置 NONE/OK/未抑制。
 * @return  无返回值。
 * @note    速率/双工不一致优先于收包静默；netif link 未建立时由生成代码负责
 *          重新拉起，本函数不动作。动作间隔与窗口限次见头文件常量。
 */
void EthLinkGuard_Evaluate(const EthLinkGuardInput *input,
                           EthLinkGuardState *state,
                           uint32_t now_ms,
                           EthLinkGuardDecision *decision)
{
    if (decision == NULL)
    {
        return;
    }
    decision->action = ETH_LINK_GUARD_NONE;
    decision->reason = ETH_LINK_GUARD_REASON_OK;
    decision->suppressed = false;
    if ((input == NULL) || (state == NULL))
    {
        return;
    }

    /* 滚动窗口：每 10 s 重新获得最多 2 次动作预算。 */
    if ((uint32_t)(now_ms - state->window_start_ms) >= ETH_LINK_GUARD_WINDOW_MS)
    {
        state->window_start_ms = now_ms;
        state->actions_in_window = 0U;
    }

    if (!input->phy_link_up)
    {
        /* 断链本身是一次链路转换：清空预算和冷却，允许重新连接后立即恢复。 */
        decision->reason = ETH_LINK_GUARD_REASON_PHY_DOWN;
        ResetBudget(state, now_ms);
        return;
    }

    const bool speed_known = (input->phy_speed_mbps == 10U) ||
                             (input->phy_speed_mbps == 100U);
    const bool speed_mismatch = speed_known &&
        ((input->phy_speed_mbps == 100U) != input->mac_100m);
    const bool duplex_mismatch = input->phy_full_duplex != input->mac_full_duplex;

    /* PHY 与 MAC 一致且确实收到包时，认为上一次故障已恢复，重置限次预算。 */
    if ((input->rx_delta > 0U) && !speed_mismatch && !duplex_mismatch &&
        input->netif_link_up)
    {
        ResetBudget(state, now_ms);
    }

    if (speed_mismatch || duplex_mismatch)
    {
        decision->reason = speed_mismatch ? ETH_LINK_GUARD_REASON_SPEED_MISMATCH :
                                            ETH_LINK_GUARD_REASON_DUPLEX_MISMATCH;
    }
    else if (!input->netif_link_up)
    {
        /* 生成代码的 ethernet_link_check_state 会在 100 ms 周期内拉起 link。 */
        decision->reason = ETH_LINK_GUARD_REASON_NETIF_DOWN;
        return;
    }
    else if ((input->rx_delta == 0U) &&
             (input->since_last_rx_ms >= ETH_LINK_GUARD_STALE_RX_MS))
    {
        /* 没有本板 ETH DMA 错误时，静默更可能是对端掉线/未发；
           重初始化本板 MAC 既不能恢复对端，又会清掉 PTP 配置，故只报告。 */
        decision->reason = ETH_LINK_GUARD_REASON_RX_STALE;
        if (!input->rx_dma_error) { return; }
    }
    else
    {
        return;
    }

    if (state->have_last_action &&
        ((uint32_t)(now_ms - state->last_action_ms) <
         ETH_LINK_GUARD_ACTION_COOLDOWN_MS))
    {
        decision->reason = ETH_LINK_GUARD_REASON_COOLDOWN;
        decision->suppressed = true;
        return;
    }
    if (state->actions_in_window >= ETH_LINK_GUARD_MAX_ACTIONS)
    {
        decision->reason = ETH_LINK_GUARD_REASON_BUDGET_EXHAUSTED;
        decision->suppressed = true;
        return;
    }

    ++state->actions_in_window;
    state->last_action_ms = now_ms;
    state->have_last_action = true;
    decision->action = speed_mismatch || duplex_mismatch ?
        ETH_LINK_GUARD_RECONFIG_MAC : ETH_LINK_GUARD_REINIT;
}

#ifdef SBCG_USE_LWIP
static EthLinkGuardState guard_state;
static bool guard_initialized;
static bool guard_have_eval;
static uint32_t guard_last_eval_ms;
static uint32_t guard_last_rx_count;
static uint32_t guard_last_rx_ms;
static bool guard_rx_counter_available;
static bool guard_rx_seen;
static uint8_t guard_mac[6];

/**
 * @brief   把 PHY 当前速率/双工写入 MAC 配置。
 * @param   link 非空只读 PHY 观测；仅 UP 时的 speed_mbps/full_duplex 有意义。
 * @return  true 表示 HAL 读取并写回 MAC 配置成功；false 表示 HAL 拒绝。
 * @note    MAC 必须处于 READY；不启动/停止 DMA，不改变 PHY。
 */
static bool ApplyPhyMacConfig(const BSP_EthernetLink *link)
{
    ETH_MACConfigTypeDef config;
    if (HAL_ETH_GetMACConfig(&heth, &config) != HAL_OK)
    {
        return false;
    }
    config.Speed = (link->speed_mbps == 100U) ? ETH_SPEED_100M : ETH_SPEED_10M;
    config.DuplexMode = link->full_duplex ? ETH_FULLDUPLEX_MODE : ETH_HALFDUPLEX_MODE;
    return HAL_ETH_SetMACConfig(&heth, &config) == HAL_OK;
}

/**
 * @brief   校验并归还一个零拷贝 RX 池缓冲。
 * @param   buff 非空 RxBuff_t.buff 地址；由描述符 BackupAddr0 或 RxAllocateCallback 提供。
 * @return  true 表示四项布局校验通过且 pbuf_free 归还成功；false 表示跳过。
 * @note    只在 DMA 停止或刚分配的 CPU 自有缓冲上调用；不访问 DMA 正在写入的数据区。
 *          pbuf_free 会走到 ethernetif.c 的 pbuf_free_custom，从而同时清除
 *          RX_POOL 耗尽锁存（RxAllocStatus）。
 */
static bool ReleaseRxBuffer(uint8_t *buff)
{
    const size_t header = (sizeof(struct pbuf_custom) + 31U) & ~(size_t)31U;
    const uintptr_t address = (uintptr_t)buff;
    if (address < header)
    {
        return false;
    }
    const uintptr_t base = address - header;
    if ((base & (uintptr_t)31U) != 0U)
    {
        return false;
    }
    struct pbuf_custom *custom = (struct pbuf_custom *)base;
    if ((custom->pbuf.payload != (void *)buff) ||
        ((custom->pbuf.flags & PBUF_FLAG_IS_CUSTOM) == 0U) ||
        (custom->custom_free_function != pbuf_free_custom) ||
        (custom->pbuf.ref != 1U))
    {
        return false;
    }
    return pbuf_free(&custom->pbuf) == 1U;
}

/**
 * @brief   在 DMA 停止后回收 Rx 描述符持有的零拷贝 pbuf。
 * @param   allocated 非空输出，返回检测到“描述符仍持有缓冲”的数量。
 * @return  实际归还到 RX_POOL 的描述符缓冲数量；校验失败或未分配的描述符跳过。
 * @note    必须在 HAL_ETH_Stop 之后、HAL_ETH_DeInit 之前调用。描述符 BackupAddr0
 *          指向 RxBuff_t.buff，pbuf 位于其前部且按 32 字节对齐；通过 payload、
 *          PBUF_FLAG_IS_CUSTOM、释放回调与 ref==1 四项校验后调用 pbuf_free，
 *          避免 HAL_ETH_Init 重建描述符时永久丢失固定 4 个 RX_POOL 缓冲。
 *          未交付的部分接收链也先整链归还，其数量不计入描述符统计。
 */
static uint32_t ReclaimRxBuffers(uint32_t *allocated)
{
    uint32_t freed = 0U;
    *allocated = 0U;
    /* 未交付的接收链仍由 HAL 拥有；先整链归还，避免重建时丢失部分帧。 */
    if (heth.RxDescList.pRxStart != NULL)
    {
        struct pbuf *partial = (struct pbuf *)heth.RxDescList.pRxStart;
        heth.RxDescList.pRxStart = NULL;
        heth.RxDescList.pRxEnd = NULL;
        (void)pbuf_free(partial);
    }
    for (uint32_t index = 0U; index < (uint32_t)ETH_RX_DESC_CNT; ++index)
    {
        ETH_DMADescTypeDef *desc =
            (ETH_DMADescTypeDef *)(uintptr_t)heth.RxDescList.RxDesc[index];
        if (desc == NULL)
        {
            continue;
        }
        const uintptr_t buff = (uintptr_t)desc->BackupAddr0;
        if (buff == 0U)
        {
            continue;
        }
        ++(*allocated);
        if (ReleaseRxBuffer((uint8_t *)buff))
        {
            ++freed;
        }
    }
    return freed;
}

/**
 * @brief   只重配 MAC 速率/双工并重启收发。
 * @param   link 非空只读 PHY 观测。
 * @return  true 表示 Stop/SetMACConfig/Start 全部成功。
 * @note    生成代码已把 netif link 置 UP 时不再触发 link 回调以外的状态变化；
 *          netif link 仍为 DOWN 则补一次 netif_set_link_up。
 */
static bool ReconfigureMac(const BSP_EthernetLink *link)
{
    if (heth.gState == HAL_ETH_STATE_STARTED)
    {
        if (HAL_ETH_Stop(&heth) != HAL_OK)
        {
            return false;
        }
    }
    if (heth.gState != HAL_ETH_STATE_READY)
    {
        return false;
    }
    if (!ApplyPhyMacConfig(link))
    {
        return false;
    }
    if (HAL_ETH_Start(&heth) != HAL_OK)
    {
        return false;
    }
    if (!netif_is_link_up(&gnetif))
    {
        netif_set_link_up(&gnetif);
    }
    return true;
}

/**
 * @brief   完整重建 ETH 描述符并按 PHY 重新配置 MAC 后启动。
 * @param   link 非空只读 PHY 观测。
 * @return  true 表示 DeInit/Init/SetMACConfig/Start 及 gratuitous ARP 路径完成。
 * @note    ethernetif.c 初始化时把 heth.Init.MACAddr 指向函数局部缓冲，DeInit 后该
 *          指针失效；重建前改用持久 MAC 副本。PTP 时间戳寄存器会随 MAC 软复位
 *          失效，本模块不越权改写 PTP，副作用在交付说明中记录。
 */
static bool ReinitializeEth(const BSP_EthernetLink *link)
{
    netif_set_link_down(&gnetif);
    if (heth.gState == HAL_ETH_STATE_STARTED)
    {
        if (HAL_ETH_Stop(&heth) != HAL_OK)
        {
            return false;
        }
    }
    uint32_t allocated = 0U;
    const uint32_t reclaimed = ReclaimRxBuffers(&allocated);
    if ((allocated > 0U) && (reclaimed == 0U))
    {
        /* 布局校验全部失败时说明无法安全回收固定 4 缓冲；恢复 DMA 并放弃本次重建，
           避免 DeInit 后 RX_POOL 永久为空。 */
        (void)HAL_ETH_Start(&heth);
        return false;
    }
    /* 探测一次 RX 池：成功分配后立即归还，可清除 ethernetif.c 的 RxAllocStatus
       耗尽锁存；该锁存会让 low_level_input 永久跳过 HAL_ETH_ReadData。 */
    uint8_t *probe = NULL;
    HAL_ETH_RxAllocateCallback(&probe);
    if (probe != NULL)
    {
        (void)ReleaseRxBuffer(probe);
    }
    if (HAL_ETH_DeInit(&heth) != HAL_OK)
    {
        return false;
    }
    memcpy(guard_mac, gnetif.hwaddr, sizeof guard_mac);
    heth.Init.MACAddr = guard_mac;
    if (HAL_ETH_Init(&heth) != HAL_OK)
    {
        return false;
    }
    /* MAC 软复位会清 MACMIIAR.CR；恢复与 ETH_PHY_IO_Init 相同的运行时 MDIO 分频。 */
    HAL_ETH_SetMDIOClockRange(&heth);
    if (!ApplyPhyMacConfig(link))
    {
        return false;
    }
    if (HAL_ETH_Start(&heth) != HAL_OK)
    {
        return false;
    }
    netif_set_link_up(&gnetif);
    (void)etharp_gratuitous(&gnetif);
    return true;
}

/**
 * @brief   把恢复动作枚举转换为日志字符串。
 * @param   action 动作枚举。
 * @return  静态只读字符串，调用方不得释放。
 */
static const char *ActionName(EthLinkGuardAction action)
{
    switch (action)
    {
        case ETH_LINK_GUARD_RECONFIG_MAC: return "RECONFIG_MAC";
        case ETH_LINK_GUARD_REINIT: return "REINIT";
        default: return "NONE";
    }
}

/**
 * @brief   把决策原因枚举转换为日志字符串。
 * @param   reason 原因枚举。
 * @return  静态只读字符串，调用方不得释放。
 */
static const char *ReasonName(EthLinkGuardReason reason)
{
    switch (reason)
    {
        case ETH_LINK_GUARD_REASON_PHY_DOWN: return "PHY_DOWN";
        case ETH_LINK_GUARD_REASON_NETIF_DOWN: return "NETIF_DOWN";
        case ETH_LINK_GUARD_REASON_SPEED_MISMATCH: return "SPEED_MISMATCH";
        case ETH_LINK_GUARD_REASON_DUPLEX_MISMATCH: return "DUPLEX_MISMATCH";
        case ETH_LINK_GUARD_REASON_RX_STALE: return "RX_STALE";
        case ETH_LINK_GUARD_REASON_COOLDOWN: return "COOLDOWN";
        case ETH_LINK_GUARD_REASON_BUDGET_EXHAUSTED: return "BUDGET_EXHAUSTED";
        case ETH_LINK_GUARD_REASON_READ_ERROR: return "READ_ERROR";
        case ETH_LINK_GUARD_REASON_HAL_ERROR: return "HAL_ERROR";
        default: return "OK";
    }
}

/**
 * @brief   输出一行 $ETH_HEALTH 诊断。
 * @param   log 可为 NULL 的回调；NULL 时不格式化也不写入。
 * @param   phy PHY 状态文本，例如 UP/100F、DOWN。
 * @param   mac MAC 速率/双工文本，例如 100F、UNKNOWN。
 * @param   rx_delta 本次采样收包计数增量。
 * @param   action 本次执行/建议的动作。
 * @param   reason 原因文本。
 * @return  无返回值。
 * @note    仅前台调用；单行长度受本地 128 字节缓冲限制。
 */
static void EmitHealth(EthLinkGuard_Log log, const char *phy, const char *mac,
                       uint32_t rx_delta, EthLinkGuardAction action,
                       EthLinkGuardReason reason)
{
    if (log == NULL)
    {
        return;
    }
    char line[128];
    const int length = snprintf(line, sizeof line,
        "$ETH_HEALTH,PHY=%s,MAC=%s,RX_DELTA=%lu,ACTION=%s,REASON=%s\r\n",
        phy, mac, (unsigned long)rx_delta, ActionName(action), ReasonName(reason));
    if ((length > 0) && ((size_t)length < sizeof line))
    {
        log(line, length);
    }
}

/**
 * @brief   把 PHY 链路/速率/双工格式化为 $ETH_HEALTH 的 PHY 字段。
 * @param   link 非空只读观测。
 * @param   out 调用方缓冲。
 * @param   capacity 缓冲字节数。
 * @return  无返回值；超长时按 snprintf 截断。
 */
static void FormatPhy(const BSP_EthernetLink *link, char *out, size_t capacity)
{
    if (link->state == BSP_ETH_LINK_UP)
    {
        (void)snprintf(out, capacity, "UP/%lu%c", (unsigned long)link->speed_mbps,
                       link->full_duplex ? 'F' : 'H');
    }
    else if (link->state == BSP_ETH_LINK_NEGOTIATING)
    {
        (void)snprintf(out, capacity, "NEGOTIATING");
    }
    else
    {
        (void)snprintf(out, capacity, "DOWN");
    }
}

/**
 * @brief   把 MACCR 的 FES/DM 位格式化为 $ETH_HEALTH 的 MAC 字段。
 * @param   valid MACCR 是否可读。
 * @param   maccr 已读取的 MACCR 值，valid 为 false 时忽略。
 * @param   out 调用方缓冲。
 * @param   capacity 缓冲字节数。
 * @return  无返回值。
 */
static void FormatMac(bool valid, uint32_t maccr, char *out, size_t capacity)
{
    if (!valid)
    {
        (void)snprintf(out, capacity, "UNKNOWN");
        return;
    }
    const bool speed_100m = (maccr & ETH_MACCR_FES) != 0U;
    const bool full_duplex = (maccr & ETH_MACCR_DM) != 0U;
    (void)snprintf(out, capacity, "%u%c", speed_100m ? 100U : 10U,
                   full_duplex ? 'F' : 'H');
}

/**
 * @brief   以 1 Hz 采样 PHY/MAC/收包计数，按纯决策执行有界恢复并输出 $ETH_HEALTH。
 * @param   now_ms 前台当前毫秒计数（通常为 HAL_GetTick），差值处理回绕。
 * @param   log 可为 NULL 的日志回调；非 NULL 时每次采样调用一次。
 * @return  无返回值。
 * @note    仅由 MX_LWIP_Process 的用户段在前台调用；内部按 1 Hz 节流。
 *          上电后尚未收到任何 UDP 包时不启用“无收包”判决，避免对端未启动时误重初始化。
 *          不新增 IRQ/DMA 映射，不改 CubeMX 生成的 ETH 引脚或时钟配置。
 */
void EthLinkGuard_Process(uint32_t now_ms, EthLinkGuard_Log log)
{
    BSP_EthernetLink link;
    char phy_text[20];
    char mac_text[12];

    if (!guard_initialized)
    {
        guard_initialized = true;
        guard_have_eval = false;
        guard_last_rx_ms = now_ms;
        guard_rx_counter_available = (UDPSoundEvent_GetRxOkCount != NULL);
        guard_last_rx_count = guard_rx_counter_available ?
            UDPSoundEvent_GetRxOkCount() : 0U;
        /* 上电后从未收到过 UDP 时（对端尚未启动）不以“无增量”判故障。 */
        guard_rx_seen = guard_rx_counter_available && (guard_last_rx_count > 0U);
        EthLinkGuard_StateInit(&guard_state, now_ms);
    }

    if (guard_have_eval &&
        ((uint32_t)(now_ms - guard_last_eval_ms) < ETH_LINK_GUARD_PERIOD_MS))
    {
        return;
    }
    guard_last_eval_ms = now_ms;
    guard_have_eval = true;

    /* ETH 处于 RESET/BUSY/ERROR 时不访问 MAC/MDIO 寄存器，避免时钟关闭后的无效访问。 */
    const bool eth_accessible = (heth.Instance != NULL) &&
        ((heth.gState == HAL_ETH_STATE_READY) ||
         (heth.gState == HAL_ETH_STATE_STARTED));
    if (!eth_accessible)
    {
        guard_last_rx_ms = now_ms;
        EmitHealth(log, "UNAVAILABLE", "UNKNOWN", 0U, ETH_LINK_GUARD_NONE,
                   ETH_LINK_GUARD_REASON_HAL_ERROR);
        return;
    }

    if (BSP_Ethernet_ReadLink(&link) != BSP_ETH_OK)
    {
        EmitHealth(log, "READ_ERROR", "UNKNOWN", 0U, ETH_LINK_GUARD_NONE,
                   ETH_LINK_GUARD_REASON_READ_ERROR);
        return;
    }

    uint32_t rx_delta = 0U;
    uint32_t since_last_rx_ms = 0U;
    /* 链路断开期间不累计“无收包”时间，重连后重新给 2 s 观察窗。 */
    if (link.state != BSP_ETH_LINK_UP)
    {
        guard_last_rx_ms = now_ms;
    }
    if (guard_rx_counter_available)
    {
        const uint32_t count = UDPSoundEvent_GetRxOkCount();
        rx_delta = count - guard_last_rx_count;
        guard_last_rx_count = count;
        if (rx_delta > 0U)
        {
            guard_last_rx_ms = now_ms;
            guard_rx_seen = true;
        }
        /* 对端尚未启动或本构建没有收包计数时不启用静默判决。 */
        since_last_rx_ms = guard_rx_seen ? (now_ms - guard_last_rx_ms) : 0U;
    }

    /* ETH 未处于可访问状态时不读 MACCR，避免时钟关闭后的无效寄存器访问。 */
    const bool mac_valid = (heth.Instance != NULL) &&
        ((heth.gState == HAL_ETH_STATE_READY) ||
         (heth.gState == HAL_ETH_STATE_STARTED));
    const uint32_t maccr = mac_valid ? READ_REG(heth.Instance->MACCR) : 0U;
    EthLinkGuardInput input;
    input.phy_link_up = link.state == BSP_ETH_LINK_UP;
    input.phy_speed_mbps = link.speed_mbps;
    input.phy_full_duplex = link.full_duplex;
    input.netif_link_up = netif_is_link_up(&gnetif) != 0U;
    /* MACCR 不可读时按 PHY 值填充，避免把读取失败误判成速率不一致。 */
    input.mac_100m = mac_valid ? ((maccr & ETH_MACCR_FES) != 0U) :
                                (link.speed_mbps == 100U);
    input.mac_full_duplex = mac_valid ? ((maccr & ETH_MACCR_DM) != 0U) :
                                       link.full_duplex;
    input.rx_delta = rx_delta;
    input.since_last_rx_ms = since_last_rx_ms;
    const uint32_t dmasr = mac_valid ? READ_REG(heth.Instance->DMASR) : 0U;
    input.rx_dma_error = mac_valid &&
        ((dmasr & (ETH_DMASR_RBUS | ETH_DMASR_RPSS | ETH_DMASR_RWTS |
                   ETH_DMASR_FBES)) != 0U);

    EthLinkGuardDecision decision;
    EthLinkGuard_Evaluate(&input, &guard_state, now_ms, &decision);

    bool action_ok = true;
    if (decision.action == ETH_LINK_GUARD_RECONFIG_MAC)
    {
        action_ok = ReconfigureMac(&link);
    }
    else if (decision.action == ETH_LINK_GUARD_REINIT)
    {
        action_ok = ReinitializeEth(&link);
    }
    if ((decision.action != ETH_LINK_GUARD_NONE) && !action_ok)
    {
        decision.reason = ETH_LINK_GUARD_REASON_HAL_ERROR;
    }

    FormatPhy(&link, phy_text, sizeof phy_text);
    FormatMac(mac_valid, maccr, mac_text, sizeof mac_text);
    EmitHealth(log, phy_text, mac_text, rx_delta, decision.action,
               decision.reason);
}
#endif
