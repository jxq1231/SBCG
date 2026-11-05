/**
 * @file        test_mode_service.c
 * @brief       在原生 C 进程内用两个独立实例测试真实模式握手。
 * @details     前台按固定毫秒步进，经过有界转发表传输原始报文副本并注入丢包、乱序和复位。
 * @note        夹具注入严格递增的启动代次，测试模式状态与业务调度无关。
 */
#include "Mode/mode_service.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

void ModeServiceB_Init(BoardRole role);
bool ModeServiceB_SetBoot(uint64_t boot);
bool ModeServiceB_Request(ModeKind mode);
void ModeServiceB_OnControl(const ModeControlMessage *message);
void ModeServiceB_Process(uint32_t now_ms, bool link_up,
                         bool (*send)(const ModeControlMessage *));
void ModeServiceB_GetSnapshot(ModeSnapshot *out);

static struct
{
    uint32_t now_ms;
    ModeControlMessage queued[32];
    size_t count;
    ModeControlMessage last[2][MODE_CONTROL_PONG + 1];
    bool lost_first[MODE_CONTROL_PONG + 1];
    bool drop_a_ping;
    bool replay_pong;
    ModeControlMessage stale_pong;
} network;

/**
 * @brief   复制或按当前测试条件丢弃发出的控制报文。
 * @param   message 非 NULL 临时报文，返回前复制。
 * @return  始终报告本地发送已提交，模拟 UDP 丢包发生在网络中。
 * @note    原控制回调在下一步投递，避免同步重入被测状态机。
 */
static bool Route(const ModeControlMessage *message)
{
    const unsigned role = message->role == BOARD_ROLE_A ? 0U : 1U;
    network.last[role][message->op] = *message;
    if (network.lost_first[message->op])
    {
        network.lost_first[message->op] = false;
        return true;
    }
    if (network.drop_a_ping && role == 0U && message->op == MODE_CONTROL_PING)
    {
        return true;
    }
    if (network.replay_pong && message->op == MODE_CONTROL_PONG)
    {
        message = &network.stale_pong;
    }
    assert(network.count < sizeof network.queued / sizeof network.queued[0]);
    network.queued[network.count++] = *message;
    return true;
}

/**
 * @brief   用不同启动代次初始化两块模拟板和网络。
 * @param   start_ms 本地毫秒计数起点，可设置在回绕附近。
 * @return  无返回值。
 * @note    A 启动默认请求 RANGE，B 不能主动请求。
 */
static void Reset(uint32_t start_ms)
{
    memset(&network, 0, sizeof network);
    network.now_ms = start_ms;
    ModeService_Init(BOARD_ROLE_A);
    ModeServiceB_Init(BOARD_ROLE_B);
    assert(!ModeService_SetBoot(0U));
    assert(ModeService_SetBoot(11U));
    assert(!ModeService_SetBoot(12U));
    assert(ModeServiceB_SetBoot(21U));
    assert(!ModeServiceB_Request(MODE_WAVE));
    assert(!ModeService_Request(MODE_NONE));
}

/**
 * @brief   投递上一轮报文并推进两个前台状态机。
 * @param   steps 执行次数，每步推进 25 ms。
 * @param   link 模拟两端是否有物理链路。
 * @return  无返回值。
 */
static void Advance(unsigned steps, bool link)
{
    for (unsigned step = 0U; step < steps; ++step)
    {
        for (size_t i = 0U; i < network.count; ++i)
        {
            const ModeControlMessage *message = &network.queued[i];
            if (message->role == BOARD_ROLE_A)
            {
                ModeServiceB_OnControl(message);
            }
            else
            {
                ModeService_OnControl(message);
            }
        }
        network.count = 0U;
        ModeService_Process(network.now_ms, link, Route);
        ModeServiceB_Process(network.now_ms, link, Route);
        network.now_ms += 25U;
    }
}

/**
 * @brief   验证双方均已提交同一个非零世代。
 * @param   mode 期望双方生效的工作模式。
 * @return  A 端快照，供测试保存历史代次。
 */
static ModeSnapshot ExpectActive(ModeKind mode)
{
    ModeSnapshot a, b;
    ModeService_GetSnapshot(&a);
    ModeServiceB_GetSnapshot(&b);
    assert(a.state == MODE_ACTIVE && b.state == MODE_ACTIVE);
    assert(a.role == BOARD_ROLE_A && b.role == BOARD_ROLE_B);
    assert(a.requested == mode && b.requested == MODE_NONE);
    assert(a.effective == mode && b.effective == mode);
    assert(a.a_boot != 0U && a.a_boot == b.a_boot);
    assert(a.b_boot != 0U && a.b_boot == b.b_boot);
    assert(a.mode_seq != 0U && a.mode_seq == b.mode_seq);
    return a;
}

