/**
 * @file        measurement_gate.c
 * @brief       静默/瞬态/失域测量状态机实现。
 * @details     只有已分类CHIRP + 标准序列形成 + 预热完成才允许发布；序列/测量后
 *              超过SILENCE_MS无新序列则撤销；FAULT需域恢复后重新预热。
 * @note        纯状态机，无硬件/网络/内存分配。
 */
#include "measurement_gate.h"
#include <stddef.h>
#include <string.h>

static MeasurementGateSnapshot gate;
static uint32_t last_sequence_ms;
static bool invalidation_pending;

static void SetState(MeasurementGateState state,uint32_t now_ms,bool can_publish)
{
    gate.state=state;
    gate.state_since_ms=now_ms;
    gate.can_publish=can_publish;
}

void MeasurementGate_Init(uint32_t now_ms)
{
    memset(&gate,0,sizeof gate);
    gate.session_valid=false;
    SetState(MEASUREMENT_GATE_BOOT,now_ms,false);
    last_sequence_ms=now_ms;
    invalidation_pending=false;
}

void MeasurementGate_OnChirp(uint32_t now_ms,uint32_t session)
{
    if(gate.session_valid && gate.session!=session)
    {
        invalidation_pending=true;
        gate.session=session;
        SetState(MEASUREMENT_GATE_BOOT,now_ms,false);
        last_sequence_ms=now_ms;
    }
    gate.session=session;gate.session_valid=true;
    gate.last_chirp_ms=now_ms;
}

void MeasurementGate_OnSequence(uint32_t now_ms)
{
    if(gate.state==MEASUREMENT_GATE_FAULT)return;
    if(gate.state==MEASUREMENT_GATE_BOOT &&
       (uint32_t)(now_ms-gate.state_since_ms)<MEASUREMENT_GATE_WARMUP_MS)return;
    last_sequence_ms=now_ms;
    SetState(MEASUREMENT_GATE_CAND,now_ms,true);
}

bool MeasurementGate_OnMeasurement(uint32_t now_ms)
{
    if(!gate.can_publish)return false;
    last_sequence_ms=now_ms;
    SetState(MEASUREMENT_GATE_MEASURED,now_ms,true);
    return true;
}

void MeasurementGate_OnDomainInvalid(uint32_t now_ms)
{
    invalidation_pending=true;
    SetState(MEASUREMENT_GATE_FAULT,now_ms,false);
}

void MeasurementGate_OnDomainValid(uint32_t now_ms)
{
    if(gate.state==MEASUREMENT_GATE_FAULT)
    { SetState(MEASUREMENT_GATE_BOOT,now_ms,false); }
}

void MeasurementGate_OnStreamInvalid(uint32_t now_ms)
{
    invalidation_pending=true;
    SetState(MEASUREMENT_GATE_FAULT,now_ms,false);
}

void MeasurementGate_Tick(uint32_t now_ms)
{
    if(gate.state==MEASUREMENT_GATE_BOOT &&
       (uint32_t)(now_ms-gate.state_since_ms)>=MEASUREMENT_GATE_WARMUP_MS)
    { SetState(MEASUREMENT_GATE_WAIT,now_ms,false); }
    if((gate.state==MEASUREMENT_GATE_CAND || gate.state==MEASUREMENT_GATE_MEASURED) &&
       (uint32_t)(now_ms-last_sequence_ms)>=MEASUREMENT_GATE_SILENCE_MS)
    {
        invalidation_pending=true;
        SetState(MEASUREMENT_GATE_SILENCE,now_ms,false);
    }
}

bool MeasurementGate_CanPublish(const MeasurementGateSnapshot *snapshot)
{
    return snapshot!=NULL && snapshot->can_publish;
}

bool MeasurementGate_TakeInvalidation(void)
{
    const bool pending=invalidation_pending;
    invalidation_pending=false;
    return pending;
}

void MeasurementGate_Get(MeasurementGateSnapshot *out)
{
    if(out!=NULL)*out=gate;
}
