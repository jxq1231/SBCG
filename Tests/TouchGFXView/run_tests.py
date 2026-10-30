"""Native View binding test; real user C++ against a minimal generated-widget seam."""
from pathlib import Path
import shutil
import subprocess
import re
import json

root = Path(__file__).resolve().parents[2]
out = root / 'Logs/TouchGFX/U3/tests/view'
stub = out / 'include/gui_generated/screen1_screen'
stub.mkdir(parents=True, exist_ok=True)
(out / 'include/gui/screen1_screen').mkdir(parents=True, exist_ok=True)
(out / 'include/gui/screen1_screen/Screen1Presenter.hpp').write_text('#pragma once\n', encoding='utf-8')
names = ('roleText', 'distanceText', 'directionText', 'qualityText', 'syncText',
         'offsetText', 'networkText', 'sourceText', 'temperatureText', 'speedText',
         'audioText', 'detailText', 'eventsText')
widgets = (root/'TouchGFX/generated/gui_generated/include/gui_generated/screen1_screen/Screen1ViewBase.hpp').read_text()
base = '''#pragma once
#include <cstdint>
#include <cstddef>
namespace touchgfx {
struct ClickEvent { enum ClickEventType{PRESSED,RELEASED,CANCEL}; ClickEventType type; int16_t x,y;
ClickEvent(ClickEventType t,int16_t a,int16_t b):type(t),x(a),y(b){} ClickEventType getType()const{return type;} int16_t getX()const{return x;} int16_t getY()const{return y;} };
struct DragEvent { enum DragEventType{DRAGGED}; int16_t x,y; DragEvent(DragEventType,int16_t,int16_t,int16_t a,int16_t b):x(a),y(b){} int16_t getNewX()const{return x;} int16_t getNewY()const{return y;} };
struct TypedText { int id; explicit TypedText(int n):id(n){} };
struct Unicode { using UnicodeChar=uint16_t;
static uint16_t fromUTF8(const uint8_t*s,uint16_t*d,uint16_t n){uint16_t i=0;for(;i+1<n&&s[i];++i)d[i]=s[i];d[i]=0;return i;}};
struct Drawable { unsigned invalidations=0; bool visible=true; int16_t x=0,y=0,w=0,h=0; uint32_t color=0;
void invalidate(){++invalidations;} void setVisible(bool v){visible=v;} bool isVisible()const{return visible;}
void setPosition(int16_t a,int16_t b,int16_t c,int16_t d){x=a;y=b;w=c;h=d;} void setColor(uint32_t c){color=c;} };
struct Box:Drawable {};
struct TextArea:Drawable {int font=-1; const uint16_t* text=nullptr; void setTypedText(TypedText t){font=t.id;} void setLinespacing(int){} };
struct TextAreaWithOneWildcard:TextArea {void setWildcard(const uint16_t*t){text=t;} };
}
class Screen1ViewBase { public: virtual ~Screen1ViewBase()=default;
virtual void setupScreen(){} virtual void tearDownScreen(){} virtual void handleTickEvent(){}
virtual void handleClickEvent(const touchgfx::ClickEvent&){} virtual void handleDragEvent(const touchgfx::DragEvent&){}
void add(touchgfx::Drawable&){} void invalidate(){}
'''
for kind,name in re.findall(r'touchgfx::(Box|TextArea|TextAreaWithOneWildcard) (\w+);',widgets):
    base += f'touchgfx::{kind} {name};\n'
for name in names:
    base += f'touchgfx::Unicode::UnicodeChar {name}Buffer[40]={{}};\n'
generated=(root/'TouchGFX/generated/gui_generated/src/screen1_screen/Screen1ViewBase.cpp').read_text()
keys=re.findall(r'^    (T_\w+),?$',(root/'TouchGFX/generated/texts/include/texts/TextKeysAndLanguages.hpp').read_text(),re.M)
base+='Screen1ViewBase(){\n'
for name,args in re.findall(r'(\w+)\.setPosition\(([^)]+)\);',generated):
    base+=f'{name}.setPosition({args});\n'
