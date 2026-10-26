#include "Measure/measurement_gate.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    MeasurementGateSnapshot gate;
    MeasurementGate_Init(0U);
    MeasurementGate_Tick(999U);MeasurementGate_Get(&gate);
    assert(gate.state==MEASUREMENT_GATE_BOOT && !gate.can_publish);
    MeasurementGate_OnChirp(999U,1U);MeasurementGate_OnSequence(999U);
    MeasurementGate_Get(&gate);assert(!gate.can_publish);
    MeasurementGate_Tick(1000U);
    MeasurementGate_OnChirp(1000U,1U);MeasurementGate_OnSequence(1000U);
    MeasurementGate_Get(&gate);
    assert(gate.state==MEASUREMENT_GATE_CAND && gate.can_publish);
    assert(MeasurementGate_OnMeasurement(1001U));
    MeasurementGate_Get(&gate);assert(gate.state==MEASUREMENT_GATE_MEASURED && gate.can_publish);
    MeasurementGate_Tick(3999U);MeasurementGate_Get(&gate);
    assert(gate.state==MEASUREMENT_GATE_MEASURED && gate.can_publish);
    MeasurementGate_Tick(4001U);MeasurementGate_Get(&gate);
    assert(gate.state==MEASUREMENT_GATE_SILENCE && !gate.can_publish);
    assert(MeasurementGate_TakeInvalidation());
    assert(!MeasurementGate_TakeInvalidation());
    /* A chirp alone does not open the gate. */
    MeasurementGate_Init(0U);MeasurementGate_Tick(1000U);
    MeasurementGate_OnChirp(1100U,1U);MeasurementGate_Get(&gate);
    assert(!gate.can_publish);
    /* Domain loss forces FAULT and requires a fresh warm-up. */
    MeasurementGate_OnSequence(1200U);assert(MeasurementGate_OnMeasurement(1201U));
    MeasurementGate_OnDomainInvalid(1202U);MeasurementGate_Get(&gate);
    assert(gate.state==MEASUREMENT_GATE_FAULT && !gate.can_publish);
    assert(MeasurementGate_TakeInvalidation());
    MeasurementGate_OnDomainValid(1300U);MeasurementGate_Get(&gate);
    assert(gate.state==MEASUREMENT_GATE_BOOT);
    MeasurementGate_OnSequence(1400U);MeasurementGate_Get(&gate);
    assert(!gate.can_publish);
    MeasurementGate_Tick(2300U);MeasurementGate_OnSequence(2300U);
    MeasurementGate_Get(&gate);assert(gate.can_publish);
    /* Session change resets to BOOT and requests invalidation. */
    MeasurementGate_OnChirp(2400U,2U);MeasurementGate_Get(&gate);
    assert(gate.state==MEASUREMENT_GATE_BOOT && !gate.can_publish);
    assert(MeasurementGate_TakeInvalidation());
    /* Stream loss also forces FAULT. */
    MeasurementGate_OnStreamInvalid(2500U);MeasurementGate_Get(&gate);
    assert(gate.state==MEASUREMENT_GATE_FAULT && !gate.can_publish);
    puts("PASS measurement gate: warm-up, sequence, silence timeout, domain/stream fault");
    return 0;
}
