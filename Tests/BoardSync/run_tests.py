"""Native PTPv2 transport tests; hardware IO alone is replaced."""
from pathlib import Path
import subprocess
import shutil
import sys
root=Path(__file__).resolve().parents[2]
out=root/'Logs/TimeSync/M2_3_2'
out.mkdir(parents=True,exist_ok=True)
gcc=shutil.which('gcc')
if not gcc:
    raise SystemExit('Native GCC is required')
common=[gcc,'-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'App')]
logs=[]
def run(command):
    p=subprocess.run(command,cwd=root,capture_output=True,text=True,timeout=30)
    logs.append(p.stdout+p.stderr)
    print(logs[-1],end='')
    return p.returncode
status=run(common+[str(root/'Tests/BoardSync/test_ptp_wire.c'),str(root/'App/Network/ptp_wire.c'),'-o',str(out/'test_ptp_wire.exe')])
if not status:
    status=run([str(out/'test_ptp_wire.exe')])
if not status and (root/'Tests/BoardSync/test_ptp_board_sync.c').exists():
    command=common+['-I',str(root/'Tests/Ptp/stubs'),'-DSTM32F746xx','-DHAL_ETH_USE_PTP']
    for directory in ['Core/Inc','Drivers/STM32F7xx_HAL_Driver/Inc','Drivers/CMSIS/Device/ST/STM32F7xx/Include','Drivers/CMSIS/Include']:
        command+=['-isystem',str(root/directory)]
    for directory in ['LWIP/Target','Middlewares/Third_Party/LwIP/src/include','Middlewares/Third_Party/LwIP/system']:
        command+=['-I',str(root/directory)]
    command += [str(root/p) for p in ['Tests/BoardSync/test_ptp_board_sync.c','App/Network/ptp_wire.c','App/Network/ptp_board_sync.c','App/BSP/board_identity.c','App/Sync/ptp_rx.c','App/Sync/ptp_tx.c']]
    command+=['-o',str(out/'test_ptp_board_sync.exe')]
    status=run(command)
    if not status:
        status=run([str(out/'test_ptp_board_sync.exe')])
if not status:
    status=run(common+[str(root/'Tests/BoardSync/test_time_domain.c'),str(root/'App/Sync/time_domain.c'),'-o',str(out/'test_time_domain.exe')])
if not status:
    status=run([str(out/'test_time_domain.exe')])
target=out/('transport_red.txt' if '--red' in sys.argv else 'transport_tests.txt')
target.write_text(''.join(logs),encoding='utf-8')
raise SystemExit(status)