for name,key in re.findall(r'(\w+)\.setTypedText\(touchgfx::TypedText\((\w+)\)\);',generated):
    base+=f'{name}.setTypedText(touchgfx::TypedText({keys.index(key)}));\n'
for name,r,g,b in re.findall(r'(\w+)\.setColor\(touchgfx::Color::getColorFromRGB\((\d+), (\d+), (\d+)\)\);',generated):
    base+=f'{name}.setColor({int(r)*65536+int(g)*256+int(b)});\n'
for name in names:
    base+=f'{name}.setWildcard({name}Buffer);\n'
base+='}\n'
base += '};\n'
(stub / 'Screen1ViewBase.hpp').write_text(base, encoding='utf-8')
for path,source in {
    'touchgfx/Color.hpp':'#pragma once\nnamespace touchgfx {struct Color {static unsigned getColorFromRGB(unsigned r,unsigned g,unsigned b){return r*65536+g*256+b;}};}\n',
    'touchgfx/widgets/Box.hpp':'#pragma once\n#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>\n',
    'touchgfx/widgets/TextAreaWithWildcard.hpp':'#pragma once\n#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>\n',
    'touchgfx/events/ClickEvent.hpp':'#pragma once\n#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>\n',
    'touchgfx/events/DragEvent.hpp':'#pragma once\n#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>\n',
    'texts/TextKeysAndLanguages.hpp':(root/'TouchGFX/generated/texts/include/texts/TextKeysAndLanguages.hpp').read_text(),
    'BSP/bsp_touch.h':'#pragma once\n#include <stdbool.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\ntypedef struct {bool sample_valid;} BSP_TouchSnapshot;\nvoid BSP_Touch_GetSnapshot(BSP_TouchSnapshot*);\n#ifdef __cplusplus\n}\n#endif\n',
}.items():
    target=out/'include'/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(source,encoding='utf-8')
