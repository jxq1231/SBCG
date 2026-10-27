"""Host app_main result-exchange wiring test; UDP transport is a boundary stub."""
from pathlib import Path
import shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
gcc=shutil.which('gcc')
if gcc is None: print('gcc not found',file=sys.stderr);sys.exit(2)
out=ROOT/'Logs/App/UDPIntegration';out.mkdir(parents=True,exist_ok=True)
cmd=[gcc,'-std=c11','-Wall','-Wextra','-Werror','-pedantic',
     '-DSBCG_HOST_TEST=1','-DSBCG_USE_LWIP=1','-DSBCG_USE_AUDIO=1',
     '-DSBCG_USE_AUDIO_ANCHOR=1','-DSBCG_USE_AUDIO_MAP=1',
     '-DSBCG_USE_SOUND_TIME=1','-DSBCG_USE_BOARD_SYNC=1','-DSBCG_USE_BOARD_IDENTITY=1',
     '-I',str(ROOT), '-I',str(ROOT/'Tests/App'), '-I',str(ROOT/'App'), '-I',str(ROOT/'Tests/App/stubs'),
     str(ROOT/'Tests/App/test_app_udp_integration.c'),str(ROOT/'App/app_main.c'),
     str(ROOT/'App/Measure/measurement_result.c'),str(ROOT/'App/Measure/ranging_tracker.c'),str(ROOT/'App/Measure/result_exchange_state.c'),
     str(ROOT/'App/Measure/measurement_gate.c'),str(ROOT/'App/Detector/standard_sequence.c'),
     str(ROOT/'App/Calibration/calibration.c'),str(ROOT/'App/Sync/time_domain.c'),
     '-o',str(out/'test_app_udp_integration.exe')]
b=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
if b.returncode: print(b.stdout+b.stderr);sys.exit(b.returncode)
r=subprocess.run([str(out/'test_app_udp_integration.exe')],capture_output=True,text=True,timeout=30)
print(r.stdout+r.stderr,end='');sys.exit(r.returncode)
