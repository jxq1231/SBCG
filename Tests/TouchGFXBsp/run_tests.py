"""Exercise real startup clears using native virtual memory at the exact A/B addresses."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'Logs/UI/U3/tests/bsp'
out.mkdir(parents=True, exist_ok=True)
source = root / 'App/BSP/bsp_touchgfx_display.c'
if not source.exists():
    source = out / 'missing_display.c'
    source.write_text('''#include "BSP/bsp_touchgfx_display.h"
#include <stddef.h>
bool BSP_TouchGFXDisplay_Init(void){return false;}
void BSP_TouchGFXDisplay_Process(void){}
bool BSP_TouchGFXDisplay_Reveal(uint32_t a){(void)a;return false;}
void BSP_TouchGFXDisplay_GetSnapshot(BSP_TouchGFXDisplaySnapshot*s){if(s)*s=(BSP_TouchGFXDisplaySnapshot){0};}
''', encoding='utf-8')
exe = out / 'display.exe'
subprocess.run([shutil.which('gcc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-pedantic', '-I', str(root / 'Tests/LcdBsp/stubs'), '-I', str(root / 'App'),
                str(root / 'Tests/TouchGFXBsp/test_display.c'), str(source),
                '-Wl,--large-address-aware', '-o', str(exe)],
               check=True, timeout=30)
for case in ['normal', 'not_ready', 'cache', 'clock', 'timing', 'address', 'pitch',
             'init_error', 'init_underflow', 'underflow', 'transfer', 'hal_error',
             'ready_lost', 'cache_changed', 'invalid']:
    subprocess.run([str(exe), case], check=True, timeout=10)
