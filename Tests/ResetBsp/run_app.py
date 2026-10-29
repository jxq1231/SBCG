"""Exercise the actual App boot formatter and its existing queue."""
from pathlib import Path
import shutil
import subprocess
root=Path(__file__).resolve().parents[2]
out=root/'Logs/ResetDiagnosis/native'
out.mkdir(parents=True,exist_ok=True)
flags=[f'-DSBCG_USE_{f}=1' for f in ('LWIP','AUDIO','AUDIO_ANCHOR','AUDIO_MAP','SOUND_TIME','BOARD_SYNC','BOARD_IDENTITY','SDRAM','LCD','TOUCHGFX','RESET_DIAGNOSTIC')]
exe=out/'app.exe'
subprocess.run([shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-pedantic',*flags,
               '-I',str(root/'Tests/App/stubs'),'-I',str(root/'App'),
               str(root/'Tests/ResetBsp/test_app.c'),str(root/'App/app_main.c'),
               str(root/'App/BSP/board_identity.c'),'-o',str(exe)],check=True,timeout=30)
for case in ('normal','missing'):subprocess.run([str(exe),case],check=True,timeout=10)
main=(root/'Core/Src/main.c').read_text(encoding='utf-8')
early=main.split('/* USER CODE BEGIN 1 */')[1].split('/* USER CODE END 1 */')[0]
assert early.count('BSP_Reset_Capture();')==1
assert main.index('BSP_Reset_Capture();')<main.index('  HAL_Init();')
print('PASS reset early hook: exactly once inside USER CODE1 before HAL')
