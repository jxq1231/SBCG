"""Compile real native SDRAM code; outputs in ignored Logs/UI/U1/tests."""
from pathlib import Path
import shutil,subprocess
root=Path(__file__).resolve().parents[2]
out=root/'Logs/UI/U1/tests';out.mkdir(parents=True,exist_ok=True)
source=root/'App/BSP/sdram_test.c'
if not source.exists():
    source=out/'missing_sdram.c'
    source.write_text('''#include "BSP/sdram_test.h"
bool SDRAM_TestInit(SDRAM_Test*t,volatile void*m,uint32_t n){(void)t;(void)m;(void)n;return false;}
void SDRAM_TestStep(SDRAM_Test*t,uint32_t n){(void)t;(void)n;}
''',encoding='utf-8')
command=[shutil.which('gcc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic',
    '-I',str(root/'App'),str(root/'Tests/Sdram/test_sdram_test.c'),str(source),'-o',str(out/'core.exe')]
subprocess.run(command,check=True,timeout=30)
subprocess.run([str(out/'core.exe')],check=True,timeout=30)
bsp=root/'App/BSP/bsp_sdram.c'
if not bsp.exists():
    bsp=out/'missing_bsp.c'
    bsp.write_text('''#include "BSP/bsp_sdram.h"
bool BSP_SDRAM_Init(void){return false;}
void BSP_SDRAM_Process(uint32_t n,bool a){(void)n;(void)a;}
void BSP_SDRAM_GetSnapshot(BSP_SdramSnapshot*s){if(s)*s=(BSP_SdramSnapshot){0};}
''',encoding='utf-8')
command=[shutil.which('gcc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic',
    '-I',str(root/'Tests/Sdram/stubs'),'-I',str(root/'App'),str(root/'Tests/Sdram/test_bsp_sdram.c'),str(bsp),'-o',str(out/'bsp.exe')]
subprocess.run(command,check=True,timeout=30)
for case in ['ok','pass','memory','selector0','selector1','selector2','selector7',
             'mpu2_missing','mpu2_base','mpu2_cache','mpu_disabled','clock','pin','timing','controller','mpu','cache','handle','fmc_clock',
             'busy_before','busy_after','command1','command2','command3','command4','refresh','readback']:
    subprocess.run([str(out/'bsp.exe'),case],check=True,timeout=30)
flags=['-DSBCG_USE_LWIP=1','-DSBCG_USE_AUDIO=1','-DSBCG_USE_AUDIO_ANCHOR=1',
       '-DSBCG_USE_AUDIO_MAP=1','-DSBCG_USE_SOUND_TIME=1','-DSBCG_USE_BOARD_SYNC=1',
       '-DSBCG_USE_BOARD_IDENTITY=1','-DSBCG_USE_SDRAM=1']
command=[shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-pedantic',*flags,
    '-I',str(root/'Tests/App/stubs'),'-I',str(root/'App'),str(root/'Tests/Sdram/test_app_sdram.c'),
    str(root/'App/app_main.c'),str(root/'App/BSP/board_identity.c'),'-o',str(out/'app.exe')]
subprocess.run(command,check=True,timeout=30)
subprocess.run([str(out/'app.exe')],check=True,timeout=30)
