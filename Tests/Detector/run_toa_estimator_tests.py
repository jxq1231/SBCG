"""Run real TOA estimator host tests (O0/O2); never access hardware."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parent = ROOT / 'Logs' / 'Detector' / 'M4_2_7_TOA'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='toa-', dir=parent))
    print('TEST_OUTPUT', out, flush=True)
    sources = ['Tests/Detector/test_toa_estimator.c', 'App/Detector/toa_estimator.c']
    for opt in ('-O0', '-O2'):
        exe = out / f'toa-{opt[1:]}.exe'
        cmd = [shutil.which('gcc'), '-std=c11', opt, '-Wall', '-Wextra', '-Werror', '-pedantic',
               '-I', str(ROOT / 'App'), *[str(ROOT / p) for p in sources], '-o', str(exe), '-lm']
        build = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        report = 'BUILD\n' + build.stdout + build.stderr
        code = build.returncode
        if code == 0:
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            report += 'RUN\n' + run.stdout + run.stderr
            code = run.returncode
        (out / f'toa-{opt[1:]}.txt').write_text(report, encoding='utf-8')
        print(report, flush=True)
        if code:
            return code
    return 0


if __name__ == '__main__':
    sys.exit(main())