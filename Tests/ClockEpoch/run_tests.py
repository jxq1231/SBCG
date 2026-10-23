"""Run real consumer regressions plus clock-epoch invalidation tests; host only."""
from pathlib import Path
import shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
def main():
 gcc=shutil.which('gcc');assert gcc,'Host GCC required'
 out=ROOT/'Logs/TimeSync/M2_3_2/epoch-tests';out.mkdir(parents=True,exist_ok=True)
 cases={
 'ptp_clock':['App/Sync/ptp_clock.c'],
 'ptp_role':['App/Sync/ptp_clock.c'],
 'anchor':['App/Audio/audio_time_anchor.c'],
 'map':['App/Audio/audio_time_map_monitor.c','App/Audio/audio_time_map.c'],
 'observation':['App/Sync/sync_estimator.c'],
 'exchange':['App/Network/udp_time_exchange.c','App/Sync/ptp_rx.c','App/Sync/ptp_tx.c','App/Sync/sync_observation.c','App/Sync/sync_estimator.c']}
 failed=0
 for name,sources in cases.items():
  exe=out/(name+'.exe');cmd=[gcc,'-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'App')]
  if name in ('anchor','map'):cmd+=['-I',str(ROOT/'Tests/Audio/stubs')]
  if name in ('ptp_clock','ptp_role','exchange'):
   cmd+=['-DSTM32F746xx','-DHAL_ETH_USE_PTP','-I',str(ROOT/'Tests/Ptp/stubs')]
   for p in ('Core/Inc','Drivers/STM32F7xx_HAL_Driver/Inc','Drivers/CMSIS/Device/ST/STM32F7xx/Include','Drivers/CMSIS/Include'):cmd+=['-isystem',str(ROOT/p)]
  if name=='ptp_role':cmd+=['-DSBCG_USE_BOARD_SYNC=1']
  if name in ('ptp_clock','ptp_role'):cmd+=['-Wl,--wrap=__mingw_snprintf','-Wl,--wrap=snprintf']
  if name=='exchange':
   for p in ('LWIP/Target','Middlewares/Third_Party/LwIP/src/include','Middlewares/Third_Party/LwIP/system'):cmd+=['-I',str(ROOT/p)]
  cmd += [str(ROOT/'Tests/ClockEpoch'/('test_'+name+'.c')),*[str(ROOT/s) for s in sources],'-o',str(exe)]
  p=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True,timeout=45)
  if p.returncode==0:p=subprocess.run([str(exe)],cwd=ROOT,capture_output=True,text=True,timeout=45)
  print(name,p.returncode,p.stdout+p.stderr,flush=True);failed+=p.returncode!=0
 return int(failed!=0)
if __name__=='__main__':sys.exit(main())