/** @brief 首次各操作丢包后启动收敛，覆盖 HAL tick 回绕。 @return 无返回值。 */
static void TestStartupLossAndWrap(void)
{
    Reset(UINT32_MAX - 299U);
    for (unsigned op = MODE_CONTROL_HELLO; op <= MODE_CONTROL_PONG; ++op)
    {
        network.lost_first[op] = true;
    }
    Advance(120U, true);
    (void)ExpectActive(MODE_RANGE);
}

/** @brief 验证请求的可观测性独立于启动、链路和模式切换。 @return 无返回值。 */
static void TestRequestVisibleBeforeSynchronization(void)
{
    ModeSnapshot a;
    ModeService_Init(BOARD_ROLE_A);
    assert(ModeService_Request(MODE_WAVE));
    ModeService_GetSnapshot(&a);
    assert(a.state == MODE_STARTUP && a.requested == MODE_WAVE && a.effective == MODE_NONE);
    ModeService_Process(100U, true, Route);
    ModeService_GetSnapshot(&a);
    assert(a.requested == MODE_WAVE);
    assert(ModeService_SetBoot(11U));
    ModeService_Process(200U, false, Route);
    assert(ModeService_Request(MODE_CLAP));
    ModeService_GetSnapshot(&a);
    assert(a.state == MODE_SYNC_WAIT && a.requested == MODE_CLAP && a.effective == MODE_NONE);
    ModeService_Process(300U, false, Route);
    ModeService_GetSnapshot(&a);
    assert(a.requested == MODE_CLAP);
}

/** @brief 快速请求、同模式幂等与延迟旧 PREPARE/COMMIT。 @return 无返回值。 */
static void TestNewRequestTombstonesOldGeneration(void)
{
    Reset(0U);
    Advance(20U, true);
    ModeSnapshot first = ExpectActive(MODE_RANGE);
    ModeControlMessage old_prepare = network.last[0][MODE_CONTROL_PREPARE];
    ModeControlMessage old_commit = network.last[0][MODE_CONTROL_COMMIT];
    assert(ModeService_Request(MODE_WAVE));
    ModeSnapshot pending;
    ModeService_GetSnapshot(&pending);
    assert(pending.requested == MODE_WAVE && pending.effective == MODE_RANGE);
    assert(ModeService_Request(MODE_RANGE));
    Advance(2U, true);
    assert(ExpectActive(MODE_RANGE).mode_seq == first.mode_seq);
    assert(ModeService_Request(MODE_CLAP));
    assert(ModeService_Request(MODE_CALIBRATION));
    Advance(30U, true);
    ModeSnapshot next = ExpectActive(MODE_CALIBRATION);
    assert(next.mode_seq == first.mode_seq + 1U);
    assert(ModeService_Request(MODE_CALIBRATION));
    ModeServiceB_OnControl(&old_prepare);
    ModeServiceB_OnControl(&old_commit);
    Advance(30U, true);
    assert(ExpectActive(MODE_CALIBRATION).mode_seq == next.mode_seq);
}

/** @brief B 即使收到本地探测响应，也须先看到提交后的 A 探测。 @return 无返回值。 */
static void TestBWaitsForAPostCommitPing(void)
{
    Reset(0U);
    network.drop_a_ping = true;
    Advance(30U, true);
    ModeSnapshot b;
    ModeServiceB_GetSnapshot(&b);
    assert(network.last[0][MODE_CONTROL_PONG].probe_id != 0U);
    assert(b.state != MODE_ACTIVE && b.effective == MODE_NONE);
    network.drop_a_ping = false;
    Advance(35U, true);
    (void)ExpectActive(MODE_RANGE);
}

/** @brief 消费过的 PONG 不延长租约，恢复需新序号。 @return 无返回值。 */
static void TestReplayCannotExtendLease(void)
{
    Reset(0U);
    Advance(24U, true);
    const ModeSnapshot first = ExpectActive(MODE_RANGE);
    network.stale_pong = network.last[1][MODE_CONTROL_PONG];
    network.replay_pong = true;
    Advance(89U, true);
    ModeSnapshot a, b;
    ModeService_GetSnapshot(&a);
    ModeServiceB_GetSnapshot(&b);
    assert(a.state != MODE_ACTIVE && a.effective == MODE_NONE);
    assert(b.state != MODE_ACTIVE && b.effective == MODE_NONE);
    network.replay_pong = false;
    Advance(150U, true);
    assert(ExpectActive(MODE_RANGE).mode_seq > first.mode_seq);
}

