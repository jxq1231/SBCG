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
#include <algorithm>
namespace touchgfx {
struct Rect {int16_t x=0,y=0,width=0,height=0; Rect()=default;Rect(int16_t a,int16_t b,int16_t c,int16_t d):x(a),y(b),width(c),height(d){}
bool isEmpty()const{return width<=0||height<=0;}
Rect operator&(const Rect&r)const{int16_t a=std::max(x,r.x),b=std::max(y,r.y);return Rect(a,b,std::max(0,std::min(x+width,r.x+r.width)-a),std::max(0,std::min(y+height,r.y+r.height)-b));}
void operator&=(const Rect&r){*this=*this&r;} };
struct ClickEvent { enum ClickEventType{PRESSED,RELEASED,CANCEL}; ClickEventType type; int16_t x,y;
ClickEvent(ClickEventType t,int16_t a,int16_t b):type(t),x(a),y(b){} ClickEventType getType()const{return type;} int16_t getX()const{return x;} int16_t getY()const{return y;} };
struct DragEvent { enum DragEventType{DRAGGED}; int16_t x,y; DragEvent(DragEventType,int16_t,int16_t,int16_t a,int16_t b):x(a),y(b){} int16_t getNewX()const{return x;} int16_t getNewY()const{return y;} };
struct TypedText { int id; explicit TypedText(int n):id(n){} };
struct Unicode { using UnicodeChar=uint16_t;
static uint16_t fromUTF8(const uint8_t*s,uint16_t*d,uint16_t n){uint16_t i=0;for(;i+1<n&&s[i];++i)d[i]=s[i];d[i]=0;return i;}};
struct Drawable { unsigned invalidations=0; bool visible=true; int16_t x=0,y=0,w=0,h=0; uint32_t color=0;
virtual ~Drawable()=default;
int16_t getWidth()const{return w;}int16_t getHeight()const{return h;} Rect getAbsoluteRect()const{return Rect(x,y,w,h);}void translateRectToAbsolute(Rect&r)const{r.x+=x;r.y+=y;}
void invalidate(){++invalidations;} void setVisible(bool v){visible=v;} bool isVisible()const{return visible;}
void setPosition(int16_t a,int16_t b,int16_t c,int16_t d){x=a;y=b;w=c;h=d;} void setColor(uint32_t c){color=c;} uint32_t getColor()const{return color;} };
struct Box:Drawable {};
struct Widget:Drawable {virtual void draw(const Rect&)const=0;virtual Rect getSolidRect()const=0;};
struct LCD {static unsigned fills;static Rect allowed;static bool check_clip;static int trace_min,trace_max;void fillRect(const Rect&r,unsigned color,uint8_t=255){if(r.width<=0||r.height<=0)return;++fills;if(color==0x2dd4bfU){trace_min=std::min(trace_min,static_cast<int>(r.y));trace_max=std::max(trace_max,r.y+r.height-1);}if(check_clip){if(r.x<allowed.x||r.y<allowed.y||r.x+r.width>allowed.x+allowed.width||r.y+r.height>allowed.y+allowed.height)__builtin_abort();}}};
struct HAL {static LCD&lcd(){static LCD lcd;return lcd;}};
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
    'touchgfx/widgets/Widget.hpp':'#pragma once\n#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>\n',
    'touchgfx/hal/HAL.hpp':'#pragma once\n#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>\n',
    'touchgfx/lcd/LCD.hpp':'#pragma once\n#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>\n',
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
#include "Calibration/calibration_control.h"
extern "C" {
#include "app_main.h"
}
unsigned touchgfx::LCD::fills=0; touchgfx::Rect touchgfx::LCD::allowed;bool touchgfx::LCD::check_clip=false;
int touchgfx::LCD::trace_min=32767,touchgfx::LCD::trace_max=-32768;
static WaveformSnapshot wave{};static ClapRangingSnapshot clap{};
static unsigned wave_reads=0;
extern "C" void App_GetClapSnapshot(ClapRangingSnapshot*out){*out=clap;}
static ModeSnapshot mode_state{};static unsigned mode_requests=0;static ModeKind mode_last=MODE_NONE;
extern "C" void App_GetModeSnapshot(ModeSnapshot*out){*out=mode_state;}
extern "C" bool App_ModeRequest(ModeKind mode){++mode_requests;mode_last=mode;mode_state.requested=mode;return true;}
extern "C" void WaveformService_GetSnapshot(WaveformSnapshot*out){*out=wave;++wave_reads;}
static UI_StatusText page{}; static uint32_t generation=1; static bool available=false;
static bool touch_valid=true;
static CalibrationControlSnapshot calibration{};
static unsigned calibration_requests=0;
static CalibrationAction requested_action=CALIBRATION_ACTION_NONE;
extern "C" void CalibrationControl_GetSnapshot(CalibrationControlSnapshot*out){*out=calibration;}
extern "C" bool CalibrationControl_Request(CalibrationAction a){
 ++calibration_requests;requested_action=a;return true;
}
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
 dumpWidget(page,"waveform",waveform);
 for(int i=0;i<5;++i)dumpWidget(page,"pageLine",pageLines[i],pageLines[i].font,pageLines[i].text);
 for(int i=0;i<3;++i){dumpWidget(page,"settingTile",settingTiles[i]);dumpWidget(page,"settingLabel",settingLabels[i],settingLabels[i].font,settingLabels[i].text);}
 dumpWidget(page,"navBackground",navigationBackground);
 for(int i=0;i<4;++i){dumpWidget(page,"navTile",navigationTiles[i]);dumpWidget(page,"navLabel",navigationLabels[i],navigationLabels[i].font,navigationLabels[i].text);}
}
bool titleIs(const char*t){return equals(pageTitleBuffer,t);}

