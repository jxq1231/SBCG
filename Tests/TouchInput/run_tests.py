"""Compile the real touch BSP against the HAL read boundary and run host cases."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'Logs/UI/U3/tests/touch'
out.mkdir(parents=True, exist_ok=True)
exe = out / 'touch.exe'
subprocess.run([shutil.which('gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-pedantic', '-I', str(root / 'Tests/TouchInput/stubs'),
                '-I', str(root / 'App'), str(root / 'Tests/TouchInput/test_touch.c'),
                str(root / 'App/BSP/bsp_touch.c'), '-o', str(exe)],
               check=True, timeout=30)
for case in ['identity', 'coordinates', 'cache', 'retry', 'busy', 'fault', 'wrap', 'arguments']:
    subprocess.run([str(exe), case], check=True, timeout=10)

adapter_exe = out / 'touch_adapter.exe'
subprocess.run([shutil.which('g++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-pedantic',
                '-I', str(root / 'Tests/TouchInput/stubs'), '-I', str(root / 'App'),
                '-I', str(root / 'TouchGFX/target'),
                str(root / 'Tests/TouchInput/test_adapter.cpp'),
                str(root / 'TouchGFX/target/STM32TouchController.cpp'),
                '-o', str(adapter_exe)], check=True, timeout=30)
subprocess.run([str(adapter_exe)], check=True, timeout=10)
