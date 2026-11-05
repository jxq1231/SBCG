/**
 * @file        mode_service.c
 * @brief       处理 A/B 双板模式切换握手和独立续约。
 * @details     接收回调只入有界队列，前台采用严格递增水位拒绝已经废弃的代次。
 * @note        不读硬件、共同时间或业务模块。所有计时使用调用方给出的本机毫秒计数。
 */
#include "mode_service.h"
#include <stddef.h>

#define MODE_INBOX_CAPACITY 8U
#define MODE_HANDSHAKE_RETRY_MS 250U
#define MODE_HANDSHAKE_TIMEOUT_MS 2000U
#define MODE_HELLO_PERIOD_MS 500U
#define MODE_PROBE_PERIOD_MS 500U
#define MODE_PROBE_TIMEOUT_MS 750U
#define MODE_LEASE_TIMEOUT_MS 2000U

/** @brief A 的 PREPARE/COMMIT 等确认与 B 的 PREPARE 等待共用计时。 */
typedef enum
{
    PHASE_WAIT,
    PHASE_PREPARE,
    PHASE_COMMIT,
    PHASE_VERIFY,
    PHASE_ACTIVE
} ModePhase;

typedef bool (*ModeSend)(const ModeControlMessage *);

/** @brief 本轮待回复操作，只借用当前收件箱中的请求，离开 Process 前消费。 */
typedef struct
{
    const ModeControlMessage *request;
    ModeControlOp op;
} ModeReply;

typedef struct
{
    ModeSnapshot view;
    ModeKind requested_applied;
    ModeKind candidate;
    ModePhase phase;
    bool boot_set;
    bool failed;
    bool hello_attempted;
    bool phase_attempted;
    bool probe_attempted;
    bool probe_pending;
    bool have_pong;
    bool have_a_ping;
    uint32_t phase_ms;
    uint32_t hello_tx_ms;
    uint32_t phase_tx_ms;
    uint32_t probe_tx_ms;
    uint32_t pong_ms;
    uint64_t probe_id;
    ModeControlMessage inbox[MODE_INBOX_CAPACITY];
    size_t inbox_count;
} ModeServiceState;

static ModeServiceState service;

/** @brief 检查业务模式枚举。 @param mode 待检查的模式。 @return 是否为有效模式。 */
static bool IsMode(ModeKind mode)
{
    return mode >= MODE_RANGE && mode <= MODE_CALIBRATION;
}

/** @brief 初始化只读角色与默认请求。 @param role 本机角色。 @return 无返回值。 */
void ModeService_Init(BoardRole role)
{
    service = (ModeServiceState){0};
    service.view.role = role;
    service.view.state = MODE_STARTUP;
    service.view.reason = MODE_REASON_BOOT;
    service.view.requested = role == BOARD_ROLE_A ? MODE_RANGE : MODE_NONE;
    service.requested_applied = service.view.requested;
}

/** @brief 锁存本机启动身份。 @param boot 本机非零启动代次。 @return 首次登记是否成功。 */
bool ModeService_SetBoot(uint64_t boot)
{
    if (boot == 0U || service.boot_set)
    {
        return false;
    }
    if (service.view.role == BOARD_ROLE_A)
    {
        service.view.a_boot = boot;
    }
    else if (service.view.role == BOARD_ROLE_B)
    {
        service.view.b_boot = boot;
    }
    else
    {
        return false;
    }
    service.boot_set = true;
    service.view.state = MODE_SYNC_WAIT;
    service.view.reason = MODE_REASON_PEER;
    return true;
}

/** @brief 登记最新的 A 板请求。 @param mode 待同步模式。 @return 是否接收该请求。 */
bool ModeService_Request(ModeKind mode)
{
    if (service.view.role != BOARD_ROLE_A || service.failed || !IsMode(mode))
    {
        return false;
    }
    service.view.requested = mode;
    return true;
}

/** @brief 入队网络交接副本。 @param message 已验证只读输入，可为 NULL。 @return 无返回值。 */
void ModeService_OnControl(const ModeControlMessage *message)
{
    if (service.boot_set && !service.failed && message != NULL &&
        service.inbox_count < MODE_INBOX_CAPACITY)
    {
        service.inbox[service.inbox_count++] = *message;
    }
}

/**
 * @brief   关闭当前模式资格并清待决探测，保留启动、序号和探测水位。
 * @param   reason 进入等待的原因。
 * @return  无返回值。
 */
static void Wait(ModeReason reason)
{
    service.phase = PHASE_WAIT;
    service.candidate = MODE_NONE;
    service.view.state = MODE_SYNC_WAIT;
    service.view.reason = reason;
    service.view.effective = MODE_NONE;
    service.probe_pending = false;
    service.have_pong = false;
    service.have_a_ping = false;
}

