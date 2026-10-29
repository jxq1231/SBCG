"""Native behavioral tests; missing implementation produces a real failing assertion."""
from pathlib import Path
import shutil,subprocess,sys
root=Path(__file__).resolve().parents[2]
out=root/'Logs/Detector/M4_1_3';out.mkdir(parents=True,exist_ok=True)
source=root/'App/Detector/sound_event_time.c'
if not source.exists():
 source=out/'missing_time.c'
 source.write_text('''#include "Detector/sound_event_time.h"
void SoundEventTime_Init(void(*l)(const char*,int)){(void)l;}
bool SoundEventTime_Submit(uint32_t n,uint32_t s,const SoundCandidate*c){(void)n;(void)s;(void)c;return false;}
void SoundEventTime_Observe(uint32_t n,const AudioTimeAnchorObservation*a){(void)n;(void)a;}
void SoundEventTime_Invalidate(SoundTimeReset r){(void)r;}
void SoundEventTime_Process(uint32_t n,const BSP_AudioSnapshot*s){(void)n;(void)s;}
void SoundEventTime_GetSnapshot(SoundEventTimeSnapshot*s){(void)s;}
''',encoding='utf-8')
exe=out/'test_event_time.exe'
command=[shutil.which('gcc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-I',str(root/'App'),str(root/'Tests/SoundEventTime/test_event_time.c'),str(source),str(root/'App/Audio/audio_time_map.c'),'-o',str(exe)]
built=subprocess.run(command,capture_output=True,text=True,timeout=30);print(built.stdout+built.stderr)
if built.returncode:sys.exit(built.returncode)
p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);print(p.stdout+p.stderr)
if p.returncode:sys.exit(p.returncode)
# Q16 window-TOA submission and independent SOUND_TOA_TIME record, both optimisation levels.
for option in ('-O0','-O2'):
 toa=out/('test_toa_time_'+option[1:]+'.exe')
 toa_command=[shutil.which('gcc'),'-std=c11',option,'-Wall','-Wextra','-Werror','-pedantic',
   '-DSBCG_USE_SOUND_TIME=1','-I',str(root/'App'),
   str(root/'Tests/SoundEventTime/test_toa_time.c'),str(source),
   str(root/'App/Audio/audio_time_map.c'),'-o',str(toa)]
 subprocess.run(toa_command,check=True,timeout=30)
 subprocess.run([str(toa)],check=True,timeout=30)
bridge=out/'event_time_cli.exe'
command[command.index(str(root/'Tests/SoundEventTime/test_event_time.c'))]=str(root/'Tests/SoundEventTime/reference_bridge.c')
command[-1]=str(bridge)
subprocess.run(command,check=True,timeout=30)
from reference_check import check
check(bridge)

for name,sources,flags in [
 ('app_time',['App/app_main.c','App/BSP/board_identity.c','Tests/App/stubs/ranging_app_stubs.c'],['-DSBCG_USE_LWIP=1','-DSBCG_USE_AUDIO=1','-DSBCG_USE_AUDIO_ANCHOR=1','-DSBCG_USE_AUDIO_MAP=1','-DSBCG_USE_BOARD_SYNC=1','-DSBCG_USE_BOARD_IDENTITY=1','-I',str(root/'Tests/App/stubs')]),
 ('clock_time',['App/Sync/time_sync.c'],([ '-DSBCG_USE_PPS=1'] if (root/'App/Sync/pps_service.h').exists() else [])+['-Wl,--wrap=BSP_PTP_Step','-Wl,--wrap=BSP_PTP_SetFrequencyPpb'])]:
 target=out/(name+'.exe')
 cmd=[shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-pedantic','-DSBCG_USE_SOUND_TIME=1','-I',str(root/'App'),*flags,str(root/'Tests/SoundEventTime'/('test_'+name+'.c')),*[str(root/s) for s in sources],'-o',str(target)]
 subprocess.run(cmd,check=True,timeout=30)
 subprocess.run([str(target)],check=True,timeout=30)
