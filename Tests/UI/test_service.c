#include "UI/status_service.h"
#include "UI/status_screen.h"
#include "BSP/bsp_lcd.h"
#include "BSP/bsp_board_identity.h"
#include "BSP/bsp_audio.h"
#include "Network/ethernet_monitor.h"
#include "Sync/time_sync.h"
#include "Temperature/temperature_service.h"
#include "Detector/sound_detector_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t pixels[480*272];
static BoardIdentityProfile identity;
static BSP_LcdSnapshot lcd;
static TemperatureSnapshot temp;
static TimeSyncSnapshot sync_state;
static unsigned begin_calls,present_calls,poll_calls,capture_calls,cancels,logs;
static bool init_ok=true,lease,present_ok=true,link_up,irq_advance;
static uint32_t tick;
bool BSP_LCD_Init(void){if(init_ok)lcd.status=BSP_LCD_READY;return init_ok;}
uint16_t *BSP_LCD_BeginFrame(void){++begin_calls;if(lease||lcd.pending_address)return NULL;lease=true;return pixels;}
bool BSP_LCD_Present(uint32_t n){assert(n==tick&&lease);++present_calls;lease=false;if(!present_ok){lcd.status=BSP_LCD_ERROR;return false;}if(!lcd.presented_frames){lcd.status=BSP_LCD_RUNNING;++lcd.presented_frames;}else lcd.pending_address=1;return true;}
void BSP_LCD_Process(uint32_t n){assert(n==tick);++poll_calls;}
void BSP_LCD_CancelFrame(void){lease=false;++cancels;}
void BSP_LCD_GetSnapshot(BSP_LcdSnapshot*out){*out=lcd;}
const BoardIdentityProfile *BSP_BoardIdentity_Get(void){++capture_calls;return &identity;}
void EthernetMonitor_GetSnapshot(uint32_t n,EthernetMonitorSnapshot*out){assert(n==tick);*out=(EthernetMonitorSnapshot){true,link_up,0};}
void TimeSync_GetSnapshot(uint32_t n,TimeSyncSnapshot*out){assert(n==tick);*out=sync_state;}
void TemperatureService_GetSnapshot(uint32_t n,TemperatureSnapshot*out){assert(n==tick);*out=temp;}
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot*out){*out=(BSP_AudioSnapshot){.running=true,.session=1,.last_callback_ms=tick+(irq_advance?1U:0U),.callback_count=1};}
uint32_t BSP_Console_GetTickMs(void){return tick+(irq_advance?1U:0U);}
void SoundDetectorService_GetSnapshot(SoundDetectorServiceSnapshot*out){*out=(SoundDetectorServiceSnapshot){.configured=true,.candidates=7};}
static void Log(const char*s,int n){assert(n>0&&n<256&&s[n-2]=='\r'&&s[n-1]=='\n');++logs;}
static void Step(uint32_t n,bool allow){tick=n;UI_StatusService_Process(n,allow);}
static void Finish(uint32_t first){for(unsigned i=0;i<34;++i)Step(first+i,true);}
int main(int argc,char **argv)
{
 assert(argc==2);identity.role=BOARD_ROLE_A;sync_state.configured=true;sync_state.role=BOARD_ROLE_A;
 const char *test=argv[1];
 irq_advance=!strcmp(test,"irq");
 if(!strcmp(test,"initfail"))init_ok=false;
 if(!strcmp(test,"b")){identity.role=BOARD_ROLE_B;sync_state.role=BOARD_ROLE_B;sync_state.state=CLOCK_SERVO_ACQUIRING;}
 UI_StatusService_Init(0,Log);UI_StatusServiceSnapshot s;UI_StatusService_GetSnapshot(&s);
 if(!strcmp(test,"initfail")){Step(0,true);assert(!s.initialized&&begin_calls==0&&present_calls==0);return 0;}
 assert(s.initialized);
 if(!strcmp(test,"gate")){Step(100,false);assert(!poll_calls&&!capture_calls&&!begin_calls&&!present_calls);return 0;}
 if(!strcmp(test,"stale")){Step(0,true);Step(2000,true);UI_StatusService_GetSnapshot(&s);assert(cancels==1&&s.discarded_frames==1&&!present_calls);return 0;}
 if(!strcmp(test,"fail")){present_ok=false;Finish(0);assert(lcd.status==BSP_LCD_ERROR&&present_calls==1);unsigned old=begin_calls;Step(1000,true);assert(begin_calls==old);return 0;}
 temp=(TemperatureSnapshot){.manual=true,.valid=true,.temperature_milli_c=25000,.sound_speed_mm_s=346450};
 Step(0,true);UI_StatusService_GetSnapshot(&s);assert(s.rendered_rows==8&&s.rendering&&s.page.temperature_milli_c==25000);
 temp.temperature_milli_c=30000;
 for(unsigned i=1;i<34;++i){Step(i,true);UI_StatusService_GetSnapshot(&s);assert(s.page.temperature_milli_c==25000);}
 assert(present_calls==1&&capture_calls==1);
 if(!strcmp(test,"b")){assert(s.page.sync==UI_SYNC_ACQUIRING&&!s.page.offset_valid);return 0;}
 assert(s.page.role==UI_ROLE_A&&s.page.sync==UI_SYNC_LOCAL&&!s.page.offset_valid);
 assert(!s.page.measurement_valid&&s.page.candidates==7&&s.page.audio_valid);
 UI_Canvas c;static uint16_t reference[480*272];assert(UI_CanvasInit(&c,reference,480*272,480,272,480));
 assert(UI_StatusRenderRows(&c,&s.page,0,0,272));assert(!memcmp(reference,pixels,sizeof pixels));
 Step(999,true);assert(present_calls==1&&capture_calls==1);
 Finish(1000);assert(present_calls==2&&lcd.pending_address&&capture_calls==2);
 unsigned old_begin=begin_calls;Step(2100,true);assert(begin_calls==old_begin);
 lcd.pending_address=0;++lcd.presented_frames;Step(2101,true);
 UI_StatusService_GetSnapshot(&s);assert(s.page.temperature_milli_c==30000);
 assert(logs>0);puts("PASS status service single snapshot, eight-row slices, DHT gate, fresh publish and pending ownership");return 0;
}