bool lineIs(int i,const char*t){return equals(pageLineBuffers[i],t);}
unsigned wavePaints()const{return waveform.invalidations;}
void drawWave(touchgfx::Rect area){waveform.draw(area);}
unsigned settingInvalidations()const{unsigned n=0;for(int i=0;i<3;++i)n+=settingTiles[i].invalidations+settingLabels[i].invalidations;return n;}
};
static void tap(Screen1View&v,int x,int y){v.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,x,y));v.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,x,y));}
int main(){
 mode_state.role=BOARD_ROLE_A;mode_state.state=MODE_ACTIVE;mode_state.requested=MODE_RANGE;
 mode_state.effective=MODE_RANGE;mode_state.a_boot=0U;mode_state.b_boot=0U;mode_state.mode_seq=0U;
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
 // Enable mode tracking for page-request tests; STATUS text stays for zero boots.
 mode_state.a_boot=1U;mode_state.b_boot=2U;mode_state.mode_seq=1U;
 // Navigation must act on release; entering another page hides the ranging widgets.
 view.setupScreen();
 mode_state.requested=MODE_CLAP;mode_state.effective=MODE_CLAP;mode_requests=0U;
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,180,250));
 assert(view.measurePanel.isVisible());
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,180,250));
 assert(mode_requests==1U && mode_last==MODE_CLAP);
 assert(!view.measurePanel.isVisible());
 assert(view.titleIs("CLAP RANGING"));
 assert(view.lineIs(0,"WAIT / NO CLAP"));
 clap.has_value=clap.fresh=true;clap.result.distance_mm=1234;clap.result.direction=2;clap.result.quality=87;clap.source_age_ms=250;
 view.handleTickEvent();assert(view.lineIs(0,"FRESH / SINGLE CLAP"));
 assert(view.lineIs(1,"DISTANCE 1234 MM"));assert(view.lineIs(2,"SIDE B / QUALITY 87"));
 assert(equals(view.distanceTextBuffer,page.distance));
 clap.fresh=false;view.handleTickEvent();assert(view.lineIs(0,"HELD / LAST CLAP"));
 clap.has_value=false;view.handleTickEvent();assert(view.lineIs(1,"DISTANCE -- MM"));
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
 mode_state.requested=MODE_RANGE;mode_state.effective=MODE_RANGE;
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,60,250));
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,60,250));
 assert(view.measurePanel.isVisible());assert(equals(view.distanceTextBuffer,"0.325 M"));
 // Hidden WAVE never polls or invalidates; re-entering fetches the latest CPU frame.
 const unsigned hidden_reads=wave_reads;
 for(int i=0;i<20;++i)view.handleTickEvent();assert(wave_reads==hidden_reads);
 mode_state.requested=MODE_WAVE;mode_state.effective=MODE_WAVE;
 wave.valid=true;wave.frame_seq=1;wave.point_count=960;wave.window_span_ns=20000000;wave.source=WAVEFORM_SOURCE_LOCAL;
 for(int i=0;i<960;++i)wave.points[i]=(i%2)?32767:-32768;
 tap(view,300,250);assert(view.lineIs(0,"LOCAL / UNSYNCED"));
 const unsigned paints_wave=view.wavePaints();view.handleTickEvent();assert(view.wavePaints()==paints_wave);
 touchgfx::LCD::allowed=touchgfx::Rect(31,142,10,12);touchgfx::LCD::check_clip=true;
 view.drawWave(touchgfx::Rect(7,9,10,12));assert(touchgfx::LCD::fills>2);
 touchgfx::LCD::check_clip=false;
 ++wave.frame_seq;wave.synced=true;wave.source=WAVEFORM_SOURCE_COMMON;wave.period_valid=true;wave.period_milli_hz=1000000;
 view.handleTickEvent();assert(view.lineIs(0,"SYNC / COMMON WINDOW"));assert(view.wavePaints()==paints_wave+1);
 // A stale COMMON source does not confer synchronization after its lease expires.
 ++wave.frame_seq;wave.synced=false;view.handleTickEvent();
 assert(view.lineIs(0,"LOCAL / UNSYNCED"));
 ++wave.frame_seq;wave.synced=true;view.handleTickEvent();
 assert(view.lineIs(0,"SYNC / COMMON WINDOW"));
 // A weak real PCM signal must remain visible; one-PCM noise must not gain full height.
 for(int i=0;i<960;++i)wave.points[i]=(i%2)?100:-200;
 ++wave.frame_seq;view.handleTickEvent();
 touchgfx::LCD::trace_min=32767;touchgfx::LCD::trace_max=-32768;
 view.drawWave(touchgfx::Rect(0,0,432,78));
 assert(touchgfx::LCD::trace_max-touchgfx::LCD::trace_min>0);
 assert(wave.points[0]==-200 && wave.points[1]==100); // display gain never edits source PCM
 for(int i=0;i<960;++i)wave.points[i]=(i%2)?1:-1;
 ++wave.frame_seq;view.handleTickEvent();
 touchgfx::LCD::trace_min=32767;touchgfx::LCD::trace_max=-32768;
 view.drawWave(touchgfx::Rect(0,0,432,78));
 assert(touchgfx::LCD::trace_max-touchgfx::LCD::trace_min<=1);
 ++wave.frame_seq;wave.valid=false;view.handleTickEvent();assert(view.lineIs(0,"NO DATA / NO AUDIO"));
 touchgfx::LCD::fills=0;view.drawWave(touchgfx::Rect(0,0,432,78));assert(touchgfx::LCD::fills==3);
 mode_state.requested=MODE_RANGE;mode_state.effective=MODE_RANGE;
 tap(view,60,250);++wave.frame_seq;wave.valid=true;wave.synced=false;wave.source=WAVEFORM_SOURCE_LOCAL;wave.period_valid=false;
 const unsigned hidden_paints=view.wavePaints();view.handleTickEvent();assert(view.wavePaints()==hidden_paints);
 mode_state.requested=MODE_WAVE;mode_state.effective=MODE_WAVE;
 tap(view,300,250);assert(view.lineIs(0,"LOCAL / UNSYNCED"));assert(view.wavePaints()==hidden_paints+1);
 tap(view,60,250);
 // Gaps/outside navigation do not activate a page.
 for(int x:{-1,0,119,120,239,359,479,480}){
  view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,x,250));
  view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,x,250));
  assert(view.measurePanel.isVisible());
 }
 ++generation;std::strcpy(page.role,"BOARD A");calibration.authority=true;view.handleTickEvent();
 clap.has_value=true;clap.fresh=false;clap.result.distance_mm=10000;clap.source_age_ms=6499;
 wave.synced=true;wave.source=WAVEFORM_SOURCE_COMMON;wave.period_valid=true;wave.period_milli_hz=1000000;++wave.frame_seq;
 for(int i=0;i<4;++i){
  view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,60+120*i,250));
  view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,60+120*i,250));
  view.dump(i);
 }
 // A can start an empty acquisition, and a held touch never changes its action across phases.
 calibration.authority=true;
 mode_state.role=BOARD_ROLE_A;mode_state.requested=MODE_CALIBRATION;mode_state.effective=MODE_CALIBRATION;
 view.handleTickEvent();
 tap(view,220,202);tap(view,370,202);assert(calibration_requests==0); // no saved coefficients
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,70,202));
 view.handleDragEvent(touchgfx::DragEvent(touchgfx::DragEvent::DRAGGED,70,202,70,170));
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,70,202));
 assert(calibration_requests==0);
 tap(view,70,202);
 assert(calibration_requests==1 && requested_action==CALIBRATION_ACTION_START);

 calibration.phase=CALIBRATION_PHASE_WAIT_SOUND;
 calibration.result=CALIBRATION_CONTROL_LISTENING;calibration.remaining_ms=14950;++calibration.completed;
 view.handleTickEvent();
 assert(view.lineIs(0,"PLAY CALIBRATION SOUND"));assert(view.lineIs(1,"TIME LEFT 15 S"));
 view.dump(4);
 calibration.heard_chirp=true;view.handleTickEvent();
 assert(view.lineIs(2,"CHIRP DETECTED / WAITING FOR RANGE"));
 view.dump(9);
 tap(view,70,202);tap(view,370,202);assert(calibration_requests==1);
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,220,202));
 calibration.phase=CALIBRATION_PHASE_WAIT_CONFIRM;
 calibration.result=CALIBRATION_CONTROL_CANDIDATE;calibration.remaining_ms=20000;++calibration.completed;
 calibration.candidate={501,20,30,7,2,1000000000U,999000000U};
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,220,202));
 assert(calibration_requests==1); // CANCEL was pressed in the previous phase.
 assert(view.lineIs(1,"RAW 501 MM / REF 500 MM"));
 assert(view.lineIs(2,"OFFSET -1 MM / 20 S"));
 view.dump(7);
 calibration.remaining_ms=10000;view.handleTickEvent();
 assert(view.lineIs(1,"RAW 501 MM / REF 500 MM"));
 assert(view.lineIs(2,"OFFSET -1 MM / 10 S"));
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,70,202));
 ++calibration.completed; // A replacement round can reach the same phase between UI ticks.
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,70,202));
 assert(calibration_requests==1);
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,70,202));
 touch_valid=false;
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,70,202));
 assert(calibration_requests==1);touch_valid=true;
 tap(view,70,202);
 assert(calibration_requests==2 && requested_action==CALIBRATION_ACTION_CONFIRM);

 calibration.phase=CALIBRATION_PHASE_IDLE;
 calibration.result=CALIBRATION_CONTROL_APPLIED;++calibration.completed;
 calibration.calibration.calibrated=true;calibration.calibration.enabled=true;
 calibration.calibration.offset_mm=-1;calibration.calibration.version=1;
 calibration.applied_offset_mm=-1;calibration.applied_version=1;
 view.handleTickEvent();
 view.dump(5);
 tap(view,220,202);tap(view,220,202);assert(calibration_requests==2); // local CANCEL
 tap(view,220,202);tap(view,70,202);
 assert(calibration_requests==3 && requested_action==CALIBRATION_ACTION_DISABLE);
 calibration.calibration.enabled=false;calibration.applied_offset_mm=0;++calibration.applied_version;
 view.handleTickEvent();tap(view,220,202);tap(view,70,202);
 assert(calibration_requests==4 && requested_action==CALIBRATION_ACTION_ENABLE);
 tap(view,370,202);tap(view,60,250);tap(view,420,250); // leaving cancels local confirmation
 assert(calibration_requests==4);
 tap(view,370,202);tap(view,70,202);
 assert(calibration_requests==5 && requested_action==CALIBRATION_ACTION_CLEAR);

 calibration.phase=CALIBRATION_PHASE_WAIT_SOUND;
 calibration.result=CALIBRATION_CONTROL_LISTENING;++calibration.completed;
 view.handleTickEvent();tap(view,220,202);
 assert(calibration_requests==6 && requested_action==CALIBRATION_ACTION_CANCEL);
 calibration.phase=CALIBRATION_PHASE_WAIT_CONFIRM;
 calibration.result=CALIBRATION_CONTROL_CANDIDATE;++calibration.completed;
 view.handleTickEvent();tap(view,220,202);
 assert(calibration_requests==7 && requested_action==CALIBRATION_ACTION_CANCEL);
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::PRESSED,70,202));
 calibration.phase=CALIBRATION_PHASE_IDLE;
 calibration.result=CALIBRATION_CONTROL_CONFIRM_TIMEOUT;++calibration.completed;
 view.handleClickEvent(touchgfx::ClickEvent(touchgfx::ClickEvent::RELEASED,70,202));
 assert(calibration_requests==7); // A held CONFIRM cannot turn into START.
 assert(view.lineIs(3,"CONFIRM TIMEOUT / RETRY"));
 view.dump(8);
 calibration.result=CALIBRATION_CONTROL_NO_SOUND;++calibration.completed;view.handleTickEvent();
 assert(view.lineIs(3,"NO CAL SOUND / RETRY"));
 view.dump(10);
 calibration.result=CALIBRATION_CONTROL_NO_QUALIFIED_SAMPLE;++calibration.completed;view.handleTickEvent();
 assert(view.lineIs(3,"CHIRP / NO VALID RANGE / RETRY"));
 view.dump(11);

 calibration.authority=false;view.handleTickEvent();
 ++generation;std::strcpy(page.role,"BOARD B");view.handleTickEvent();
 view.dump(6);
 for(int x:{70,220,370}){tap(view,x,202);tap(view,70,202);}
 assert(calibration_requests==7); // B cannot submit even while holding saved-state fixtures.
 const unsigned paints=view.settingInvalidations();
 for(int i=0;i<20;++i)view.handleTickEvent();
 assert(view.settingInvalidations()==paints); // unchanged settings must not force VSYNC redraws
 // Phase 5: B only browses; A sends one request per valid release; pending never shows stale data.
 mode_state.role=BOARD_ROLE_B;mode_state.requested=MODE_RANGE;mode_state.effective=MODE_RANGE;mode_requests=0;
 tap(view,300,250);
 assert(mode_requests==0U && view.titleIs("MICROPHONE WAVEFORM"));
 assert(view.lineIs(0,"MODE NOT ACTIVE"));
 assert(equals(view.roleTextBuffer,"B VW:WAV EF:RNG"));
 mode_state.role=BOARD_ROLE_A;mode_requests=0;
 tap(view,300,250);assert(mode_requests==1U && mode_last==MODE_WAVE);
 mode_state.requested=MODE_WAVE;mode_state.effective=MODE_RANGE;view.handleTickEvent();
 assert(equals(view.roleTextBuffer,"A RQ:WAV+ EF:RNG"));
 assert(view.lineIs(0,"MODE NOT ACTIVE"));
 mode_state.effective=MODE_WAVE;view.handleTickEvent();
 assert(equals(view.roleTextBuffer,"A RQ:WAV EF:WAV"));
 mode_state.requested=MODE_RANGE;mode_state.effective=MODE_RANGE;mode_requests=0;
 tap(view,180,250);tap(view,60,250);tap(view,180,250);
 assert(mode_requests==3U && mode_last==MODE_CLAP && view.titleIs("CLAP RANGING"));
 assert(equals(view.roleTextBuffer,"A RQ:CLP+ EF:RNG"));
 assert(view.lineIs(0,"MODE NOT ACTIVE"));
 mode_state.requested=MODE_CLAP;mode_state.effective=MODE_CLAP;view.handleTickEvent();
 assert(equals(view.roleTextBuffer,"A RQ:CLP EF:CLP"));
 assert(view.lineIs(0,"HELD / LAST CLAP"));
}
'''
dump_calls=[]
for kind,name in re.findall(r'touchgfx::(Box|TextArea|TextAreaWithOneWildcard) (\w+);',widgets):
    args='' if kind=='Box' else f',{name}.font,{name}.text'
    dump_calls.append(f'dumpWidget(page,"{name}",{name}{args});')
test.write_text(test_source.replace('// DUMP_WIDGETS','\n'.join(dump_calls)),encoding='utf-8')
exe = out / 'view.exe'
subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-DSBCG_USE_MODE_OBSERVATION=1',
                '-I', str(out/'include'), '-I', str(root/'TouchGFX/gui/include'),
                '-I', str(root/'App'), str(test),
                str(root/'TouchGFX/gui/src/screen1_screen/Screen1View.cpp'),
                '-o', str(exe)], check=True, timeout=30)
result=subprocess.run([str(exe)], check=True, timeout=30,capture_output=True,text=True)
layout=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
(out/'navigation-layout.json').write_text(json.dumps(layout,indent=2),encoding='utf-8')
print('PASS View: 13 bindings, CLAP fresh/hold/withdraw, WAVE local/sync/wait/clip/version/hidden, navigation and calibration')
