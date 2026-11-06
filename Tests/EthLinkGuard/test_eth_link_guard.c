/**
 * @file        test_eth_link_guard.c
 * @brief       在主机上验证 ETH 链路守护的纯决策规则与有界重试。
 * @details     构造 PHY/MAC 速率双工、netif link、收包增量与静默时长的合成输入，
 *              断言一致→NONE、速率/双工不一致→RECONFIG_MAC、PHY UP 无包→REINIT、
 *              10 s 窗口最多 2 次动作、收包恢复后预算重置。
 * @note        只验证与硬件无关的决策函数；真实 HAL_ETH Stop/Init、MDIO 和实机
 *              RX 恢复由板上验收覆盖，本测试不访问任何 HAL 寄存器。
 */

#include "Network/eth_link_guard.h"

#include <assert.h>
#include <stdio.h>

/**
 * @brief   构造一份 100M 全双工、MAC 一致、收包正常的输入。
 * @return  合成输入；默认 rx_delta=1、since_last_rx_ms=100。
 * @note    仅本测试使用，不访问硬件。
 */
static EthLinkGuardInput Input100Full(void)
{
    EthLinkGuardInput input;
    input.phy_link_up = true;
    input.phy_speed_mbps = 100U;
    input.phy_full_duplex = true;
    input.netif_link_up = true;
    input.mac_100m = true;
    input.mac_full_duplex = true;
    input.rx_delta = 1U;
    input.since_last_rx_ms = 100U;
    input.rx_dma_error = true;
    return input;
}

/**
 * @brief   验证 PHY/MAC 一致时始终返回 NONE。
 * @return  无返回值。
 * @note    覆盖 100M 全双工与 10M 半双工两种合法组合。
 */
static void TestConsistent(void)
{
    EthLinkGuardState state;
    EthLinkGuardDecision decision;
    EthLinkGuard_StateInit(&state, 0U);

    EthLinkGuardInput input = Input100Full();
    EthLinkGuard_Evaluate(&input, &state, 0U, &decision);
    assert(decision.action == ETH_LINK_GUARD_NONE);
    assert(decision.reason == ETH_LINK_GUARD_REASON_OK);
    assert(!decision.suppressed);
    assert(state.actions_in_window == 0U);

    input.mac_100m = false;
    input.mac_full_duplex = false;
    input.phy_speed_mbps = 10U;
    input.phy_full_duplex = false;
    EthLinkGuard_Evaluate(&input, &state, 1000U, &decision);
    assert(decision.action == ETH_LINK_GUARD_NONE);
    assert(decision.reason == ETH_LINK_GUARD_REASON_OK);
    puts("PASS consistent: 100F/10H matching MAC keeps NONE");
}

/**
 * @brief   验证 PHY/MAC 速率或双工不一致触发 RECONFIG_MAC 且限次生效。
 * @return  无返回值。
 * @note    时间线：0 s 第 1 次、2 s 第 2 次、5 s 超限、10 s 新窗口恢复预算。
 */
static void TestMismatch(void)
{
    EthLinkGuardState state;
    EthLinkGuardDecision decision;
    EthLinkGuard_StateInit(&state, 0U);

    EthLinkGuardInput input = Input100Full();
    input.mac_100m = false;
    input.mac_full_duplex = false;
    EthLinkGuard_Evaluate(&input, &state, 0U, &decision);
    assert(decision.action == ETH_LINK_GUARD_RECONFIG_MAC);
    assert(decision.reason == ETH_LINK_GUARD_REASON_SPEED_MISMATCH);
    assert(state.actions_in_window == 1U);

    /* 冷却期内只告警。 */
    EthLinkGuard_Evaluate(&input, &state, 1000U, &decision);
    assert(decision.action == ETH_LINK_GUARD_NONE);
    assert(decision.reason == ETH_LINK_GUARD_REASON_COOLDOWN);
    assert(decision.suppressed);

    EthLinkGuard_Evaluate(&input, &state, 2000U, &decision);
    assert(decision.action == ETH_LINK_GUARD_RECONFIG_MAC);
    assert(state.actions_in_window == 2U);

    /* 窗口预算耗尽后不再动作，只保留原因。 */
    EthLinkGuard_Evaluate(&input, &state, 5000U, &decision);
    assert(decision.action == ETH_LINK_GUARD_NONE);
    assert(decision.reason == ETH_LINK_GUARD_REASON_BUDGET_EXHAUSTED);
    assert(decision.suppressed);

    /* 10 s 窗口滚动后重新获得预算。 */
    EthLinkGuard_Evaluate(&input, &state, 10001U, &decision);
    assert(decision.action == ETH_LINK_GUARD_RECONFIG_MAC);
    assert(state.actions_in_window == 1U);
    puts("PASS mismatch: speed mismatch -> RECONFIG, bounded to 2 per 10 s");
}

