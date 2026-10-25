"""有界搜索主机测试；不烧录或连接硬件。"""
from pathlib import Path
import shutil
import subprocess
import sys
import wave
import tempfile

ROOT=Path(__file__).resolve().parents[2]
OUTPUT=ROOT/'Logs/Detector/M4_2_3_B'


def main():
    OUTPUT.mkdir(parents=True,exist_ok=True)
    output=Path(tempfile.mkdtemp(prefix='native-',dir=OUTPUT))
    print('TEST_OUTPUT',output,flush=True)
    source=ROOT/'App/Detector/chirp_service.c'
    phase='GREEN' if source.exists() else 'RED'
    if not source.exists():
        source=output/'missing_service.c'
        source.write_text('''#include "Detector/chirp_service.h"
#include <stddef.h>
bool ChirpService_Init(uint32_t t,uint8_t c,void(*l)(const char*,int)){(void)t;(void)c;(void)l;return false;}
bool ChirpService_Stage(uint32_t t,const BSP_AudioBlock*b){(void)t;(void)b;return false;}
bool ChirpService_Complete(bool a){(void)a;return false;}
void ChirpService_Process(uint32_t t,const BSP_AudioSnapshot*s){(void)t;(void)s;}
void ChirpService_GetSnapshot(ChirpServiceSnapshot*s){if(s)*s=(ChirpServiceSnapshot){0};}
''',encoding='utf-8')
    extra=['App/Detector/chirp_template.c']
    if phase=='GREEN':
        extra+=['App/Detector/chirp_window.c','App/Detector/chirp_matcher.c','App/Detector/sound_detector.c']
    for option in ['-O0','-O2']:
        exe=output/f'service_{option[1:]}.exe'
        cmd=[shutil.which('gcc'),'-std=c11',option,'-Wall','-Wextra','-Werror','-pedantic',
             '-I',str(ROOT/'App'),str(ROOT/'Tests/ChirpService/test_service.c'),str(source),
             *[str(ROOT/p) for p in extra],'-o',str(exe),'-lm']
        built=subprocess.run(cmd,text=True,capture_output=True,timeout=30)
        text='BUILD\n'+built.stdout+built.stderr
        code=built.returncode
        if code==0:
            run=subprocess.run([str(exe)],text=True,capture_output=True,timeout=30)
            text+='RUN\n'+run.stdout+run.stderr;code=run.returncode
        (output/f'{phase}-{option[1:]}.txt').write_text(text,encoding='utf-8')
        print(text,flush=True)
        if code:return code
        if phase=='GREEN':
            batch=output/f'scan_batch_{option[1:]}.exe'
            cmd=[shutil.which('gcc'),'-std=c11',option,'-Wall','-Wextra','-Werror','-pedantic',
                 '-finstrument-functions','-I',str(ROOT/'App'),
                 str(ROOT/'Tests/ChirpService/test_scan_batch.c'),str(source),
                 *[str(ROOT/p) for p in extra],'-o',str(batch),'-lm']
            built=subprocess.run(cmd,text=True,capture_output=True,timeout=30)
            text='BUILD\n'+built.stdout+built.stderr;code=built.returncode
            if code==0:
                run=subprocess.run([str(batch)],text=True,capture_output=True,timeout=30)
                text+='RUN\n'+run.stdout+run.stderr;code=run.returncode
            (output/f'scan-batch-{option[1:]}.txt').write_text(text,encoding='utf-8')
            print(text,flush=True)
            if code:return code
            capture=output/f'capture_{option[1:]}.exe'
            sources=['Tests/ChirpService/test_capture.c','App/Audio/audio_capture.c',
                     'App/Audio/audio_statistics.c','App/Detector/sound_detector_service.c']
            cmd=[shutil.which('gcc'),'-std=c11',option,'-Wall','-Wextra','-Werror','-pedantic',
                 '-DSBCG_USE_SOUND_DETECTOR=1','-DSBCG_USE_CHIRP=1','-I',str(ROOT/'App'),
                 *[str(ROOT/p) for p in sources],str(source),*[str(ROOT/p) for p in extra],
                 '-o',str(capture),'-lm']
            built=subprocess.run(cmd,text=True,capture_output=True,timeout=30)
            if built.returncode:
                print(built.stdout+built.stderr);return built.returncode
            with wave.open(str(ROOT/'Data/Audio/standard_chirp_2-6khz_10x_20s.wav'),'rb') as wav:
                raw=wav.readframes(wav.getnframes())
            data=output/'standard_chirp.pcm';data.write_bytes(raw)
            for calls in ['64','5']:
                run=subprocess.run([str(capture),str(data),calls],text=True,capture_output=True,timeout=60)
                report=run.stdout+run.stderr
                (output/f'capture-{option[1:]}-{calls}.txt').write_text(report,encoding='utf-8')
                print(report,flush=True)
                if run.returncode:return run.returncode
    return subprocess.run([sys.executable,'-B',str(ROOT/'Tests/ChirpService/run_diagnostics_tests.py')],
                          cwd=ROOT,check=False).returncode


if __name__=='__main__':
    sys.exit(main())
