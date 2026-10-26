"""Run real detector service against synthetic PCM, without board access.

包含原逐帧服务测试和一个64点批量迁移的逐字段等价测试；-finstrument-functions
只在批量测试中统计检测入口调用，用于证明8192帧Stage走的是批量入口。
"""
from pathlib import Path
import shutil,subprocess,sys

def main():
 root=Path(__file__).resolve().parents[2];out=root/'Logs/Detector/M4_2_4_B';out.mkdir(parents=True,exist_ok=True)
 source=root/'App/Detector/sound_detector_service.c';phase='GREEN' if source.exists() else 'RED'
 if not source.exists():
  source=out/'missing_service.c'
  source.write_text('''#include "Detector/sound_detector_service.h"
const SoundDetectorConfig SoundDetectorService_DefaultConfig={64,256,128,2,8,24000};
bool SoundDetectorService_Init(uint32_t n,const SoundDetectorConfig*c,uint8_t h,void(*l)(const char*,int)){(void)n;(void)c;(void)h;(void)l;return false;}
bool SoundDetectorService_Stage(uint32_t n,const BSP_AudioBlock*b){(void)n;(void)b;return false;}
bool SoundDetectorService_Complete(bool a){(void)a;return false;}
void SoundDetectorService_Process(uint32_t n,const BSP_AudioSnapshot*s){(void)n;(void)s;}
void SoundDetectorService_GetSnapshot(SoundDetectorServiceSnapshot*s){if(s)*s=(SoundDetectorServiceSnapshot){0};}
''',encoding='utf-8')
 exe=out/'test_sound_service.exe'
 command=[shutil.which('gcc'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-I',str(root/'App'),str(root/'Tests/SoundDetectorService/test_service.c'),str(source),str(root/'App/Detector/sound_detector.c'),str(root/'App/Detector/toa_estimator.c'),'-o',str(exe)]
 p=subprocess.run(command,capture_output=True,text=True,timeout=30);log=p.stdout+p.stderr
 if not p.returncode:
  p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);log+=p.stdout+p.stderr
 (out/f'service-{phase}.log').write_text(log+f'\nexit={p.returncode}',encoding='utf-8');print(log)
 if p.returncode:return p.returncode
 # 64点批量迁移的逐字段对照；O0/O2各一遍，钩子只在Stage期间计数。
 for option in ('-O0','-O2'):
  exe=out/f'test_batch_service_{option[1:]}.exe'
  command=[shutil.which('gcc'),'-std=c11',option,'-Wall','-Wextra','-Werror','-pedantic',
           '-finstrument-functions','-I',str(root/'App'),
           str(root/'Tests/SoundDetectorService/test_batch_service.c'),str(source),
           str(root/'App/Detector/sound_detector.c'),str(root/'App/Detector/toa_estimator.c'),'-o',str(exe)]
  p=subprocess.run(command,capture_output=True,text=True,timeout=30);log=p.stdout+p.stderr
  if not p.returncode:
   p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);log+=p.stdout+p.stderr
  (out/f'batch-{phase}-{option[1:]}.log').write_text(log+f'\nexit={p.returncode}',encoding='utf-8');print(log)
  if p.returncode:return p.returncode
 return 0
if __name__=='__main__':sys.exit(main())
