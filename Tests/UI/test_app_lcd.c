/** @file test_app_sdram.c @brief 真实App入口的SDRAM启动顺序/前台门控/日志节流测试。 */
#define main unused_app_main
#include "../App/test_app_main.c"
#undef main
#include "BSP/bsp_sdram.h"
#include "Detector/sound_event_time.h"
#include "BSP/bsp_board_identity.h"
#include "Sync/time_sync.h"
static BoardIdentityProfile identity;
const BoardIdentityProfile *BSP_BoardIdentity_Get(void){return &identity;}
bool TimeSync_Deinit(void){return true;}
bool TimeSync_Init(uint32_t n,const BoardIdentityProfile*p,uint32_t(*tick)(void),void(*log)(const char*,int))
{assert(n==tick_ms&&p&&tick&&log);return true;}
void TimeSync_Process(uint32_t n,bool allowed){assert(n==tick_ms&&allowed==!temperature_low);}
bool AudioTimeAnchor_TakeObservation(AudioTimeAnchorObservation*out){*out=(AudioTimeAnchorObservation){0};return true;}
void AudioTimeMapMonitor_Observe(uint32_t n,const AudioTimeAnchorObservation*out){assert(n==tick_ms&&out);}
void SoundEventTime_Init(void(*log)(const char*,int)){assert(log);}
void SoundEventTime_Observe(uint32_t n,const AudioTimeAnchorObservation*out){assert(n==tick_ms&&out);}
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot*out){*out=(BSP_AudioSnapshot){0};++tick_ms;}
void SoundEventTime_Process(uint32_t n,const BSP_AudioSnapshot*out){assert(n==tick_ms&&out);}
static unsigned sdram_init_calls,sdram_steps,sdram_enabled;
static BSP_SdramSnapshot sdram;
bool BSP_SDRAM_Init(void)
{assert(audio_inits==0U);++sdram_init_calls;sdram.status=BSP_SDRAM_TESTING;sdram.sdclk_hz=66666666;sdram.refresh_count=1021;return true;}
void BSP_SDRAM_Process(uint32_t now,bool allowed)
{assert(now==tick_ms&&allowed==!temperature_low);++sdram_steps;if(allowed)++sdram_enabled;}
void BSP_SDRAM_GetSnapshot(BSP_SdramSnapshot*out){*out=sdram;}
#include "UI/status_service.h"
static unsigned display_calls,ui_inits,ui_calls;
bool BSP_SDRAM_InitDisplay(void)
{assert(audio_inits==0U);++display_calls;sdram.status=BSP_SDRAM_READY;return true;}
void UI_StatusService_Init(uint32_t now,void(*log)(const char*,int))
{assert(audio_inits>0U&&display_calls==1U&&now==tick_ms&&log);++ui_inits;}
void UI_StatusService_Process(uint32_t now,bool allowed)
{assert(audio_calls==lwip_calls&&now==tick_ms&&allowed==!temperature_low);++ui_calls;}
int main(void)
{
 memcpy(info_uid,BoardIdentity_DefaultMap.a_uid,sizeof info_uid);
 assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&identity));
 Reset(0,true);assert(display_calls==1U&&sdram_init_calls==0U&&ui_inits==1U);
 tick_ms=1000;App_Process();assert(ui_calls==1U&&sdram_steps==0U&&write_count==2U);
 start_low_in_process=true;tick_ms=2000;App_Process();assert(ui_calls==2U&&sdram_steps==0U&&write_count==2U);
 puts("PASS App display init before audio, UI after audio, no destructive test, DHT gate and one UART per loop");return 0;
}
