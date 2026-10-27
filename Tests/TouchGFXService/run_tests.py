"""Native service tests with actual GNU wrap and shared business snapshot."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'Logs/UI/U3/tests/service'
out.mkdir(parents=True, exist_ok=True)
source = root / 'App/UI/touchgfx_service.c'
if not source.exists():
    source = out / 'missing_service.c'
    source.write_text('''#include "UI/touchgfx_service.h"
#include <stddef.h>
void UI_TouchGFX_Init(uint32_t n,void(*l)(const char*,int)){(void)n;(void)l;}
void UI_TouchGFX_Process(uint32_t n,bool a){(void)n;(void)a;}
bool UI_TouchGFX_GetText(UI_StatusText*t,uint32_t*g){(void)t;(void)g;return false;}
void UI_TouchGFX_GetSnapshot(UI_TouchGFXSnapshot*s){if(s)*s=(UI_TouchGFXSnapshot){0};}
void UI_TouchGFX_FrameCompleted(uint32_t a){(void)a;}
void UI_TouchGFX_BufferVisible(uint32_t a){(void)a;}
void __wrap_MX_TouchGFX_Process(void) {}
''', encoding='utf-8')
sources = [root / 'Tests/TouchGFXService/test_service.c',
           root / 'Tests/TouchGFXService/test_framework.c', source,
           root / 'App/UI/status_view.c']
snapshot = root / 'App/UI/status_snapshot.c'
if snapshot.exists():
    sources.append(snapshot)
measurement = root / 'App/Measure/measurement_result.c'
if measurement.exists():
    sources.append(measurement)
    tracker = root / 'App/Measure/ranging_tracker.c'
    if tracker.exists():
        sources.append(tracker)
exe = out / 'service.exe'
subprocess.run([shutil.which('gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-pedantic', '-I', str(root / 'App'), *map(str, sources),
                '-Wl,--wrap=MX_TouchGFX_Process', '-o', str(exe)], check=True, timeout=30)
for case in ['basic', 'gate', 'initfail', 'b', 'irq', 'fail', 'audio_error', 'drop',
             'invalid', 'framework_error', 'race', 'initial_address', 'no_draw', 'timing']:
    subprocess.run([str(exe), case], check=True, timeout=10)
