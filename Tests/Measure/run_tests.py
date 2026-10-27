"""Host tests for pure two-board pairing/distance and event wire; no hardware."""
import shutil
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
compiler=shutil.which('gcc')
if compiler is None:
    print('gcc not found',file=sys.stderr);sys.exit(2)
out=ROOT/'Logs/Measure/Tests';out.mkdir(parents=True,exist_ok=True)
result=0
for option in ('-O0','-O2'):
    for name,test,sources in (
        ('test_sound_pair','Tests/Measure/test_sound_pair.c',['App/Measure/sound_pair.c']),
        ('test_sound_event_wire','Tests/Measure/test_sound_event_wire.c',
         ['App/Measure/sound_pair.c','App/Network/sound_event_wire.c']),
        ('test_measurement_result','Tests/Measure/test_measurement_result.c',
         ['App/Measure/sound_pair.c','App/Measure/measurement_result.c']),
        ('test_measurement_wire','Tests/Measure/test_measurement_wire.c',
         ['App/Measure/sound_pair.c','App/Network/measurement_wire.c']),
        ('test_measurement_gate','Tests/Measure/test_measurement_gate.c',
         ['App/Measure/measurement_gate.c']),
        ('test_ranging_tracker','Tests/Measure/test_ranging_tracker.c',
         ['App/Measure/ranging_tracker.c']),
        ('test_ranging_status_wire','Tests/Measure/test_ranging_status_wire.c',
         ['App/Measure/ranging_tracker.c','App/Network/ranging_status_wire.c']),
        ('test_result_exchange_state','Tests/Measure/test_result_exchange_state.c',
         ['App/Measure/result_exchange_state.c'])):
        exe=out/(name+'_'+option[1:]+'.exe')
        command=[compiler,'-std=c11',option,'-Wall','-Wextra','-Werror','-pedantic',
                 '-I',str(ROOT/'App'),str(ROOT/test),*[str(ROOT/s) for s in sources],
                 '-o',str(exe)]
        built=subprocess.run(command,capture_output=True,text=True,timeout=30)
        if built.returncode:
            print(built.stdout+built.stderr);result=result or built.returncode;continue
        ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
        print(ran.stdout+ran.stderr,end='')
        result=result or ran.returncode
sys.exit(result)