/** @brief 突发重复探测只占用一个回复名额，保留本机周期探测。 @return 无返回值。 */
static void TestBurstKeepsSendBudgetAndProbe(void)
{
    Reset(0U);
    Advance(20U, true);
    (void)ExpectActive(MODE_RANGE);
    const ModeControlMessage a_ping = network.last[0][MODE_CONTROL_PING];
    const uint64_t previous_probe = network.last[1][MODE_CONTROL_PING].probe_id;
    assert(a_ping.probe_id != 0U && previous_probe != 0U);
    for (unsigned duplicate = 0U; duplicate < 8U; ++duplicate)
    {
        ModeServiceB_OnControl(&a_ping);
    }
    const size_t before = network.count;
    ModeServiceB_Process(network.now_ms + 250U, true, Route);
    assert(network.count == before + 2U);
    assert(network.queued[before].op == MODE_CONTROL_PONG);
    assert(network.queued[before + 1U].op == MODE_CONTROL_PING);
    assert(network.last[1][MODE_CONTROL_PING].probe_id > previous_probe);

    /* 下一轮仍有 HELLO 发送资格，前一轮探测不占用本轮节拍。 */
    ModeServiceB_Process(network.now_ms + 275U, true, Route);
    assert(network.count == before + 3U);
    assert(network.queued[before + 2U].op == MODE_CONTROL_HELLO);
}

/** @brief A、B 分别复位后重放旧代次报文，活跃方水位必须前进。 @return 无返回值。 */
static void TestEitherBoardReboot(void)
{
    Reset(0U);
    Advance(20U, true);
    (void)ExpectActive(MODE_RANGE);
    const ModeControlMessage old_hello = network.last[1][MODE_CONTROL_HELLO];
    const ModeControlMessage old_prepare = network.last[0][MODE_CONTROL_PREPARE];
    const ModeControlMessage old_commit = network.last[0][MODE_CONTROL_COMMIT];
    ModeServiceB_Init(BOARD_ROLE_B);
    assert(ModeServiceB_SetBoot(22U));
    ModeServiceB_OnControl(&old_prepare);
    ModeServiceB_OnControl(&old_commit);
    Advance(30U, true);
    assert(ExpectActive(MODE_RANGE).b_boot == 22U);
    ModeService_OnControl(&old_hello);
    Advance(2U, true);
    assert(ExpectActive(MODE_RANGE).b_boot == 22U);
    ModeService_Init(BOARD_ROLE_A);
    assert(ModeService_SetBoot(12U));
    Advance(42U, true);
    assert(ExpectActive(MODE_RANGE).a_boot == 12U);
    ModeControlMessage delayed_prepare = old_prepare;
    delayed_prepare.b_boot = 22U;
    delayed_prepare.mode_seq = UINT64_MAX;
    ModeServiceB_OnControl(&delayed_prepare);
    Advance(2U, true);
    assert(ExpectActive(MODE_RANGE).a_boot == 12U);
}

/** @brief 物理链路断开立即撤值，重连不接受旧提交。 @return 无返回值。 */
static void TestLinkDownAndReconnect(void)
{
    Reset(0U);
    Advance(20U, true);
    const ModeSnapshot first = ExpectActive(MODE_RANGE);
    const ModeControlMessage old_prepare = network.last[0][MODE_CONTROL_PREPARE];
    const ModeControlMessage old_commit = network.last[0][MODE_CONTROL_COMMIT];
    Advance(1U, false);
    ModeSnapshot a, b;
    ModeService_GetSnapshot(&a);
    ModeServiceB_GetSnapshot(&b);
    assert(a.effective == MODE_NONE && b.effective == MODE_NONE);
    ModeServiceB_OnControl(&old_prepare);
    ModeServiceB_OnControl(&old_commit);
    Advance(45U, true);
    assert(ExpectActive(MODE_RANGE).mode_seq > first.mode_seq);
}

/** @brief 运行模式握手的原生回归用例。 @return 全部断言通过时返回零。 */
int main(void)
{
    TestStartupLossAndWrap();
    TestRequestVisibleBeforeSynchronization();
    TestNewRequestTombstonesOldGeneration();
    TestBWaitsForAPostCommitPing();
    TestReplayCannotExtendLease();
    TestBurstKeepsSendBudgetAndProbe();
    TestEitherBoardReboot();
    TestLinkDownAndReconnect();
    puts("ModeService: 8 scenarios passed");
    return 0;
}
