"""Host tests for 0.5m single-point calibration; no HAL."""
import shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
gcc=shutil.which('gcc')
if gcc is None: print('gcc not found',file=sys.stderr);sys.exit(2)
out=ROOT/'Logs/Calibration';out.mkdir(parents=True,exist_ok=True)
res=0
for opt in ('-O0','-O2'):
 exe=out/('test_calibration_'+opt[1:]+'.exe')
 cmd=[gcc,'-std=c11',opt,'-Wall','-Wextra','-Werror','-pedantic','-I',str(ROOT/'App'),str(ROOT/'Tests/Calibration/test_calibration.c'),str(ROOT/'App/Calibration/calibration.c'),'-o',str(exe)]
 b=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
 if b.returncode: print(b.stdout+b.stderr);res=res or b.returncode;continue
 r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);print(r.stdout+r.stderr,end='');res=res or r.returncode
sys.exit(res)
