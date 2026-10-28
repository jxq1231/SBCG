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

/** @brief 比较域、权威代际、revision和ID；参数仅在本函数借用，不比较status。 */
static bool SameRangingKey(const RangingStatusAck *a,const RangingStatusAck *b)
{
    return a->domain==b->domain && a->authority_clock_epoch==b->authority_clock_epoch &&
        a->state_revision==b->state_revision && a->measurement_id==b->measurement_id;
}

/** @brief 初始化纯前台确认状态；s可NULL，不操作硬件。 */
void RangingExchange_Init(RangingExchangeState *s)
{ if(s!=NULL)memset(s,0,sizeof *s); }

/** @brief 检查精确状态的重发资格；now_ms无符号差支持回绕，key仅借用。 */
bool RangingExchange_ShouldSend(const RangingExchangeState *s,uint32_t now_ms,
                                 const RangingStatusAck *key)
{
    if(s==NULL || key==NULL || key->domain==0U || key->authority_clock_epoch==0U ||
       key->state_revision==0U)return false;
    if(!s->have_attempt || !SameRangingKey(&s->pending,key))return true;
    return !s->acked && (uint32_t)(now_ms-s->last_tx_ms)>=RESULT_REFRESH_MS;
}

/** @brief 保存本次尝试及成功标志；失败保留时间以限制重试，所有输入按值复制。 */
void RangingExchange_MarkAttempt(RangingExchangeState *s,uint32_t now_ms,
                                  const RangingStatusAck *key,bool sent)
{
    if(s==NULL || key==NULL)return;
    s->pending=*key;s->last_tx_ms=now_ms;s->have_attempt=true;s->sent=sent;s->acked=false;
}

/** @brief ACK只确认已成功发出的精确四元组；错误ACK不改变状态或任何测量期限。 */
bool RangingExchange_OnAck(RangingExchangeState *s,const RangingStatusAck *ack)
{
    if(s==NULL || ack==NULL || !s->have_attempt || !s->sent ||
       ack->status!=RANGING_STATUS_ACK_OK || !SameRangingKey(&s->pending,ack))return false;
    s->acked=true;return true;
}
