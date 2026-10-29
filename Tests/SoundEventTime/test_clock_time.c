/**
 * @file        test_clock_time.c
 * @brief       真实协调器对校时动作与声音时间模型的交接验证。
 * @details     相位动作在任何写时钟之前撤销模型；成功且相位连续的调频保留声音队列与采锚节拍，
 *              只按新旧绝对频率差通报模型；调频失败或未知代际变化仍撤销模型。
 * @note        复用协调器测试的依赖替代；本文件只检查调用时序与参数，不模拟硬件精度。
 */
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
static unsigned sound_invalidations,frequency_notices;
static int32_t noticed_delta;
static bool frequency_write_seen,invalidate_after_write;
/** @brief 记录模型撤销；reason必须为CLOCK，并标记是否发生在调频硬件写入之后。 */
void SoundEventTime_Invalidate(SoundTimeReset reason)
{
    assert(reason==SOUND_TIME_CLOCK); ++sound_invalidations;
    if(frequency_write_seen) invalidate_after_write=true;
}
/**
 * @brief 记录保留队列的调频通报。
 * @param delta_ppb 新绝对频率减旧绝对频率。
 * @note 必须在实际写时钟之前调用；成功调频不得同时撤销模型。
 */
void SoundEventTime_NotifyFrequencyChange(int32_t delta_ppb)
{
    assert(!frequency_write_seen); ++frequency_notices; noticed_delta=delta_ppb;
}
BSP_PtpStatus __real_BSP_PTP_Step(int64_t phase);
BSP_PtpStatus __real_BSP_PTP_SetFrequencyPpb(int32_t ppb);
/** @brief phase为模拟修正纳秒，确认新模型已失效再转发，返回模拟硬件状态。 */
BSP_PtpStatus __wrap_BSP_PTP_Step(int64_t phase){assert(sound_invalidations>0);return __real_BSP_PTP_Step(phase);}
/**
 * @brief ppb为模拟频率修正，确认已按频率差通报且未撤销模型再转发。
 * @return 模拟硬件写入结果；失败路径的撤销由调用方在返回后检查。
 */
BSP_PtpStatus __wrap_BSP_PTP_SetFrequencyPpb(int32_t ppb)
{
    assert(frequency_notices>0 && sound_invalidations==0);
    frequency_write_seen=true; return __real_BSP_PTP_SetFrequencyPpb(ppb);
}
/** @brief 清空本文件与被包含测试共享的观察计数。 */
static void ClearObservations(void)
{
    sound_invalidations=frequency_notices=0; noticed_delta=0;
    frequency_write_seen=invalidate_after_write=false;
}
int main(void)
{
    #ifdef SBCG_USE_PPS
    stop_ok=true;
    #endif
    Reset();Init(BOARD_ROLE_B);ClearObservations();
    requested=(ClockServoAction){CLOCK_SERVO_STEP,1000,0};Sample();
    assert(sound_invalidations>0 && frequency_notices==0 && clock_writes==1 && !invalidate_after_write);

    Reset();Init(BOARD_ROLE_B);ClearObservations();
    requested=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,10};Sample();
    assert(clock_writes==1 && frequency_notices==1 && noticed_delta==10);
    assert(sound_invalidations==0 && !invalidate_after_write);
    assert(strchr(trace,'A')==NULL);

    Reset();Init(BOARD_ROLE_B);ClearObservations();write_status=BSP_PTP_UPDATE_TIMEOUT;
    requested=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,10};Sample();
    assert(clock_writes==1 && frequency_notices==1 && noticed_delta==10);
    assert(active_servo->state==CLOCK_SERVO_FAULT);
    assert(sound_invalidations>0 && invalidate_after_write);

    #ifdef SBCG_USE_PPS
    Reset();Init(BOARD_ROLE_B);ClearObservations();stop_ok=false;++epoch;
    TimeSync_Process(now_tick,true);
    assert(sound_invalidations>0 && frequency_notices==0);
    stop_ok=true;
    #endif
    puts("PASS real controller: phase revokes; successful continuous frequency keeps model and reports delta; failure revokes");return 0;
}
