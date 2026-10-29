"""Native early-boot register evidence tests; no device access."""
from pathlib import Path
import shutil
import subprocess
root=Path(__file__).resolve().parents[2]
out=root/'Logs/ResetDiagnosis/native'
out.mkdir(parents=True,exist_ok=True)
source=root/'App/BSP/bsp_reset.c'
if not source.exists():
    source=out/'missing_reset.c'
    source.write_text('#include "BSP/bsp_reset.h"\nvoid BSP_Reset_Capture(void){}\nbool BSP_Reset_GetSnapshot(BSP_ResetSnapshot*s){(void)s;return false;}\n',encoding='utf-8')
exe=out/'reset.exe'
subprocess.run([shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-pedantic',
               '-I',str(root/'Tests/ResetBsp/stubs'),'-I',str(root/'App'),
               str(root/'Tests/ResetBsp/test_reset.c'),str(source),'-o',str(exe)],check=True,timeout=30)
for value in [0,3,0x02000000,0x04000000,0x08000000,0x10000000,0x20000000,
              0x40000000,0x80000000,0x0e000000,0xfe000003]:
    subprocess.run([str(exe),f'{value:x}','0'],check=True,timeout=10)
subprocess.run([str(exe),'fe000003','1'],check=True,timeout=10)
