"""Compile the real LCD BSP against minimal HAL stubs; never access hardware."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'Logs/UI/U2/tests/lcd'
out.mkdir(parents=True, exist_ok=True)
source = root / 'App/BSP/bsp_lcd.c'
if not source.exists():
    source = out / 'missing_lcd.c'
    source.write_text('''#include "BSP/bsp_lcd.h"
#include <stddef.h>
bool BSP_LCD_Init(void){return false;}
uint16_t *BSP_LCD_BeginFrame(void){return NULL;}
bool BSP_LCD_Present(uint32_t n){(void)n;return false;}
void BSP_LCD_Process(uint32_t n){(void)n;}
void BSP_LCD_CancelFrame(void){}
void BSP_LCD_GetSnapshot(BSP_LcdSnapshot *s){if(s)*s=(BSP_LcdSnapshot){0};}
''', encoding='utf-8')
command = [shutil.which('gcc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
           '-pedantic', '-I', str(root / 'Tests/LcdBsp/stubs'), '-I', str(root / 'App'),
           str(root / 'Tests/LcdBsp/test_bsp_lcd.c'), str(source), '-o', str(out / 'lcd.exe')]
subprocess.run(command, check=True, timeout=30)
for case in ['unsupported_ltdc_query', 'ownership', 'not_ready', 'cache', 'clock',
             'clock_n', 'clock_r', 'clock_m', 'clock_div', 'clock_sai_not_ready',
             'clock_main_not_ready', 'clock_hse_not_ready', 'clock_hsi',
             'clock_m_zero', 'clock_n_zero', 'clock_r_zero', 'timing', 'blending', 'alpha',
             'address', 'pitch', 'layer2', 'init_error', 'init_underflow', 'hal_error',
             'timeout', 'wrap', 'underflow', 'transfer', 'runtime_hal', 'ready_lost', 'cache_changed']:
    subprocess.run([str(out / 'lcd.exe'), case], check=True, timeout=10)
