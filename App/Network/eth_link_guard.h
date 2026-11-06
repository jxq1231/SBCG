/**
 * @file        eth_link_guard.h
 * @brief       ETH PHY/MAC 链路一致性检查与有界恢复决策接口。
 * @details     纯决策部分比较 PHY 链路/速率/双工、netif 链路、MACCR 的 FES/DM 位、
 *              收包计数增量与静默时长，输出 NONE、RECONFIG_MAC 或 REINIT；
 *              固件部分由 MX_LWIP_Process 的前台用户段以 1 Hz 采样并执行动作。
 * @note        纯决策不依赖 HAL，可在主机编译测试；固件接口只在 SBCG_USE_LWIP 构建提供。
 *              本模块不修改 CubeMX 拥有的 ETH 初始化、IRQ、DMA 或引脚配置。
 */

#ifndef SBCG_ETH_LINK_GUARD_H
#define SBCG_ETH_LINK_GUARD_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 恢复动作：不动作、只重配 MAC 速率/双工、完整重初始化 ETH。 */
typedef enum
{
    ETH_LINK_GUARD_NONE = 0,
    ETH_LINK_GUARD_RECONFIG_MAC,
    ETH_LINK_GUARD_REINIT
} EthLinkGuardAction;

/** @brief 决策/诊断原因，对应 $ETH_HEALTH 的 REASON 字段。 */
typedef enum
{
    ETH_LINK_GUARD_REASON_OK = 0,
    ETH_LINK_GUARD_REASON_PHY_DOWN,
    ETH_LINK_GUARD_REASON_NETIF_DOWN,
    ETH_LINK_GUARD_REASON_SPEED_MISMATCH,
    ETH_LINK_GUARD_REASON_DUPLEX_MISMATCH,
    ETH_LINK_GUARD_REASON_RX_STALE,
    ETH_LINK_GUARD_REASON_COOLDOWN,
    ETH_LINK_GUARD_REASON_BUDGET_EXHAUSTED,
    ETH_LINK_GUARD_REASON_READ_ERROR,
    ETH_LINK_GUARD_REASON_HAL_ERROR
} EthLinkGuardReason;

/** @brief 一次健康采样的纯输入；时间单位均为毫秒，速率为 Mbps。 */
typedef struct
{
    bool phy_link_up;          /**< PHY 自协商链路是否已连接。 */
    uint32_t phy_speed_mbps;   /**< 仅 phy_link_up 有效，BSP 只会给出 10 或 100。 */
    bool phy_full_duplex;      /**< PHY 当前是否全双工。 */
    bool netif_link_up;        /**< lwIP netif 的 link 标志（netif_is_link_up）。 */
    bool mac_100m;             /**< MACCR.FES：true 为 100 Mbps，false 为 10 Mbps。 */
    bool mac_full_duplex;      /**< MACCR.DM：true 为全双工。 */
    uint32_t rx_delta;         /**< 距上次采样成功收包计数的增量，允许自然回绕。 */
    uint32_t since_last_rx_ms; /**< 距最近一次收包增量的毫秒数。 */
} EthLinkGuardInput;

/** @brief 纯决策的限次状态；由 EthLinkGuard_StateInit 初始化，仅由 Evaluate 更新。 */
typedef struct
{
    uint32_t window_start_ms;  /**< 当前 10 s 计数窗口起点。 */
    uint32_t actions_in_window;/**< 当前窗口内已执行的动作次数。 */
    uint32_t last_action_ms;   /**< 最近一次动作的毫秒时刻。 */
    bool have_last_action;     /**< last_action_ms 是否有效。 */
} EthLinkGuardState;

/** @brief 一次决策结果；suppressed 为 true 表示命中冷却/限次，只告警不动作。 */
typedef struct
{
    EthLinkGuardAction action; /**< 建议/已执行的动作。 */
    EthLinkGuardReason reason; /**< 诊断原因；命中冷却时为 COOLDOWN/BUDGET_EXHAUSTED。 */
    bool suppressed;           /**< true 表示因限次被抑制。 */
} EthLinkGuardDecision;

