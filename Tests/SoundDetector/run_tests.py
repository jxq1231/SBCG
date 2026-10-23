"""Native tests only: no firmware build, serial connection or flashing."""
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    output = root / "Logs/Detector/M4_1_1"
    output.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which("gcc")
    if not compiler:
        raise RuntimeError("Native GCC required")
    source = root / "App/Detector/sound_detector.c"
    phase = "GREEN" if source.exists() else "RED"
    if not source.exists():
        # Compiling missing-feature stand-in: RED must be behavioral, not a link error.
        source = output / "missing_detector.c"
        source.write_text('''#include "Detector/sound_detector.h"
#include <stddef.h>
bool SoundDetector_Init(SoundDetector *d,const SoundDetectorConfig *c){(void)c;if(d)*d=(SoundDetector){0};return false;}
bool SoundDetector_Reset(SoundDetector *d){(void)d;return false;}
SoundDetectorResult SoundDetector_Push(SoundDetector *d,uint32_t s,uint64_t f,int16_t x,SoundCandidate *o){(void)d;(void)s;(void)f;(void)x;if(o)*o=(SoundCandidate){0};return SOUND_DETECTOR_INVALID;}
''', encoding="utf-8")
    for optimization in ["-O0", "-O2"]:
        exe = output / ("test_sound_detector_" + optimization[1:] + ".exe")
        command = [compiler, "-std=c11", optimization, "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-I", str(root / "App"), str(root / "Tests/SoundDetector/test_sound_detector.c"),
                   str(source), "-o", str(exe)]
        build = subprocess.run(command, capture_output=True, text=True, timeout=30)
        log = "BUILD\n" + build.stdout + build.stderr
        result = None
        if build.returncode == 0:
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            log += "RUN\n" + result.stdout + result.stderr
        code = build.returncode if result is None else result.returncode
        log += f"\nexit={code}\n"
        (output / (phase + "-" + optimization[1:] + ".log")).write_text(log, encoding="utf-8")
        print(log, flush=True)
        if code:
            return code
    return subprocess.run([sys.executable, "-B", str(root / "Tests/SoundDetector/reference_check.py")],
                          cwd=root, check=False, timeout=90).returncode


if __name__ == "__main__":
    sys.exit(main())
