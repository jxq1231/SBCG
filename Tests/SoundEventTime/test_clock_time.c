/** @file test_clock_time.c @brief 真实协调器在任何校时写之前使事件模型失效的主机验证。 */
#define main old_controller_main
#include "../SyncController/test_time_sync.c"
#undef main
#include "Detector/sound_event_time.h"
#ifdef SBCG_USE_PPS
#include "Sync/pps_service.h"
#endif
#include <stdlib.h>
#undef assert
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#ifdef SBCG_USE_PPS
static bool stop_ok=true;
bool PpsService_Init(uint32_t n,BoardRole r,void(*l)(const char*,int)){(void)n;(void)r;(void)l;return true;}
bool PpsService_Deinit(void){return true;}
bool PpsService_Invalidate(uint32_t n){(void)n;return stop_ok;}
void PpsService_Observe(uint32_t n,uint32_t m,bool l,bool v,int64_t o,int64_t d,int32_t f){(void)n;(void)m;(void)l;(void)v;(void)o;(void)d;(void)f;}
bool PpsService_BeforeFrequency(uint32_t n,int32_t p,int32_t q){(void)n;(void)p;(void)q;return true;}
void PpsService_AfterFrequency(uint32_t n,bool ok){(void)n;(void)ok;}

#endif
static unsigned sound_invalidations;
/** @brief 记录硬件写前的CLOCK清理；reason必须正确，无返回值。 */
void SoundEventTime_Invalidate(SoundTimeReset reason){assert(reason==SOUND_TIME_CLOCK);++sound_invalidations;}
BSP_PtpStatus __real_BSP_PTP_Step(int64_t phase);
BSP_PtpStatus __real_BSP_PTP_SetFrequencyPpb(int32_t ppb);
/** @brief phase为模拟修正纳秒，确认新模型已失效再转发，返回模拟硬件状态。 */
BSP_PtpStatus __wrap_BSP_PTP_Step(int64_t phase){assert(sound_invalidations>0);return __real_BSP_PTP_Step(phase);}
/** @brief ppb为模拟频率修正，确认新模型已失效再转发，返回模拟硬件状态。 */
BSP_PtpStatus __wrap_BSP_PTP_SetFrequencyPpb(int32_t ppb){assert(sound_invalidations>0);return __real_BSP_PTP_SetFrequencyPpb(ppb);}
int main(void){Reset();Init(BOARD_ROLE_B);sound_invalidations=0;requested=(ClockServoAction){CLOCK_SERVO_STEP,1000,0};Sample();assert(sound_invalidations>0 && clock_writes==1);
 Reset();Init(BOARD_ROLE_B);sound_invalidations=0;requested=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,10};Sample();assert(sound_invalidations>0 && clock_writes==1);
#ifdef SBCG_USE_PPS
 Reset();Init(BOARD_ROLE_B);sound_invalidations=0;stop_ok=false;++epoch;TimeSync_Process(now_tick,true);assert(sound_invalidations>0);stop_ok=true;
#endif
 puts("PASS real controller: invalidate event model before phase/frequency hardware commands");return 0;}