/** @brief PHY 上报已连接但连续无收包增量达到该毫秒数时触发 REINIT。 */
#define ETH_LINK_GUARD_STALE_RX_MS 2000U
/** @brief 恢复动作计数窗口长度，单位毫秒。 */
#define ETH_LINK_GUARD_WINDOW_MS 10000U
/** @brief 每个计数窗口内允许的最大恢复动作次数。 */
#define ETH_LINK_GUARD_MAX_ACTIONS 2U
/** @brief 两次恢复动作之间的最小间隔，单位毫秒，用于给链路恢复留出观察时间。 */
#define ETH_LINK_GUARD_ACTION_COOLDOWN_MS 2000U
/** @brief 前台采样周期，单位毫秒。 */
#define ETH_LINK_GUARD_PERIOD_MS 1000U

/**
 * @brief   清零限次状态并把 10 s 计数窗口起点设为当前时刻。
 * @param   state 调用方拥有的状态；NULL 时直接返回，不做任何写入。
 * @param   now_ms 当前毫秒计数，允许自然回绕。
 * @return  无返回值。
 * @note    只由前台初始化/测试使用；不访问 HAL，不改变网络状态。
 */
void EthLinkGuard_StateInit(EthLinkGuardState *state, uint32_t now_ms);

/**
 * @brief   根据一次健康采样给出恢复动作，并更新限次窗口。
 * @param   input 非空只读采样输入；NULL 时按 NONE/OK 处理。
 * @param   state 调用方拥有的非空限次状态；NULL 时按 NONE/OK 处理。
 * @param   now_ms 当前毫秒计数，差值以 uint32_t 处理回绕。
 * @param   decision 调用方拥有的非空输出；函数先置 NONE/OK/未抑制。
 * @return  无返回值。
 * @note    规则：PHY UP 且 MAC 速率或双工不一致 → RECONFIG_MAC；MAC 一致、
 *          netif link up 且 since_last_rx_ms ≥ 2000 且无增量 → REINIT。
 *          10 s 窗口内最多 2 次动作，动作后 2 s 内不再动作；超限只置 suppressed
 *          并给出原因，不产生动作。收包增量恢复且无速率/双工不一致时重置窗口预算。
 */
void EthLinkGuard_Evaluate(const EthLinkGuardInput *input,
                           EthLinkGuardState *state,
                           uint32_t now_ms,
                           EthLinkGuardDecision *decision);

#ifdef SBCG_USE_LWIP
/**
 * @brief   向应用提交一行 $ETH_HEALTH 观测日志的回调类型。
 * @param   line 只读日志字符缓冲，不可为NULL；仅本次调用有效。
 * @param   length 有效字符数，单位字节，不含结尾零字符。
 * @return  无返回值。
 * @note    回调在 MX_LWIP_Process 前台上下文同步执行，不得长时间阻塞。
 */
typedef void (*EthLinkGuard_Log)(const char *line, int length);

/**
 * @brief   以 1 Hz 采样 PHY/MAC/收包计数，按纯决策执行有界恢复并输出 $ETH_HEALTH。
 * @param   now_ms 前台当前毫秒计数（通常为 HAL_GetTick），差值处理回绕。
 * @param   log 可为 NULL 的日志回调；非 NULL 时每次采样调用一次。
 * @return  无返回值。
 * @note    仅由 MX_LWIP_Process 的用户段在前台调用；内部按 1 Hz 节流。
 *          上电后尚未收到任何 UDP 包时不启用“无收包”判决，避免对端未启动时误重初始化。
 *          RECONFIG_MAC 执行 HAL_ETH_Stop/SetMACConfig/Start；
 *          REINIT 执行 Stop→DeInit→Init→SetMACConfig→Start→etharp_gratuitous，
 *          并在 Stop 后回收 Rx 描述符持有的 pbuf、探测并归还一次 RX 池缓冲，
 *          以清除 ethernetif 固定 4 缓冲的 RX_POOL/RxAllocStatus 锁存。
 *          不新增 IRQ/DMA 映射，不改 CubeMX 生成的 ETH 引脚或时钟配置。
 */
void EthLinkGuard_Process(uint32_t now_ms, EthLinkGuard_Log log);
#endif

#endif