/** @brief 启动内序号耗尽时停用本代次。 @return 无返回值。 */
static void Exhausted(void)
{
    Wait(MODE_REASON_EXHAUSTED);
    service.failed = true;
    service.view.state = MODE_ERROR;
}

/**
 * @brief   开始一个握手阶段，重试节流从本阶段起算。
 * @param   phase 待确认的 PREPARE、COMMIT 或 VERIFY 阶段。
 * @param   now_ms 当前本机毫秒计数。
 * @return  无返回值。
 */
static void EnterPhase(ModePhase phase, uint32_t now_ms)
{
    service.phase = phase;
    service.phase_ms = now_ms;
    service.phase_attempted = false;
    service.view.state = MODE_SWITCHING;
    service.view.effective = MODE_NONE;
}

/**
 * @brief   为 A 板建立更高序号的模式候选。
 * @param   now_ms 当前本机毫秒计数。
 * @param   reason 发起同步的原因。
 * @return  无返回值。
 */
static void BeginA(uint32_t now_ms, ModeReason reason)
{
    Wait(reason);
    if (service.view.mode_seq == UINT64_MAX)
    {
        Exhausted();
        return;
    }
    ++service.view.mode_seq;
    service.candidate = service.requested_applied;
    EnterPhase(PHASE_PREPARE, now_ms);
}

/**
 * @brief   提交当前代次的一份控制消息并记发送尝试。
 * @param   op 控制操作。HELLO 使用本机 B 代次并清其余会话字段。
 * @param   probe_id 对 PING/PONG 为请求代号，其他操作传零。
 * @param   send 非 NULL 前台提交回调，只在调用期间借用报文。
 * @return  是否已提交到本地网络栈。
 */
static bool SendControl(ModeControlOp op, uint64_t probe_id, ModeSend send)
{
    ModeControlMessage message = {
        .op = op,
        .mode = service.candidate,
        .role = service.view.role,
        .a_boot = service.view.a_boot,
        .b_boot = service.view.b_boot,
        .mode_seq = service.view.mode_seq,
        .probe_id = probe_id
    };
    if (op == MODE_CONTROL_HELLO)
    {
        message.mode = MODE_NONE;
        message.a_boot = 0U;
        message.mode_seq = 0U;
    }
    if (service.view.attempts < UINT32_MAX)
    {
        ++service.view.attempts;
    }
    return send(&message);
}

/**
 * @brief   判断消息是否属于当前尚有效的模式候选。
 * @param   message 非 NULL 已解码报文。
 * @return  启动代次、模式序号和模式均与当前候选相同时为 true。
 */
static bool Matches(const ModeControlMessage *message)
{
    return service.phase != PHASE_WAIT && message->a_boot == service.view.a_boot &&
        message->b_boot == service.view.b_boot && message->mode_seq == service.view.mode_seq &&
        message->mode == service.candidate;
}

/**
 * @brief   选择本轮唯一回复，当前代次优先，其次保留更靠后的确认或更新的探测。
 * @param   reply 非 NULL 本轮可写回复槽。
 * @param   message 非 NULL 收件箱报文，在本轮发送结束前有效。
 * @param   op READY、COMMITTED 或 PONG，线路编号顺序与回复优先级一致。
 * @return  无返回值。
 */
static void QueueReply(ModeReply *reply, const ModeControlMessage *message, ModeControlOp op)
{
    if (reply->request == NULL || !Matches(reply->request) || reply->op < op ||
        (reply->op == op && reply->request->probe_id < message->probe_id))
    {
        reply->request = message;
        reply->op = op;
    }
}

/**
 * @brief   完成提交确认后开始本机租约验证。
 * @param   now_ms 当前本机毫秒计数。
 * @return  无返回值。
 */
static void BeginVerify(uint32_t now_ms)
{
    EnterPhase(PHASE_VERIFY, now_ms);
    service.view.reason = MODE_REASON_HANDSHAKE;
    service.probe_attempted = false;
    service.probe_pending = false;
    service.have_pong = false;
    service.have_a_ping = false;
}

/** @brief 探测闭环满足后发布有效模式。 @return 无返回值。 */
static void Promote(void)
{
    if (service.phase == PHASE_VERIFY && service.have_pong &&
        (service.view.role == BOARD_ROLE_A || service.have_a_ping))
    {
        service.phase = PHASE_ACTIVE;
        service.view.effective = service.candidate;
        service.view.state = MODE_ACTIVE;
        service.view.reason = MODE_REASON_OK;
    }
}

/**
 * @brief   处理双向探测。只有当前未完成的本机探测可刷新租约。
 * @param   message 非 NULL 且与当前候选匹配的控制报文。
 * @param   now_ms 当前本机毫秒计数。
 * @param   reply 非 NULL 本轮可写回复槽。
 * @return  无返回值。
 */
