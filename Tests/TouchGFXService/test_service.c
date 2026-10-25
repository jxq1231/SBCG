/**
 * @file        test_service.c
 * @brief       验证真实TouchGFX前台服务的门控、快照、缓冲地址关联和错误终态。
 * @details     只替代业务出口/BSP/框架，使用真实服务和共享快照、格式化实现。
 * @note        各场景独立进程；不消费PCM或访问目标硬件。
 */
#include "UI/touchgfx_service.h"
#include "BSP/bsp_touchgfx_display.h"
#include "BSP/bsp_board_identity.h"
#include "BSP/bsp_audio.h"
#include "Network/ethernet_monitor.h"
#include "Sync/time_sync.h"
#include "Temperature/temperature_service.h"
#include "Detector/sound_detector_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
unsigned framework_inits, framework_calls, bsp_inits;
static unsigned polls, reveals, captures, logs;
static bool init_ok=true, irq_advance, framework_error;
static uint32_t tick;
static BoardIdentityProfile identity;
static BSP_TouchGFXDisplaySnapshot lcd;
static TemperatureSnapshot temp;
static TimeSyncSnapshot sync_state;
static BSP_AudioSnapshot audio;
/** @brief 返回注入的显示启动结果。@return 启动许可，不访问外存。 */
bool BSP_TouchGFXDisplay_Init(void){++bsp_inits;if(init_ok)lcd.status=BSP_TOUCHGFX_READY;return init_ok;}
/** @brief 记录前台轮询。@return 无返回值。 */
void BSP_TouchGFXDisplay_Process(void){++polls;}
/** @brief 模拟前台显示开启或非法地址锁存。@param address 字节地址。@return 合法地址为true。 */
bool BSP_TouchGFXDisplay_Reveal(uint32_t address){++reveals;if(address!=BSP_TOUCHGFX_FRAME_A&&address!=BSP_TOUCHGFX_FRAME_B){lcd.status=BSP_TOUCHGFX_ERROR;lcd.config_errors=1U;return false;}lcd.status=BSP_TOUCHGFX_RUNNING;lcd.front_address=address;return true;}
/** @brief 复制BSP桩状态。@param out 非NULL输出。@return 无返回值。 */
void BSP_TouchGFXDisplay_GetSnapshot(BSP_TouchGFXDisplaySnapshot*out){*out=lcd;}
/** @brief 记录业务捕获并提供静态身份。@return 全场景有效的只读身份地址。 */
const BoardIdentityProfile *BSP_BoardIdentity_Get(void){++captures;return &identity;}
/** @brief 提供网络状态。@param n 当前测试毫秒。@param out 非NULL输出。@return 无返回值。 */
void EthernetMonitor_GetSnapshot(uint32_t n,EthernetMonitorSnapshot*out){assert(n==tick);*out=(EthernetMonitorSnapshot){true,true,0};}
/** @brief 提供同步状态。@param n 当前测试毫秒。@param out 非NULL输出。@return 无返回值。 */
void TimeSync_GetSnapshot(uint32_t n,TimeSyncSnapshot*out){assert(n==tick);*out=sync_state;}
/** @brief 提供温度副本。@param n 当前测试毫秒。@param out 非NULL输出。@return 无返回值。 */
void TemperatureService_GetSnapshot(uint32_t n,TemperatureSnapshot*out){assert(n==tick);*out=temp;}
/** @brief 模拟快照期间到达的新音频IRQ时间。@param out 非NULL输出。@return 无返回值。 */
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot*out){*out=audio;out->last_callback_ms=tick+(irq_advance?1U:0U);}
/** @brief 返回复制音频副本之后的测试毫秒。@return 当前毫秒，必要时比入口晚1ms。 */
uint32_t BSP_Console_GetTickMs(void){return tick+(irq_advance?1U:0U);}
/** @brief 提供只读候选计数。@param out 非NULL输出。@return 无返回值。 */
void SoundDetectorService_GetSnapshot(SoundDetectorServiceSnapshot*out){*out=(SoundDetectorServiceSnapshot){.configured=true,.candidates=7};}
/** @brief 检查有界机器日志。@param s 本次有效文本，不保存地址。@param n 字节长度。@return 无返回值。 */
static void Log(const char*s,int n){assert(n>0&&n<256&&s[n-2]=='\r'&&s[n-1]=='\n');assert(strstr(s,"BACKEND=TOUCHGFX"));++logs;}
/** @brief 执行一次前台。@param n 测试毫秒。@param allow DHT许可。@return 无返回值。 */
static void Step(uint32_t n,bool allow){tick=n;UI_TouchGFX_Process(n,allow);}
/** @brief 在真实入口桩内部注入显示错误。@return 无返回值。 */
void Test_FrameworkStep(void){if(framework_error){lcd.status=BSP_TOUCHGFX_ERROR;lcd.hal_errors=1U;}}
extern void MX_TouchGFX_Process(void);
/** @brief 通知本轮绘制完成，供首帧地址场景复用。@param address 已绘制缓冲字节地址，0表示未绘图。@return 无返回值。 */
static void Completed(uint32_t address){UI_TouchGFX_FrameCompleted(address);}
/** @brief 执行独立服务场景。@param argc 必须2。@param argv 非NULL场景参数。@return 0表示通过。 */
int main(int argc,char**argv)
{
 assert(argc==2);const char *test=argv[1];UI_TouchGFXSnapshot s;UI_StatusText text;uint32_t generation=0;
 identity.role=BOARD_ROLE_A;sync_state.configured=true;sync_state.role=BOARD_ROLE_A;
 audio=(BSP_AudioSnapshot){.running=true,.session=1,.callback_count=1};
 irq_advance=!strcmp(test,"irq");
 if(!strcmp(test,"initfail"))init_ok=false;
 UI_TouchGFX_Init(0,Log);UI_TouchGFX_Init(0,Log);UI_TouchGFX_GetSnapshot(&s);
 MX_TouchGFX_Process();assert(framework_calls==0U); /* Actual GNU --wrap routing. */
 if(!strcmp(test,"initfail")){Step(0,true);assert(!s.ready&&!framework_inits&&!framework_calls&&!captures);return 0;}
 assert(s.initialized&&s.ready&&framework_inits==1U&&bsp_inits==1U);
 assert(!UI_TouchGFX_GetText(&text,&generation));
 if(!strcmp(test,"gate")){Step(1000,false);assert(!polls&&!captures&&!framework_calls&&!logs);return 0;}
 temp=(TemperatureSnapshot){.manual=true,.valid=true,.temperature_milli_c=25000,.sound_speed_mm_s=346450};
 if(!strcmp(test,"b")){identity.role=BOARD_ROLE_B;sync_state.role=BOARD_ROLE_B;sync_state.state=CLOCK_SERVO_ACQUIRING;sync_state.offset_valid=true;sync_state.offset_x2_ns=-4200;}
 Step(0,true);assert(framework_calls==1U&&captures==1U);assert(UI_TouchGFX_GetText(&text,&generation)&&generation==1U);
 assert(!strcmp(text.temperature,"25.0 C")&&!strcmp(text.temperature_source,"MANUAL"));
 UI_TouchGFX_GetSnapshot(&s);assert(s.page.audio_valid&&s.page.candidates==7U&&!s.page.measurement_valid);
 if(!strcmp(test,"b")){assert(s.page.offset_valid&&s.page.offset_x2_ns==-4200&&!strcmp(text.offset,"-2.100 US"));}
 else assert(s.page.sync==UI_SYNC_LOCAL&&!s.page.offset_valid&&!strcmp(text.offset,"-- US"));
 if(!strcmp(test,"race")){
  /* IRQ在父endFrame返回之前已换页，之后没有任何新绘图或换页通知。 */
  UI_TouchGFX_BufferVisible(BSP_TOUCHGFX_FRAME_B);Step(1,true);assert(!reveals);
  Completed(BSP_TOUCHGFX_FRAME_B);assert(!reveals);Step(2,false);assert(!reveals);
  Step(2,true);UI_TouchGFX_GetSnapshot(&s);
  assert(reveals==1U&&s.visible&&s.visible_address==BSP_TOUCHGFX_FRAME_B&&s.frames==1U);
  puts("PASS TouchGFX service race: visible before completed, no extra swap required");
  return 0;
 }
 if(!strcmp(test,"initial_address")){
  /* 初始化A可见并不能证明后来绘制完成的B已经可见。 */
  UI_TouchGFX_BufferVisible(BSP_TOUCHGFX_FRAME_A);Completed(BSP_TOUCHGFX_FRAME_B);
  Step(1,true);Step(2,true);assert(!reveals);
  UI_TouchGFX_BufferVisible(BSP_TOUCHGFX_FRAME_B);Step(3,true);assert(reveals==1U);
  puts("PASS TouchGFX service initial_address: initial A cannot reveal completed B");
  return 0;
 }
 if(!strcmp(test,"no_draw")){
  UI_TouchGFX_BufferVisible(BSP_TOUCHGFX_FRAME_A);Completed(0U);Step(1,true);assert(!reveals);
  UI_TouchGFX_BufferVisible(BSP_TOUCHGFX_FRAME_A);Completed(0U);Step(2,true);
  UI_TouchGFX_GetSnapshot(&s);assert(!reveals&&!s.visible&&s.frames==2U);
  Completed(BSP_TOUCHGFX_FRAME_B);Step(3,true);assert(!reveals);
  UI_TouchGFX_BufferVisible(BSP_TOUCHGFX_FRAME_B);Step(4,true);assert(reveals==1U);
  puts("PASS TouchGFX service no_draw: empty endFrame only increments frame count");
  return 0;
 }
 if(!strcmp(test,"invalid")){
  unsigned calls=framework_calls,old_logs=logs,old_polls=polls;
  UI_TouchGFX_BufferVisible(0xC0000002U);assert(!reveals&&logs==old_logs&&polls==old_polls);
  Step(1,false);assert(!reveals&&framework_calls==calls);
  Step(1,true);UI_TouchGFX_GetSnapshot(&s);assert(s.failed&&!s.ready&&framework_calls==calls&&reveals==1U);return 0;
 }
 if(!strcmp(test,"framework_error")){
  framework_error=true;UI_TouchGFX_FrameCompleted(BSP_TOUCHGFX_FRAME_B);UI_TouchGFX_BufferVisible(BSP_TOUCHGFX_FRAME_B);
  Step(1,true);UI_TouchGFX_GetSnapshot(&s);assert(s.failed&&!s.ready&&!reveals);
  unsigned calls=framework_calls;Step(2,true);assert(calls==framework_calls);return 0;
 }
 UI_TouchGFX_BufferVisible(BSP_TOUCHGFX_FRAME_A);Step(1,true);assert(!reveals);
 UI_TouchGFX_FrameCompleted(BSP_TOUCHGFX_FRAME_B);Step(2,true);assert(!reveals);
 UI_TouchGFX_BufferVisible(BSP_TOUCHGFX_FRAME_B);assert(!reveals);Step(3,false);assert(!reveals);
 Step(3,true);assert(reveals==1U);UI_TouchGFX_GetSnapshot(&s);assert(s.frames==1U&&s.visible&&s.visible_address==BSP_TOUCHGFX_FRAME_B);
 temp.temperature_milli_c=30000;temp.manual=false;Step(999,true);assert(captures==1U);
 Step(1000,true);assert(captures==2U&&UI_TouchGFX_GetText(&text,&generation)&&generation==2U);
 assert(!strcmp(text.temperature,"30.0 C")&&!strcmp(text.temperature_source,"AUTO"));
 if(!strcmp(test,"fail")){lcd.status=BSP_TOUCHGFX_ERROR;lcd.hal_errors=1U;unsigned calls=framework_calls;Step(1001,true);Step(2000,true);assert(framework_calls==calls&&captures==2U);UI_TouchGFX_GetSnapshot(&s);assert(s.failed&&!s.ready);}
 if(!strcmp(test,"audio_error")){audio.hal_error=1U;Step(2000,true);UI_TouchGFX_GetSnapshot(&s);assert(!s.page.audio_valid&&s.page.audio_errors==1U);}
 if(!strcmp(test,"drop")){audio.dropped_blocks=1U;Step(2000,true);UI_TouchGFX_GetSnapshot(&s);assert(!s.page.audio_valid&&s.page.audio_dropped==1U);}
 assert(!UI_TouchGFX_GetText(NULL,&generation)&&!UI_TouchGFX_GetText(&text,NULL));
 assert(logs>0U);printf("PASS TouchGFX service %s\n",test);return 0;
}
