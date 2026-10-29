"""Count actual draw calls under GCC instrumentation, without timing-dependent assertions."""
from pathlib import Path
import subprocess,shutil
root=Path(__file__).resolve().parents[2];out=root/'Logs/UI/U2/tests';out.mkdir(parents=True,exist_ok=True)
exe=out/'band_work.exe'
subprocess.run([shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-finstrument-functions','-I',str(root/'App'),str(root/'Tests/UI/test_band_work.c'),str(root/'App/UI/rgb565_canvas.c'),'-o',str(exe)],check=True,timeout=30)
subprocess.run([str(exe)],check=True,timeout=30)