static void HandleProbe(const ModeControlMessage *message, uint32_t now_ms, ModeReply *reply)
{
    const bool verifying = service.phase == PHASE_VERIFY || service.phase == PHASE_ACTIVE;
    if (message->op == MODE_CONTROL_PING && message->probe_id != 0U)
    {
        if (verifying || (service.view.role == BOARD_ROLE_A && service.phase == PHASE_COMMIT))
        {
            QueueReply(reply, message, MODE_CONTROL_PONG);
            if (service.view.role == BOARD_ROLE_B)
            {
                service.have_a_ping = true;
                Promote();
            }
        }
    }
    else if (message->op == MODE_CONTROL_PONG && verifying && service.probe_pending &&
             message->probe_id == service.probe_id &&
             (uint32_t)(now_ms - service.probe_tx_ms) < MODE_PROBE_TIMEOUT_MS)
    {
        service.probe_pending = false;
        service.have_pong = true;
        service.pong_ms = now_ms;
        Promote();
    }
}

/**
 * @brief   A 发现 B 或推进当前模式握手。
 * @param   message 非 NULL 已通过固定对端检查且角色为 B 的报文。
 * @param   now_ms 当前本机毫秒计数。
 * @param   reply 非 NULL 本轮可写回复槽。
 * @return  无返回值。
 */
static void ReceiveA(const ModeControlMessage *message, uint32_t now_ms, ModeReply *reply)
{
    if (message->op == MODE_CONTROL_HELLO)
    {
        if ((message->a_boot != 0U && message->a_boot != service.view.a_boot) ||
            message->b_boot == 0U || message->b_boot < service.view.b_boot)
        {
            return;
        }
        if (message->b_boot > service.view.b_boot || service.phase == PHASE_WAIT)
        {
            service.view.b_boot = message->b_boot;
            BeginA(now_ms, MODE_REASON_PEER);
        }
        return;
    }
    if (!Matches(message))
    {
        return;
    }
    if (message->op == MODE_CONTROL_READY && service.phase == PHASE_PREPARE)
    {
        EnterPhase(PHASE_COMMIT, now_ms);
        service.view.reason = MODE_REASON_HANDSHAKE;
    }
    else if (message->op == MODE_CONTROL_COMMITTED && service.phase == PHASE_COMMIT)
    {
        BeginVerify(now_ms);
    }
    else
    {
        HandleProbe(message, now_ms, reply);
    }
}

/**
 * @brief   B 检查 PREPARE 的启动和序号水位，或幂等回复当前请求。
 * @param   message 非 NULL 已通过固定对端检查且角色为 A 的 PREPARE。
 * @param   now_ms 当前本机毫秒计数。
 * @param   reply 非 NULL 本轮可写回复槽。
 * @return  无返回值。
 */
static void PrepareB(const ModeControlMessage *message, uint32_t now_ms, ModeReply *reply)
{
    if (message->b_boot != service.view.b_boot || message->a_boot == 0U ||
        message->mode_seq == 0U || !IsMode(message->mode))
    {
        return;
    }
    if (message->a_boot > service.view.a_boot ||
        (message->a_boot == service.view.a_boot && message->mode_seq > service.view.mode_seq))
    {
        Wait(MODE_REASON_HANDSHAKE);
        service.view.a_boot = message->a_boot;
        service.view.mode_seq = message->mode_seq;
        service.candidate = message->mode;
        EnterPhase(PHASE_PREPARE, now_ms);
    }
    if (Matches(message))
    {
        QueueReply(reply, message, MODE_CONTROL_READY);
    }
}

/**
 * @brief   B 确认 A 的候选或推进已提交候选的探测。
 * @param   message 非 NULL 已通过固定对端检查且角色为 A 的报文。
 * @param   now_ms 当前本机毫秒计数。
 * @param   reply 非 NULL 本轮可写回复槽。
 * @return  无返回值。
 */
static void ReceiveB(const ModeControlMessage *message, uint32_t now_ms, ModeReply *reply)
{
    if (message->op == MODE_CONTROL_PREPARE)
    {
        PrepareB(message, now_ms, reply);
    }
    else if (Matches(message))
    {
        if (message->op == MODE_CONTROL_COMMIT)
        {
            if (service.phase == PHASE_PREPARE)
            {
                BeginVerify(now_ms);
            }
            QueueReply(reply, message, MODE_CONTROL_COMMITTED);
        }
        else
        {
            HandleProbe(message, now_ms, reply);
        }
    }
}

/**
 * @brief   按照本机单调时间处理事务或租约过期。
 * @param   now_ms 当前本机毫秒计数。
 * @return  无返回值。
 * @note    先于本轮入箱报文处理，迟到确认不能恢复已废弃的事务。
 */
