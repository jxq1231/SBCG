"""Run real chirp PCM capture tests on the host; never access hardware."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def main():
    parent=ROOT/'Logs/Detector/M4_2_6_PCM_Capture';parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='pcm-capture-',dir=parent));print('TEST_OUTPUT',out,flush=True)
    sources=['Tests/ChirpService/test_pcm_capture.c',
             'App/Detector/chirp_window.c','App/Detector/chirp_matcher.c',
             'App/Detector/chirp_template.c','App/Detector/sound_detector.c']
    for opt in ('-O0','-O2'):
        exe=out/f'capture-{opt[1:]}.exe'
        cmd=[shutil.which('gcc'),'-std=c11',opt,'-Wall','-Wextra','-Werror','-pedantic',
             '-I',str(ROOT/'App'),*[str(ROOT/p) for p in sources],'-o',str(exe),'-lm']
        build=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
        report='BUILD\n'+build.stdout+build.stderr;code=build.returncode
        if code==0:
            run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            report+='RUN\n'+run.stdout+run.stderr;code=run.returncode
        (out/f'capture-{opt[1:]}.txt').write_text(report,encoding='utf-8')
        print(report,flush=True)
        if code:return code
    return 0

if __name__=='__main__':sys.exit(main())