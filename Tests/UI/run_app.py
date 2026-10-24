"""Compile actual App LCD dispatch with established SDRAM/Audio/Network fixtures."""
from pathlib import Path
import subprocess,shutil
root=Path(__file__).resolve().parents[2];out=root/'Logs/UI/U2/tests';out.mkdir(parents=True,exist_ok=True)
flags=[f'-DSBCG_USE_{feature}=1' for feature in ('LWIP','AUDIO','AUDIO_ANCHOR','AUDIO_MAP','SOUND_TIME','BOARD_SYNC','BOARD_IDENTITY','SDRAM','LCD')]
exe=out/'app_lcd.exe'
subprocess.run([shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-pedantic',*flags,'-I',str(root/'Tests/App/stubs'),'-I',str(root/'App'),str(root/'Tests/UI/test_app_lcd.c'),str(root/'App/app_main.c'),str(root/'App/BSP/board_identity.c'),'-o',str(exe)],check=True,timeout=30)
subprocess.run([str(exe)],check=True,timeout=30)
