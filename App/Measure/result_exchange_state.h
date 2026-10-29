/**
 * @file        result_exchange_state.h
 * @brief       A板权威结果重发/ACK状态（纯C）。
 * @details     500ms同ID刷新；只有精确四元组ACK才停止；失败尝试同样占用节拍。
 * @note        只管理ID/时间/四元组，不发包、不访问HAL/网络。
 */
#ifndef SBCG_RESULT_EXCHANGE_STATE_H
#define SBCG_RESULT_EXCHANGE_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "Network/ranging_status_wire.h"

#define RESULT_REFRESH_MS 500U

/** @brief RST2独立确认状态；保存精确四元组和失败尝试节奏，不保存测量值。 */
typedef struct
{
    RangingStatusAck pending;
    uint32_t last_tx_ms;
    bool have_attempt;
    bool sent;
    bool acked;
} RangingExchangeState;
/** @brief 清空当前会话确认状态；s可NULL，无返回值。 */
void RangingExchange_Init(RangingExchangeState *s);
/** @brief 判断非零域/epoch/revision的当前状态是否需要发送；ID可为无值状态0。 */
bool RangingExchange_ShouldSend(const RangingExchangeState *s,uint32_t now_ms,
                                 const RangingStatusAck *key);
/** @brief 发送前记录尝试；key只复制，失败也保留500ms退避，无返回值。 */
void RangingExchange_MarkAttempt(RangingExchangeState *s,uint32_t now_ms,
                                  const RangingStatusAck *key,bool sent);
/** @brief 仅精确匹配已成功发送的四元组且ACK_OK才确认；不改变源期限。 */
bool RangingExchange_OnAck(RangingExchangeState *s,const RangingStatusAck *ack);

#endif
