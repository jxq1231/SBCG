"""Native tests only: no firmware build, serial connection or flashing."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import wave


def main():
    root = Path(__file__).resolve().parents[2]
    parent = root / "Logs/Detector/M4_2_3_A"
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="native-",dir=parent))
    print("TEST_OUTPUT",output,flush=True)
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
    # A one-point stand-in makes the missing 64-point feature fail behaviorally.
    has_batch = 'SoundDetectorResult SoundDetector_PushBatch(' in source.read_text(encoding='utf-8')
    batch_sources = [source]
    flags = []
    if not has_batch:
        standin = output / 'one_point_batch_standin.c'
        standin.write_text('''#include "Detector/sound_detector.h"
#include <stddef.h>
SoundDetectorResult SoundDetector_PushBatch(SoundDetector *d,uint32_t s,uint64_t f,
 const int16_t *x,uint16_t n,uint16_t *used,SoundCandidate *out){
 if(used){*used=0;}
 if(out){*out=(SoundCandidate){0};}
 if(!used||!out||!x||n==0||n>64){return SOUND_DETECTOR_INVALID;}
 SoundDetectorResult r=SoundDetector_Push(d,s,f,x[0],out);
 if(r!=SOUND_DETECTOR_INVALID){*used=1;}
 return r;
}
''',encoding='utf-8')
        batch_sources.append(standin)
        flags.append('-DSBCG_BATCH_RED_STANDIN=1')
    with wave.open(str(root/'Data/Audio/standard_chirp_2-6khz_10x_20s.wav'),'rb') as wav:
        if wav.getnchannels()!=1 or wav.getsampwidth()!=2 or wav.getframerate()!=48000:
            raise ValueError('Frozen batch fixture must be mono PCM16 at 48kHz')
        raw=wav.readframes(wav.getnframes())
    pcm=output/'standard_chirp.pcm';pcm.write_bytes(raw)
    for optimization in ('-O0','-O2'):
        exe=output/f'test_batch_{optimization[1:]}.exe'
        command=[compiler,'-std=c11',optimization,'-Wall','-Wextra','-Werror','-pedantic',
                 '-finstrument-functions',*flags,'-I',str(root/'App'),
                 str(root/'Tests/SoundDetector/test_batch.c'),*[str(p) for p in batch_sources],'-o',str(exe)]
        built=subprocess.run(command,capture_output=True,text=True,timeout=30)
        log='BUILD\n'+built.stdout+built.stderr;code=built.returncode
        if code==0:
            result=subprocess.run([str(exe),str(pcm)],capture_output=True,text=True,timeout=90)
            log+='RUN\n'+result.stdout+result.stderr;code=result.returncode
        log+=f'\nexit={code}\n'
        (output/f'{"GREEN" if has_batch else "RED"}-batch-{optimization[1:]}.log').write_text(log,encoding='utf-8')
        print(log,flush=True)
        if code:return code
    return subprocess.run([sys.executable,"-B",str(root/'Tests/SoundDetector/reference_check.py'),
                           '--output-dir',str(output)],cwd=root,check=False,timeout=90).returncode


if __name__ == "__main__":
    sys.exit(main())