/**
 * @brief   验证仅双工不一致也触发 RECONFIG_MAC，且速率不一致优先于收包静默。
 * @return  无返回值。
 * @note    速率优先用例同时给出 stale RX，断言仍选择 RECONFIG_MAC。
 */
static void TestDuplexAndPriority(void)
{
    EthLinkGuardState state;
    EthLinkGuardDecision decision;
    EthLinkGuard_StateInit(&state, 0U);

    EthLinkGuardInput input = Input100Full();
    input.mac_full_duplex = false;
    EthLinkGuard_Evaluate(&input, &state, 0U, &decision);
    assert(decision.action == ETH_LINK_GUARD_RECONFIG_MAC);
    assert(decision.reason == ETH_LINK_GUARD_REASON_DUPLEX_MISMATCH);

    EthLinkGuard_StateInit(&state, 0U);
    input = Input100Full();
    input.mac_100m = false;
    input.rx_delta = 0U;
    input.since_last_rx_ms = 5000U;
    EthLinkGuard_Evaluate(&input, &state, 5000U, &decision);
    assert(decision.action == ETH_LINK_GUARD_RECONFIG_MAC);
    assert(decision.reason == ETH_LINK_GUARD_REASON_SPEED_MISMATCH);
    puts("PASS duplex/priority: duplex mismatch covered, mismatch beats RX stale");
}

/**
 * @brief   验证 PHY 保持 UP 且 2 s 无收包增量触发 REINIT 与窗口限次。
 * @return  无返回值。
 * @note    时间线：2.5 s 第 1 次、4.5 s 第 2 次、6.5 s 超限、12.5 s 新窗口。
 */
static void TestStaleRxBounded(void)
{
    EthLinkGuardState state;
    EthLinkGuardDecision decision;
    EthLinkGuard_StateInit(&state, 0U);

    EthLinkGuardInput input = Input100Full();
    input.rx_delta = 0U;
    input.since_last_rx_ms = 2500U;

    EthLinkGuard_Evaluate(&input, &state, 2500U, &decision);
    assert(decision.action == ETH_LINK_GUARD_REINIT);
    assert(decision.reason == ETH_LINK_GUARD_REASON_RX_STALE);
    assert(state.actions_in_window == 1U);

    EthLinkGuard_Evaluate(&input, &state, 3500U, &decision);
    assert(decision.action == ETH_LINK_GUARD_NONE);
    assert(decision.reason == ETH_LINK_GUARD_REASON_COOLDOWN);

    EthLinkGuard_Evaluate(&input, &state, 4500U, &decision);
    assert(decision.action == ETH_LINK_GUARD_REINIT);
    assert(state.actions_in_window == 2U);

    EthLinkGuard_Evaluate(&input, &state, 6500U, &decision);
    assert(decision.action == ETH_LINK_GUARD_NONE);
    assert(decision.reason == ETH_LINK_GUARD_REASON_BUDGET_EXHAUSTED);
    assert(decision.suppressed);

    EthLinkGuard_Evaluate(&input, &state, 12500U, &decision);
    assert(decision.action == ETH_LINK_GUARD_REINIT);
    assert(state.actions_in_window == 1U);
    puts("PASS stale RX: 2 s silence -> REINIT, only 2 actions per 10 s");
}

/**
 * @brief   验证收包恢复后预算清零、后续再次静默可立即动作。
 * @return  无返回值。
 * @note    恢复样本必须同时满足 MAC 一致与 netif link up。
 */
static void TestRecoveryResetsBudget(void)
{
    EthLinkGuardState state;
    EthLinkGuardDecision decision;
    EthLinkGuard_StateInit(&state, 0U);

    EthLinkGuardInput input = Input100Full();
    input.rx_delta = 0U;
    input.since_last_rx_ms = 2500U;
    EthLinkGuard_Evaluate(&input, &state, 2500U, &decision);
    assert(decision.action == ETH_LINK_GUARD_REINIT);
    assert(state.actions_in_window == 1U);

    /* 收到包：恢复为 NONE，并清空动作预算与冷却。 */
    input.rx_delta = 3U;
    input.since_last_rx_ms = 100U;
    EthLinkGuard_Evaluate(&input, &state, 3500U, &decision);
    assert(decision.action == ETH_LINK_GUARD_NONE);
    assert(decision.reason == ETH_LINK_GUARD_REASON_OK);
    assert(state.actions_in_window == 0U);
    assert(!state.have_last_action);

    /* 再次静默应立即允许 REINIT，而不是等满 10 s 窗口。 */
    input.rx_delta = 0U;
    input.since_last_rx_ms = 2100U;
    EthLinkGuard_Evaluate(&input, &state, 6000U, &decision);
    assert(decision.action == ETH_LINK_GUARD_REINIT);
    puts("PASS recovery: RX resume resets action budget and cooldown");
}

