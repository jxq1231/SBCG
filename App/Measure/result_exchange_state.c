/**
 * @file        result_exchange_state.c
 * @brief       A板结果重发/ACK状态实现。
 * @details     序号用有符号差判断回退；时间用无符号差支持回绕。
 * @note        纯状态，无网络/HAL/内存分配。
 */
#include "result_exchange_state.h"
#include <stddef.h>
#include <string.h>

void ResultExchange_Init(ResultExchangeState *state)
{
    if(state!=NULL)memset(state,0,sizeof *state);
}

void ResultExchange_OnDomainRestored(ResultExchangeState *state)
{
    if(state==NULL)return;
    state->acked_id=0U;
    state->last_id=0U;
}

void ResultExchange_OnPeerAckSequence(ResultExchangeState *state,uint32_t sequence)
{
    if(state==NULL)return;
    /* Only a real backward step is treated as a peer restart; an equal
       sequence is a UDP duplicate/retransmission and must not clear the ACK. */
    if(state->have_peer_sequence &&
       (int32_t)(sequence-state->last_peer_sequence)<0)
    {
        state->acked_id=0U;
        state->last_id=0U;
    }
    state->last_peer_sequence=sequence;
    state->have_peer_sequence=true;
}

void ResultExchange_OnAck(ResultExchangeState *state,uint32_t measurement_id)
{
    if(state==NULL || measurement_id==0U)return;
    state->acked_id=measurement_id;
}

bool ResultExchange_ShouldSend(const ResultExchangeState *state,uint32_t now_ms,
                               uint32_t measurement_id)
{
    if(state==NULL || measurement_id==0U)return false;
    if(measurement_id==state->acked_id)return false;
    if(measurement_id==state->last_id &&
       (uint32_t)(now_ms-state->last_tx_ms)<RESULT_REFRESH_MS)return false;
    return true;
}

void ResultExchange_MarkSent(ResultExchangeState *state,uint32_t now_ms,
                             uint32_t measurement_id)
{
    if(state==NULL)return;
    state->last_id=measurement_id;
    state->last_tx_ms=now_ms;
}
