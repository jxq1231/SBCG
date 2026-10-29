"""原生C主机验证；不修改固件构建，不播放或连接开发板。"""
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'Logs/Detector/M4_2_1'


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('gcc')
    if not compiler:
        raise RuntimeError('Native GCC required')
    source = ROOT / 'App/Detector/chirp_matcher.c'
    phase = 'GREEN' if source.exists() else 'RED'
    if not source.exists():
        source = OUTPUT / 'missing_chirp_matcher.c'
        source.write_text('''#include "Detector/chirp_matcher.h"
#include <stddef.h>
const ChirpMatcherConfig ChirpMatcher_DefaultConfig={64U,0.8f,48U,24000U};
bool ChirpMatcher_Init(ChirpMatcher *m,const int16_t *t,uint16_t n,const ChirpMatcherConfig *c)
{(void)t;(void)n;(void)c;if(m)*m=(ChirpMatcher){0};return false;}
bool ChirpMatcher_Reset(ChirpMatcher *m){(void)m;return false;}
ChirpMatcherResult ChirpMatcher_Push(ChirpMatcher *m,uint32_t s,uint64_t f,int16_t x,ChirpMatch *o)
{(void)m;(void)s;(void)f;(void)x;if(o)*o=(ChirpMatch){0};return CHIRP_MATCH_INVALID;}
''', encoding='utf-8')
    for optimization in ('-O0', '-O2'):
        exe = OUTPUT / f'test_chirp_matcher_{optimization[1:]}.exe'
        command = [compiler, '-std=c11', optimization, '-Wall', '-Wextra', '-Werror', '-pedantic',
                   '-I', str(ROOT / 'App'), str(ROOT / 'Tests/ChirpMatcher/test_chirp_matcher.c'),
                   str(source), str(ROOT/'App/Detector/chirp_template.c'), '-o', str(exe), '-lm']
        build = subprocess.run(command, capture_output=True, text=True, timeout=30)
        report = 'BUILD\n' + build.stdout + build.stderr
        code = build.returncode
        if code == 0:
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            report += 'RUN\n' + run.stdout + run.stderr
            code = run.returncode
        (OUTPUT / f'{phase}-{optimization[1:]}.txt').write_text(report, encoding='utf-8')
        print(report, flush=True)
        if code:
            return code
    return subprocess.run([sys.executable, '-B', str(ROOT / 'Tests/ChirpMatcher/reference_check.py')],
                          cwd=ROOT, timeout=180).returncode


if __name__ == '__main__':
    sys.exit(main())