/**
 * @brief   验证 PHY 断开、netif down 与速率未知时不误触发恢复。
 * @return  无返回值。
 * @note    PHY 断开应清空预算；netif down 交由生成代码拉起；未知速率不判为不一致。
 */
static void TestNoFalseActions(void)
{
    EthLinkGuardState state;
    EthLinkGuardDecision decision;
    EthLinkGuard_StateInit(&state, 0U);

    EthLinkGuardInput input = Input100Full();
    input.phy_link_up = false;
    input.rx_delta = 0U;
    input.since_last_rx_ms = 6000U;
    EthLinkGuard_Evaluate(&input, &state, 1000U, &decision);
    assert(decision.action == ETH_LINK_GUARD_NONE);
    assert(decision.reason == ETH_LINK_GUARD_REASON_PHY_DOWN);
    assert(state.actions_in_window == 0U);

    input = Input100Full();
    input.netif_link_up = false;
    input.rx_delta = 0U;
    input.since_last_rx_ms = 6000U;
    EthLinkGuard_Evaluate(&input, &state, 2000U, &decision);
    assert(decision.action == ETH_LINK_GUARD_NONE);
    assert(decision.reason == ETH_LINK_GUARD_REASON_NETIF_DOWN);

    input = Input100Full();
    input.phy_speed_mbps = 0U;
    input.mac_100m = false;
    input.mac_full_duplex = false;
    input.phy_full_duplex = false;
    EthLinkGuard_Evaluate(&input, &state, 3000U, &decision);
    assert(decision.action == ETH_LINK_GUARD_NONE);
    assert(decision.reason == ETH_LINK_GUARD_REASON_OK);
    puts("PASS no false action: PHY down/netif down/unknown speed stay NONE");
}

/**
 * @brief   验证 PHY 断开再连接后可立即恢复动作预算。
 * @return  无返回值。
 * @note    断链视为链路转换，不应消耗上一次故障的 10 s 窗口。
 */
static void TestPhyDownResetsBudget(void)
{
    EthLinkGuardState state;
    EthLinkGuardDecision decision;
    EthLinkGuard_StateInit(&state, 0U);

    EthLinkGuardInput input = Input100Full();
    input.rx_delta = 0U;
    input.since_last_rx_ms = 2500U;
    EthLinkGuard_Evaluate(&input, &state, 2500U, &decision);
    assert(decision.action == ETH_LINK_GUARD_REINIT);
    assert(state.actions_in_window == 1U);

    input.phy_link_up = false;
    EthLinkGuard_Evaluate(&input, &state, 3000U, &decision);
    assert(state.actions_in_window == 0U);

    input = Input100Full();
    input.rx_delta = 0U;
    input.since_last_rx_ms = 2100U;
    EthLinkGuard_Evaluate(&input, &state, 4000U, &decision);
    assert(decision.action == ETH_LINK_GUARD_REINIT);
    puts("PASS PHY down: link transition resets bounded-action budget");
}

/**
 * @brief   验证对端静默但本板无 DMA 错误时不重初始化本板 MAC。
 * @return  无返回值。
 * @note    对端掉线不能通过重置本板 ETH 恢复，REINIT 还会清掉 PTP 配置。
 */
static void TestPeerSilenceNoReinit(void)
{
    EthLinkGuardState state;
    EthLinkGuardDecision decision;
    EthLinkGuard_StateInit(&state, 0U);

    EthLinkGuardInput input = Input100Full();
    input.rx_delta = 0U;
    input.since_last_rx_ms = 2500U;
    input.rx_dma_error = false;
    EthLinkGuard_Evaluate(&input, &state, 2500U, &decision);
    assert(decision.action == ETH_LINK_GUARD_NONE);
    assert(decision.reason == ETH_LINK_GUARD_REASON_RX_STALE);
    assert(state.actions_in_window == 0U);
    puts("PASS peer silence without DMA error: report only, no local MAC reset");
}

int main(void)
{
    TestConsistent();
    TestMismatch();
    TestDuplexAndPriority();
    TestStaleRxBounded();
    TestRecoveryResetsBudget();
    TestNoFalseActions();
    TestPhyDownResetsBudget();
    TestPeerSilenceNoReinit();
    puts("All eth_link_guard host tests passed.");
    return 0;
}
