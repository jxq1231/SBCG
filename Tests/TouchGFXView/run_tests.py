"""Native View binding test; real user C++ against a minimal generated-widget seam."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'Logs/TouchGFX/U3/tests/view'
stub = out / 'include/gui_generated/screen1_screen'
stub.mkdir(parents=True, exist_ok=True)
(out / 'include/gui/screen1_screen').mkdir(parents=True, exist_ok=True)
(out / 'include/gui/screen1_screen/Screen1Presenter.hpp').write_text('#pragma once\n', encoding='utf-8')
names = ('roleText', 'distanceText', 'directionText', 'qualityText', 'syncText',
         'offsetText', 'networkText', 'sourceText', 'temperatureText', 'speedText',
         'audioText', 'detailText', 'eventsText')
base = '''#pragma once
#include <cstdint>
#include <cstddef>
namespace touchgfx {
struct Unicode { using UnicodeChar=uint16_t;
static uint16_t fromUTF8(const uint8_t*s,uint16_t*d,uint16_t n){uint16_t i=0;for(;i+1<n&&s[i];++i)d[i]=s[i];d[i]=0;return i;}};
struct TextAreaWithOneWildcard { unsigned invalidations=0; void invalidate(){++invalidations;} };
}
class Screen1ViewBase { public: virtual ~Screen1ViewBase()=default;
virtual void setupScreen(){} virtual void tearDownScreen(){} virtual void handleTickEvent(){}
'''
for name in names:
    base += f'touchgfx::TextAreaWithOneWildcard {name}; touchgfx::Unicode::UnicodeChar {name}Buffer[40]={{}};\n'
base += '};\n'
(stub / 'Screen1ViewBase.hpp').write_text(base, encoding='utf-8')
test = out / 'test_view.cpp'
test.write_text('''#include <cassert>
#include <cstring>
#include <gui/screen1_screen/Screen1View.hpp>
#include "UI/touchgfx_service.h"
static UI_StatusText page{}; static uint32_t generation=1; static bool available=false;
extern "C" bool UI_TouchGFX_GetText(UI_StatusText*out,uint32_t*g){if(!available)return false;*out=page;*g=generation;return true;}
static bool equals(const uint16_t*b,const char*s){while(*s&&*b==static_cast<unsigned char>(*s)){++b;++s;}return !*b&&!*s;}
int main(){
 Screen1View view; view.setupScreen(); assert(view.roleText.invalidations==0);
 available=true; std::strcpy(page.role,"BOARD A");std::strcpy(page.distance,"-- M");
 std::strcpy(page.direction,"SIDE --");std::strcpy(page.quality,"QUALITY --");
 std::strcpy(page.sync,"LOCAL REFERENCE");std::strcpy(page.offset,"-- US");
 std::strcpy(page.network,"NO LINK");std::strcpy(page.temperature_source,"MANUAL");
 std::strcpy(page.temperature,"25.0 C");std::strcpy(page.speed,"346.4 M/S");
 std::strcpy(page.audio,"RUN");std::strcpy(page.audio_detail,"DROP 0 ERR 0");std::strcpy(page.counts,"EVENTS 12");
 view.handleTickEvent();
 assert(equals(view.roleTextBuffer,page.role));assert(equals(view.distanceTextBuffer,page.distance));
 assert(equals(view.directionTextBuffer,page.direction));assert(equals(view.qualityTextBuffer,page.quality));
 assert(equals(view.syncTextBuffer,page.sync));assert(equals(view.offsetTextBuffer,page.offset));
 assert(equals(view.networkTextBuffer,page.network));assert(equals(view.sourceTextBuffer,page.temperature_source));
 assert(equals(view.temperatureTextBuffer,page.temperature));assert(equals(view.speedTextBuffer,page.speed));
 assert(equals(view.audioTextBuffer,page.audio));assert(equals(view.detailTextBuffer,page.audio_detail));assert(equals(view.eventsTextBuffer,page.counts));
 assert(view.roleText.invalidations==1);view.handleTickEvent();assert(view.roleText.invalidations==1);
 ++generation;view.handleTickEvent();assert(view.roleText.invalidations==1);
 ++generation;std::strcpy(page.role,"BOARD B");view.handleTickEvent();assert(equals(view.roleTextBuffer,"BOARD B"));assert(view.roleText.invalidations==2);
 assert(view.distanceText.invalidations==1);
 available=false;++generation;view.handleTickEvent();assert(view.roleText.invalidations==2);
 view.tearDownScreen();
}
''', encoding='utf-8')
exe = out / 'view.exe'
subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I', str(out/'include'), '-I', str(root/'TouchGFX/gui/include'),
                '-I', str(root/'App'), str(test),
                str(root/'TouchGFX/gui/src/screen1_screen/Screen1View.cpp'),
                '-o', str(exe)], check=True, timeout=30)
subprocess.run([str(exe)], check=True, timeout=30)
print('PASS View: all 13 bindings, missing data, generation/change invalidation, placeholders')
