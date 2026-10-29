#define main prior_sync_main
#include "../SyncController/test_time_sync.c"
#undef main
int main(void)
{
    TimeSyncSnapshot s; Reset(); TimeSync_GetSnapshot(now_tick,&s); assert(!s.configured && !s.offset_valid);
    Init(BOARD_ROLE_A); TimeSync_GetSnapshot(now_tick,&s);
    assert(s.configured && s.role==BOARD_ROLE_A && !s.offset_valid && !s.internal_locked);
    Reset(); Init(BOARD_ROLE_B); Sample();
    active_servo->have_sample=true; active_servo->state=CLOCK_SERVO_LOCKED_INTERNAL;
    active_servo->internal_locked=true; active_servo->offset_x2_ns=-200;
    active_servo->last_sample_ms=now_tick;
    const uint32_t old_writes=clock_writes, old_ticks=tick_calls, old_protocol=protocol_calls;
    const ClockServo before=*active_servo;
    TimeSync_GetSnapshot(now_tick,&s); assert(s.offset_valid && s.internal_locked && s.offset_x2_ns==-200);
    TimeSync_GetSnapshot(now_tick+1999U,&s); assert(s.offset_valid);
    TimeSync_GetSnapshot(now_tick+2000U,&s); assert(!s.offset_valid && !s.internal_locked);
    TimeSync_GetSnapshot(now_tick,NULL);
    assert(clock_writes==old_writes && tick_calls==old_ticks && protocol_calls==old_protocol);
    assert(memcmp(&before,active_servo,sizeof before)==0);
    active_servo->state=CLOCK_SERVO_HOLDOVER; TimeSync_GetSnapshot(now_tick,&s); assert(!s.offset_valid);
    active_servo->state=CLOCK_SERVO_FAULT; TimeSync_GetSnapshot(now_tick,&s); assert(s.faulted && !s.offset_valid);
    active_servo->state=CLOCK_SERVO_TRACKING; active_servo->last_sample_ms=UINT32_MAX-100U;
    TimeSync_GetSnapshot(99U,&s); assert(s.offset_valid && s.sample_age_ms==200U);
    assert(TimeSync_Deinit()); TimeSync_GetSnapshot(now_tick,&s); assert(!s.configured && !s.offset_valid);
    puts("PASS read-only A/B sync snapshot validity, stale/fault/wrap, no control side effects"); return 0;
}
