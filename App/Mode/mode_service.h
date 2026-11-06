/**
 * @file        mode_service.h
 * @brief       声明双板模式握手、租约和只读观测接口。
 * @details     A 板提出模式，B 板确认，双方通过独立探测确认当前模式代次。
 *              A 在等待态可本地重试发起，双方对持续出现的更低启动代次执行回退收养。
 * @note        仅用于串行前台。依赖可信的启动代次和经过对端校验的控制报文；
 *              单发旧代次报文不会触发回退，只更新诊断候选。
 */
#ifndef SBCG_MODE_SERVICE_H
#define SBCG_MODE_SERVICE_H

#include "Network/mode_wire.h"

/** @brief 观测状态。只有 ACTIVE 公开有效模式。 */
typedef enum
{
    MODE_STARTUP,
    MODE_SYNC_WAIT,
    MODE_SWITCHING,
    MODE_ACTIVE,
    MODE_ERROR
} ModeState;

/** @brief 最近一次状态或握手阶段变化的原因。 */
typedef enum
{
    MODE_REASON_BOOT,
    MODE_REASON_LINK,
    MODE_REASON_PEER,
    MODE_REASON_REQUEST,
    MODE_REASON_HANDSHAKE,
    MODE_REASON_LEASE,
    MODE_REASON_OK,
    MODE_REASON_EXHAUSTED,
    MODE_REASON_ROLLBACK
} ModeReason;

/**
 * @brief   前台模式状态副本。
 * @details requested 仅记录 A 的请求。a_boot/b_boot/mode_seq 可保留已作废的观测水位。
 *          reason 是最近一次状态/阶段变化的原因，block 是当前实际阻塞原因；
 *          boot_set 表示本机启动代次已登记，waiting 为真时 wait_since_ms 有效。
 * @note    attempts 累计本机尝试提交的控制报文数，饱和于 UINT32_MAX；
 *          rollbacks 累计对端更低启动代次被持续确认并收养的次数。
 */
typedef struct
{
    BoardRole role;
    ModeKind requested;
    ModeKind effective;
    ModeState state;
    ModeReason reason;
    ModeReason block;
    uint64_t a_boot;
    uint64_t b_boot;
    uint64_t mode_seq;
    uint32_t attempts;
    uint32_t state_ms;
    uint32_t wait_since_ms;
    uint32_t rollbacks;
    bool boot_set;
    bool waiting;
    bool link_up;
} ModeSnapshot;

/**
 * @brief   初始化本次启动的模式观测服务。
 * @param   role 已验证的本机 A/B 角色。
 * @return  无返回值。
 * @note    A 默认请求 RANGE。仅在应用初始化时调用一次，运行中保留已废弃水位。
 */
void ModeService_Init(BoardRole role);

/**
 * @brief   登记本次启动的非零代次。
 * @param   boot 已保留且较本机上次启动严格递增的代次，无单位。
 * @return  true 表示本次首次登记。未配置角色、零或已登记时返回 false。
 * @note    调用者负责持久化和跨启动递增，服务只锁存本次值。
 */
bool ModeService_SetBoot(uint64_t boot);

/**
 * @brief   为 A 板暂存一个有效模式请求。
 * @param   mode RANGE、CLAP、WAVE 或 CALIBRATION。
 * @return  true 表示接受请求。B、未登记启动代次、已失效或无效模式返回 false。
 * @note    快照立即显示新请求。握手由 Process 按最新请求推进，生效模式只在握手后更新；
 *          启动代次未登记时返回 false，避免调用方把"未参与握手"误判为已提交。
 */
bool ModeService_Request(ModeKind mode);

/**
 * @brief   将固定对端的控制消息复制到前台收件箱。
 * @param   message 已按线路格式和固定对端验证的只读副本，NULL 跳过。
 * @return  无返回值。
 * @note    网络回调只登记消息，不保留指针或推进状态。收件箱满时由后续控制重试恢复。
 */
void ModeService_OnControl(const ModeControlMessage *message);

/**
 * @brief   处理链路、请求、握手及本机租约，更新模式快照。
 * @param   now_ms 本机单调毫秒计数，允许 uint32_t 常规回绕。
 * @param   link_up 本机接口及物理链路可用。
 * @param   send 控制消息提交函数，可为 NULL；收到的指针只在回调中有效，返回本地提交结果。
 * @return  无返回值。
 * @note    在网络轮询结束后由前台唯一调用。每轮最多一个合并回复和一个周期报文；发送回调不能重入本模块。
 *          A 在等待态每 2 秒或请求变化时主动发起；双方只收养持续约 3 秒的更低启动代次。
 */
void ModeService_Process(uint32_t now_ms, bool link_up,
                         bool (*send)(const ModeControlMessage *));

/**
 * @brief   复制本机模式状态。
 * @param   out 调用方所有的输出，NULL 时跳过。
 * @return  无返回值。
 * @note    快照用于观测，服务不控制采集、应用算法或页面。
 */
void ModeService_GetSnapshot(ModeSnapshot *out);

#endif
