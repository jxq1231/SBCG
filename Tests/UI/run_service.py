"""Native real status scheduler/renderer with read-only business and LCD fixtures."""
from pathlib import Path
import subprocess,shutil
root=Path(__file__).resolve().parents[2];out=root/'Logs/UI/U2/tests';out.mkdir(parents=True,exist_ok=True)
service=root/'App/UI/status_service.c'
if not service.exists():
    service=out/'missing_service.c'
    service.write_text('#include "UI/status_service.h"\nvoid UI_StatusService_Init(uint32_t n,void(*l)(const char*,int)){(void)n;(void)l;}\nvoid UI_StatusService_Process(uint32_t n,bool a){(void)n;(void)a;}\nvoid UI_StatusService_GetSnapshot(UI_StatusServiceSnapshot*s){if(s)*s=(UI_StatusServiceSnapshot){0};}\n',encoding='utf-8')
sources=[root/'Tests/UI/test_service.c',service,*[root/f'App/UI/{x}.c' for x in ('status_view','status_snapshot','rgb565_canvas','status_screen')]]
exe=out/'service.exe'
subprocess.run([shutil.which('gcc'),'-std=c11','-Wall','-Wextra','-Werror','-pedantic','-I',str(root/'App'),*map(str,sources),'-o',str(exe)],check=True,timeout=30)
for test in ('basic','gate','stale','initfail','fail','b','irq'):
    subprocess.run([str(exe),test],check=True,timeout=30)
