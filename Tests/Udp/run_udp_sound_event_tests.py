"""Host integration test for local-send + peer-receive pairing; LwIP IO is stubbed."""
from pathlib import Path
import shutil
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
gcc=shutil.which('gcc')
if gcc is None:
    print('Native GCC is required',file=sys.stderr);sys.exit(2)
out=ROOT/'Logs/UDP/SoundEvent';out.mkdir(parents=True,exist_ok=True)
includes=['App','Tests/Network/stubs','LWIP/Target',
          'Middlewares/Third_Party/LwIP/src/include',
          'Middlewares/Third_Party/LwIP/system']
cmd=[gcc,'-std=c11','-Wall','-Wextra','-Werror','-pedantic']
for include in includes: cmd+=['-I',str(ROOT/include)]
cmd += [str(ROOT/'Tests/Udp/test_udp_sound_event.c'),
        str(ROOT/'App/Network/udp_sound_event.c'),
        str(ROOT/'App/Network/sound_event_wire.c'),
        str(ROOT/'App/Network/measurement_wire.c'),
        str(ROOT/'App/Network/domain_wire.c'),
        str(ROOT/'App/Network/measurement_ack.c'),
        str(ROOT/'App/Network/temperature_wire.c'),
     str(ROOT/'App/Network/ranging_status_wire.c'),str(ROOT/'App/Network/ranging_session.c'),
        str(ROOT/'App/Measure/ranging_tracker.c'),
        str(ROOT/'App/Temperature/sound_speed.c'),
        str(ROOT/'App/Measure/sound_pair.c'),
        str(ROOT/'App/Measure/waveform_tdoa.c'),
        '-o',str(out/'test_udp_sound_event.exe')]
built=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
if built.returncode:
    print(built.stdout+built.stderr);sys.exit(built.returncode)
ran=subprocess.run([str(out/'test_udp_sound_event.exe')],capture_output=True,text=True,timeout=30)
print(ran.stdout+ran.stderr,end='')
sys.exit(ran.returncode)
