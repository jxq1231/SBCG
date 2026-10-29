"""Host tests for the selected-standard periodic sequence monitor; no audio/HAL."""
import shutil
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
compiler=shutil.which('gcc')
if compiler is None:
    print('gcc not found',file=sys.stderr);sys.exit(2)
out=ROOT/'Logs/Detector/StandardSequence';out.mkdir(parents=True,exist_ok=True)
result=0
for option in ('-O0','-O2'):
    exe=out/('test_standard_sequence_'+option[1:]+'.exe')
    cmd=[compiler,'-std=c11',option,'-Wall','-Wextra','-Werror','-pedantic',
         '-I',str(ROOT/'App'),str(ROOT/'Tests/Detector/test_standard_sequence.c'),
         str(ROOT/'App/Detector/standard_sequence.c'),'-o',str(exe)]
    built=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
    if built.returncode:
        print(built.stdout+built.stderr);result=result or built.returncode;continue
    ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
    print(ran.stdout+ran.stderr,end='')
    result=result or ran.returncode
sys.exit(result)