test = out / 'test_view.cpp'
test_source='''#include <cassert>
#include <cstring>
#include <initializer_list>
#include <cstdio>
#include <gui/screen1_screen/Screen1View.hpp>
#include "UI/touchgfx_service.h"
#include "BSP/bsp_touch.h"
static UI_StatusText page{}; static uint32_t generation=1; static bool available=false;
static bool touch_valid=true;
extern "C" void BSP_Touch_GetSnapshot(BSP_TouchSnapshot*out){out->sample_valid=touch_valid;}
extern "C" bool UI_TouchGFX_GetText(UI_StatusText*out,uint32_t*g){if(!available)return false;*out=page;*g=generation;return true;}
static bool equals(const uint16_t*b,const char*s){while(*s&&*b==static_cast<unsigned char>(*s)){++b;++s;}return !*b&&!*s;}
static void dumpWidget(int page,const char*name,const touchgfx::Drawable&w,int typed=-1,const uint16_t*text=nullptr){
 if(!w.isVisible())return;
 std::printf("{\\"page\\":%d,\\"name\\":\\"%s\\",\\"x\\":%d,\\"y\\":%d,\\"w\\":%d,\\"h\\":%d,\\"color\\":%u,\\"typed\\":%d,\\"text\\":",page,name,w.x,w.y,w.w,w.h,w.color,typed);
 if(text){std::putchar('"');for(;*text;++text)std::putchar(static_cast<char>(*text));std::putchar('"');}else std::printf("null");
 std::puts("}");
}
class InspectView:public Screen1View {public:
void dump(int page){
// DUMP_WIDGETS
 dumpWidget(page,"pagePanel",pagePanel);dumpWidget(page,"pageTitle",pageTitle,pageTitle.font,pageTitle.text);
 for(int i=0;i<5;++i)dumpWidget(page,"pageLine",pageLines[i],pageLines[i].font,pageLines[i].text);
 for(int i=0;i<3;++i){dumpWidget(page,"settingTile",settingTiles[i]);dumpWidget(page,"settingLabel",settingLabels[i],settingLabels[i].font,settingLabels[i].text);}
 dumpWidget(page,"navBackground",navigationBackground);
 for(int i=0;i<4;++i){dumpWidget(page,"navTile",navigationTiles[i]);dumpWidget(page,"navLabel",navigationLabels[i],navigationLabels[i].font,navigationLabels[i].text);}
}
bool titleIs(const char*t){return equals(pageTitleBuffer,t);}
};
int main(){
 InspectView view; view.setupScreen(); assert(view.roleText.invalidations==0);
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
 // Navigation must act on release; entering another page hides the ranging widgets.
 view.setupScreen();
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,180,250));
 assert(view.measurePanel.isVisible());
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,180,250));
 assert(!view.measurePanel.isVisible());
 assert(view.titleIs("CLAP RANGING"));
 // A held finger never reselects; invalid release and dragging out cancel.
 for(int i=0;i<20;++i)view.handleTickEvent();
 assert(!view.measurePanel.isVisible());
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,60,250));
 assert(!view.measurePanel.isVisible());
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,60,250));
 touch_valid=false;
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,60,250));
 assert(!view.measurePanel.isVisible());touch_valid=true;
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,60,250));
 view.handleDragEvent(touchgfx::DragEvent(touchgfx::DragEvent::DRAGGED,60,250,60,200));
 view.handleDragEvent(touchgfx::DragEvent(touchgfx::DragEvent::DRAGGED,60,200,60,250));
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,60,250));
 assert(!view.measurePanel.isVisible());
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,60,250));
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::CANCEL,60,250));
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,60,250));
 assert(!view.measurePanel.isVisible());
 // All pages keep pulling new real text. Disabled settings controls do nothing.
 for(int x:{300,420}){
  view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,x,250));
  view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,x,250));
  assert(!view.measurePanel.isVisible());
  assert(view.titleIs(x==300?"MICROPHONE WAVEFORM":"CALIBRATION SETTINGS"));
 }
 ++generation;available=true;std::strcpy(page.distance,"0.325 M");view.handleTickEvent();
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,70,195));
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,70,195));
 assert(!view.measurePanel.isVisible());
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,60,250));
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,60,250));
 assert(view.measurePanel.isVisible());assert(equals(view.distanceTextBuffer,"0.325 M"));
 // Gaps/outside navigation do not activate a page.
 for(int x:{-1,0,119,120,239,359,479,480}){
  view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,x,250));
  view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,x,250));
  assert(view.measurePanel.isVisible());
 }
 ++generation;std::strcpy(page.role,"BOARD A");view.handleTickEvent();
 for(int i=0;i<4;++i){
  view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,60+120*i,250));
  view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,60+120*i,250));
  view.dump(i);
 }
}
'''
dump_calls=[]
for kind,name in re.findall(r'touchgfx::(Box|TextArea|TextAreaWithOneWildcard) (\w+);',widgets):
    args='' if kind=='Box' else f',{name}.font,{name}.text'
    dump_calls.append(f'dumpWidget(page,"{name}",{name}{args});')
test.write_text(test_source.replace('// DUMP_WIDGETS','\n'.join(dump_calls)),encoding='utf-8')
exe = out / 'view.exe'
subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I', str(out/'include'), '-I', str(root/'TouchGFX/gui/include'),
                '-I', str(root/'App'), str(test),
                str(root/'TouchGFX/gui/src/screen1_screen/Screen1View.cpp'),
                '-o', str(exe)], check=True, timeout=30)
result=subprocess.run([str(exe)], check=True, timeout=30,capture_output=True,text=True)
layout=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
(out/'navigation-layout.json').write_text(json.dumps(layout,indent=2),encoding='utf-8')
print('PASS View: 13 bindings, four pages, release/hold/drag/cancel/fault, hidden updates, disabled actions')
