#define main prior_network_main
#include "../Network/test_ethernet.c"
#undef main
int main(void)
{
    EthernetMonitorSnapshot s;
    Reset(); EthernetMonitor_Init(0U,LogLine);
    EthernetMonitor_GetSnapshot(0U,&s); assert(!s.valid);
    EthernetMonitor_Process(0U);
    const uint32_t reads=read_calls, writes=write_calls;
    EthernetMonitor_GetSnapshot(0U,&s); assert(s.valid && !s.link_up && s.age_ms==0U);
    EthernetMonitor_GetSnapshot(999U,&s); assert(s.valid);
    EthernetMonitor_GetSnapshot(1000U,&s); assert(!s.valid);
    EthernetMonitor_GetSnapshot(0U,NULL);
    assert(read_calls==reads && write_calls==writes);
    fail_next_read=true; EthernetMonitor_Process(250U);
    EthernetMonitor_GetSnapshot(250U,&s); assert(!s.valid);
    Reset(); EthernetMonitor_Init(UINT32_MAX-100U,LogLine);
    EthernetMonitor_Process(UINT32_MAX-100U);
    EthernetMonitor_GetSnapshot(99U,&s); assert(s.valid && s.age_ms==200U);
    puts("PASS cached network snapshots, stale/error/wrap and zero extra PHY reads"); return 0;
}
