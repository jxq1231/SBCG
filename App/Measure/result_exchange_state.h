/**
 * @file        result_exchange_state.h
 * @brief       A板权威结果重发/ACK/对端启动代际状态（纯C）。
 * @details     500ms同ID刷新；收到MACK后停止；B ACK序号回退（重启）或域恢复时
 *              清除ACK，让当前结果重新发送。
 * @note        只管理ID/时间/序号，不发包、不访问HAL/网络。
 */
#ifndef SBCG_RESULT_EXCHANGE_STATE_H
#define SBCG_RESULT_EXCHANGE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#define RESULT_REFRESH_MS 500U

typedef struct
{
    uint32_t last_id;
    uint32_t last_tx_ms;
    uint32_t acked_id;
    uint32_t last_peer_sequence;
    bool have_peer_sequence;
} ResultExchangeState;

void ResultExchange_Init(ResultExchangeState *state);

/** @brief 域恢复/令牌重建：允许当前结果重新发送。 */
void ResultExchange_OnDomainRestored(ResultExchangeState *state);

/** @brief 观察B的DOMAIN_ACK序号；回退/重置表示B重启，清除ACK。 */
void ResultExchange_OnPeerAckSequence(ResultExchangeState *state,uint32_t sequence);

/** @brief 收到成功MACK：停止该ID周期刷新。 */
void ResultExchange_OnAck(ResultExchangeState *state,uint32_t measurement_id);

/** @brief 当前是否应发送该结果（ID变化立即；同ID最多500ms；已ACK不重发）。 */
bool ResultExchange_ShouldSend(const ResultExchangeState *state,uint32_t now_ms,
                               uint32_t measurement_id);

/** @brief 记录成功发送。 */
void ResultExchange_MarkSent(ResultExchangeState *state,uint32_t now_ms,
                             uint32_t measurement_id);

#endif
