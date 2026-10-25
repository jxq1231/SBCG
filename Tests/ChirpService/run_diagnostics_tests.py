"""Run real chirp/App diagnostic tests on the host; never access hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys

ROOT=Path(__file__).resolve().parents[2]

def main():
    parent=ROOT/'Logs/Detector/M4_2_2';parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='diagnostics-',dir=parent));print('TEST_OUTPUT',out,flush=True)
    service=['Tests/ChirpService/test_diagnostics.c','App/Detector/chirp_service.c',
             'App/Detector/chirp_window.c','App/Detector/chirp_matcher.c',
             'App/Detector/chirp_template.c','App/Detector/sound_detector.c']
    flags=['-DSBCG_USE_CHIRP=1','-DSBCG_USE_SOUND_TIME=1','-DSBCG_USE_LWIP=1',
           '-DSBCG_USE_AUDIO=1','-DSBCG_USE_AUDIO_ANCHOR=1','-DSBCG_USE_AUDIO_MAP=1']
    for opt in ('-O0','-O2'):
        for name,sources,extra in [('service',service,[]),('app',
            ['Tests/ChirpService/test_app_diagnostics.c','App/app_main.c'],flags),('app-direct',
            ['Tests/ChirpService/test_app_diagnostics.c','App/app_main.c'],
            [flag for flag in flags if flag!='-DSBCG_USE_SOUND_TIME=1'])]:
            exe=out/f'{name}-{opt[1:]}.exe'
            cmd=[shutil.which('gcc'),'-std=c11',opt,'-Wall','-Wextra','-Werror','-pedantic',
                 '-I',str(ROOT/'App'),'-I',str(ROOT/'Tests/App/stubs'),*extra,
                 *[str(ROOT/s) for s in sources],'-o',str(exe),'-lm']
            build=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
            report='BUILD\n'+build.stdout+build.stderr;code=build.returncode
            if code==0:
                run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
                report+='RUN\n'+run.stdout+run.stderr;code=run.returncode
            (out/f'{name}-{opt[1:]}.txt').write_text(report,encoding='utf-8')
            print(report,flush=True)
            if code:return code
    return 0

if __name__=='__main__':sys.exit(main())