static void Expire(uint32_t now_ms)
{
    if (service.phase == PHASE_ACTIVE)
    {
        if ((uint32_t)(now_ms - service.pong_ms) >= MODE_LEASE_TIMEOUT_MS)
        {
            Wait(MODE_REASON_LEASE);
        }
    }
    else if (service.phase != PHASE_WAIT &&
             (uint32_t)(now_ms - service.phase_ms) >= MODE_HANDSHAKE_TIMEOUT_MS)
    {
        Wait(MODE_REASON_HANDSHAKE);
    }
}

/**
 * @brief   用一个周期发送名额提交握手、探测或 B 的发现报文。
 * @param   now_ms 当前本机毫秒计数。
 * @param   send 非 NULL 前台提交函数。
 * @return  无返回值。
 * @note    B 两类报文同时到期时优先探测，首个 HELLO 或已等待两个周期的 HELLO 优先发送。
 *          只有实际提交时才占用相应计时节拍。
 */
static void Poll(uint32_t now_ms, ModeSend send)
{
    const uint32_t since_hello_ms = now_ms - service.hello_tx_ms;
    const uint32_t since_probe_ms = now_ms - service.probe_tx_ms;
    const bool hello_due = service.view.role == BOARD_ROLE_B &&
        (!service.hello_attempted || since_hello_ms >= MODE_HELLO_PERIOD_MS);
    const bool probe_due = (service.phase == PHASE_VERIFY || service.phase == PHASE_ACTIVE) &&
        (!service.probe_attempted || since_probe_ms >=
        (service.probe_pending ? MODE_PROBE_TIMEOUT_MS : MODE_PROBE_PERIOD_MS));
    if (hello_due && (!probe_due || !service.hello_attempted ||
                      since_hello_ms >= 2U * MODE_HELLO_PERIOD_MS))
    {
        service.hello_attempted = true;
        service.hello_tx_ms = now_ms;
        (void)SendControl(MODE_CONTROL_HELLO, 0U, send);
        return;
    }
    if (service.view.role == BOARD_ROLE_A &&
        (service.phase == PHASE_PREPARE || service.phase == PHASE_COMMIT) &&
        (!service.phase_attempted || (uint32_t)(now_ms - service.phase_tx_ms) >= MODE_HANDSHAKE_RETRY_MS))
    {
        service.phase_attempted = true;
        service.phase_tx_ms = now_ms;
        const ModeControlOp op = service.phase == PHASE_PREPARE ? MODE_CONTROL_PREPARE : MODE_CONTROL_COMMIT;
        (void)SendControl(op, 0U, send);
        return;
    }
    if (!probe_due)
    {
        return;
    }
    if (service.probe_id == UINT64_MAX)
    {
        Exhausted();
        return;
    }
    service.probe_attempted = true;
    service.probe_tx_ms = now_ms;
    ++service.probe_id;
    service.probe_pending = SendControl(MODE_CONTROL_PING, service.probe_id, send);
}

/**
 * @brief   依次处理链路、超时、最新请求、网络收件箱和周期发送。
 * @param   now_ms 当前本机毫秒计数。
 * @param   link_up 本机链路可用时为 true。
 * @param   send 控制报文发送回调，NULL 表示当前通道不可用。
 * @return  无返回值。
 */
void ModeService_Process(uint32_t now_ms, bool link_up, ModeSend send)
{
    if (!service.boot_set || service.failed)
    {
        service.inbox_count = 0U;
        return;
    }
    if (!link_up || send == NULL)
    {
        service.inbox_count = 0U;
        service.hello_attempted = false;
        Wait(MODE_REASON_LINK);
        return;
    }
    Expire(now_ms);
    if (service.requested_applied != service.view.requested)
    {
        service.requested_applied = service.view.requested;
        if (service.phase != PHASE_WAIT)
        {
            BeginA(now_ms, MODE_REASON_REQUEST);
        }
    }
    ModeReply reply = {0};
    for (size_t i = 0U; i < service.inbox_count && !service.failed; ++i)
    {
        const ModeControlMessage *message = &service.inbox[i];
        if (service.view.role == BOARD_ROLE_A && message->role == BOARD_ROLE_B)
        {
            ReceiveA(message, now_ms, &reply);
        }
        else if (service.view.role == BOARD_ROLE_B && message->role == BOARD_ROLE_A)
        {
            ReceiveB(message, now_ms, &reply);
        }
    }
    if (!service.failed)
    {
        if (reply.request != NULL && Matches(reply.request))
        {
            (void)SendControl(reply.op, reply.request->probe_id, send);
        }
        Poll(now_ms, send);
    }
    service.inbox_count = 0U;
}

/** @brief 读取只读状态副本。 @param out 调用方所有的输出，可为 NULL。 @return 无返回值。 */
void ModeService_GetSnapshot(ModeSnapshot *out)
{
    if (out != NULL)
    {
        *out = service.view;
    }
}
