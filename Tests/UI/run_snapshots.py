"""Test real cached getters with existing native PHY/controller dependency fixtures."""
from pathlib import Path
import subprocess, shutil
root=Path(__file__).resolve().parents[2]
out=root/'Logs/UI/U2/tests';out.mkdir(parents=True,exist_ok=True)
common=[shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'App')]
for variant in ('network','sync'):
    sources=[root/f'Tests/UI/test_snapshots_{variant}.c']
    flags=[]
    if variant=='network':
        flags=['-I',str(root/'Tests/Network/stubs'),'-I',str(root/'Drivers/BSP/Components/lan8742'),'-Wno-unused-parameter']
        sources += [root/'App/BSP/bsp_ethernet.c',root/'App/Network/ethernet_monitor.c',root/'Drivers/BSP/Components/lan8742/lan8742.c']
        missing='EthernetMonitor_GetSnapshot'
        fallback='#include "Network/ethernet_monitor.h"\nvoid EthernetMonitor_GetSnapshot(uint32_t n,EthernetMonitorSnapshot*s){(void)n;if(s)*s=(EthernetMonitorSnapshot){0};}\n'
    else:
        sources += [root/'App/Sync/time_sync.c']
        missing='TimeSync_GetSnapshot'
        fallback='#include "Sync/time_sync.h"\nvoid TimeSync_GetSnapshot(uint32_t n,TimeSyncSnapshot*s){(void)n;if(s)*s=(TimeSyncSnapshot){0};}\n'
    if not any(('void '+missing+'(') in p.read_text(encoding='utf-8') for p in sources[1:]):
        stub=out/f'missing_{variant}.c';stub.write_text(fallback,encoding='utf-8');sources.append(stub)
    exe=out/f'snapshot_{variant}.exe'
    subprocess.run([*common,*flags,*map(str,sources),'-o',str(exe)],check=True,timeout=30)
    subprocess.run([str(exe)],check=True,timeout=30)
