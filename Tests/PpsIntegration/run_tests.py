"""Real TimeSync/PPS integration with existing controller mocks; no hardware."""
from pathlib import Path
import subprocess,shutil,sys

def main():
 root=Path(__file__).resolve().parents[2]
 out=root/'Logs/PPS/M2_3_3';out.mkdir(parents=True,exist_ok=True)
 exe=out/'test_controller_pps.exe'
 cmd=[shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-DSBCG_USE_PPS=1','-I',str(root/'App'),str(root/'Tests/PpsIntegration/test_controller_pps.c'),str(root/'App/Sync/time_sync.c'),'-Wl,--wrap=BSP_PTP_Step','-Wl,--wrap=BSP_PTP_SetFrequencyPpb','-o',str(exe)]
 p=subprocess.run(cmd,cwd=root,check=False,timeout=30)
 if p.returncode:return p.returncode
 return subprocess.run([str(exe)],cwd=root,check=False,timeout=30).returncode
if __name__=='__main__':sys.exit(main())
