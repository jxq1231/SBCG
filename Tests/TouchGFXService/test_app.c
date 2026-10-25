/** @file test_app.c @brief 复用旧App桩核对TouchGFX前后顺序及独占调度。 */
#define SBCG_TEST_SKIP_LEGACY_UI_MAIN
#include "../UI/test_app_lcd.c"
#include "BSP/bsp_touchgfx_display.h"
#include "UI/touchgfx_service.h"
static unsigned prepared,framework_inits,framework_calls;
/** @brief 核对只在SDRAM显示READY之后且音频启动前预备双帧。@return true为桩就绪。 */
bool BSP_TouchGFXDisplay_Init(void)
{assert(display_calls==1U&&audio_inits==0U);++prepared;return true;}
/** @brief 核对框架初始化晚于音频/业务初始化，预备已完成。@param now 当前桩毫秒。@param log 非空日志出口。@return 无返回值。 */
void UI_TouchGFX_Init(uint32_t now,void(*log)(const char*,int))
{assert(prepared==1U&&audio_inits>0U&&now==tick_ms&&log);++framework_inits;}
/** @brief 核对唯一UI调度位于音频处理后并使用本轮DHT门控。@param now 当前毫秒。@param allow 当前允许标志。@return 无返回值。 */
void UI_TouchGFX_Process(uint32_t now,bool allow)
{assert(audio_calls==lwip_calls&&now==tick_ms&&allow==!temperature_low);++framework_calls;}
/** @brief 执行真实App与两种显示后端互斥断言。@return 全部通过为0。 */
#ifndef SBCG_TEST_SKIP_TOUCH_MAIN
int main(void)
{
 memcpy(info_uid,BoardIdentity_DefaultMap.a_uid,sizeof info_uid);
 assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&identity));
 Reset(0,true);assert(prepared==1U&&framework_inits==1U&&ui_inits==0U);
 tick_ms=1000;App_Process();assert(framework_calls==1U&&ui_calls==0U&&sdram_steps==0U);
 start_low_in_process=true;tick_ms=2000;App_Process();assert(framework_calls==2U&&ui_calls==0U);
 puts("PASS TouchGFX App: preclear before audio, framework after services, exclusive UI dispatch, current DHT gate");
 return 0;
}
#endif
