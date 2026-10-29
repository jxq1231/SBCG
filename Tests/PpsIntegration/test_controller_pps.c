/**
 * @file test_controller_pps.c
 * @brief 检查真实TimeSync在硬件动作前后交接PPS资格，保留原协调测试。
 * @note 主机替代PPS和硬件，不证明真实脉冲精度。
 */
#define main previous_controller_tests
#include "../SyncController/test_time_sync.c"
#undef main
#include "Sync/pps_service.h"
static unsigned pps_inits,pps_deinits,pps_observations,pps_invalidations,pps_before,pps_after;
static bool pps_init_ok=true,pps_stop_ok=true,pps_before_ok=true,pps_saw_lock,track_hooks;
static char pps_stage;
/** @brief 模拟PPS初始化。 @param now 当前毫秒。 @param role A/B。 @param log 可空日志。 @return 预置结果。 */
bool PpsService_Init(uint32_t now,BoardRole role,void(*log)(const char*,int))
{ assert(now==now_tick && (role==BOARD_ROLE_A||role==BOARD_ROLE_B));(void)log;++pps_inits;return pps_init_ok; }
/** @brief 模拟停止PPS。 @return 预置停止结果。 */
bool PpsService_Deinit(void) { ++pps_deinits;return pps_stop_ok; }
/** @brief 协调器不应代App调用PPS轮询。 @param now 当前毫秒。 @param allowed 排程门控。 @return 无返回值。 */
void PpsService_Process(uint32_t now,bool allowed) { (void)now;(void)allowed;assert(false); }
/**
 * @brief 验证Observe后、动作提交前转交本次内部锁证据。
 * @param now 新鲜毫秒。 @param ms 样本形成毫秒。 @param locked 观测瞬间锁。
 * @param drift_valid 是否已有漂移。 @param offset 两倍纳秒时差。 @param delay 两倍纳秒时延。
 * @param drift ppb漂移。 @return 无返回值。
 */
void PpsService_Observe(uint32_t now,uint32_t ms,bool locked,bool drift_valid,int64_t offset,int64_t delay,int32_t drift)
{ assert(now==now_tick && ms==captured && offset==last_offset && delay==last_delay);(void)drift_valid;(void)drift;++pps_observations;pps_saw_lock=locked;if(track_hooks)pps_stage='O'; }
/** @brief 记录同步撤资格。 @param now 当前毫秒。 @return 预置停止结果。 */
bool PpsService_Invalidate(uint32_t now) { assert(now==now_tick);++pps_invalidations;if(track_hooks)pps_stage='I';return pps_stop_ok; }
/** @brief 检查频率动作前交接。 @param now 当前毫秒。 @param previous 已知硬件ppb。 @param next 目标ppb。 @return 预置许可。 */
bool PpsService_BeforeFrequency(uint32_t now,int32_t previous,int32_t next)
{ assert(now==now_tick && previous==hardware_frequency && next==requested.frequency_ppb);++pps_before;if(track_hooks){assert(pps_stage=='O');pps_stage='B';}return pps_before_ok; }
/** @brief 检查真实频率结果之后交接。 @param now 当前毫秒。 @param success 真实结果。 @return 无返回值。 */
void PpsService_AfterFrequency(uint32_t now,bool success)
{ assert(now==now_tick);++pps_after;if(track_hooks){assert(pps_stage==(success?'W':'B')||!success);pps_stage='A';} }
BSP_PtpStatus __real_BSP_PTP_Step(int64_t phase);
BSP_PtpStatus __real_BSP_PTP_SetFrequencyPpb(int32_t ppb);
/** @brief 检查相位写之前PPS已经同步撤销。 @param phase 纳秒修正。 @return 原替代硬件结果。 */
BSP_PtpStatus __wrap_BSP_PTP_Step(int64_t phase)
{ if(track_hooks)assert(pps_stage=='I' && pps_stop_ok);return __real_BSP_PTP_Step(phase); }
/** @brief 检查频率写之前资格交接完成。 @param ppb 绝对修正。 @return 原替代硬件结果。 */
BSP_PtpStatus __wrap_BSP_PTP_SetFrequencyPpb(int32_t ppb)
{ if(track_hooks){assert(pps_stage=='B' && pps_before_ok);pps_stage='W';}return __real_BSP_PTP_SetFrequencyPpb(ppb); }
/** @brief 执行协调器与PPS的集成负例。 @return 成功0。 */
int main(void)
{
 assert(previous_controller_tests()==0);
 track_hooks=true;Reset();Init(BOARD_ROLE_B);active_servo->internal_locked=true;active_servo->drift_valid=true;
 requested=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,10};Sample();
 assert(pps_saw_lock && pps_before && pps_after && pps_stage=='A' && clock_writes==1U);
 Reset();Init(BOARD_ROLE_B);requested=(ClockServoAction){CLOCK_SERVO_STEP,1000,0};Sample();assert(clock_writes==1U && pps_stage=='I');
 Reset();Init(BOARD_ROLE_B);requested=(ClockServoAction){CLOCK_SERVO_STEP,1000,0};pps_stop_ok=false;Sample();
 assert(clock_writes==0U && active_servo->state==CLOCK_SERVO_FAULT);pps_stop_ok=true;
 Reset();Init(BOARD_ROLE_B);requested=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,10};pps_before_ok=false;Sample();
 assert(clock_writes==0U && active_servo->state==CLOCK_SERVO_FAULT);pps_before_ok=true;
 Reset();Init(BOARD_ROLE_B);unsigned previous=pps_invalidations;estimate_status=SYNC_ESTIMATOR_BAD_ORDER;Sample();assert(pps_invalidations>previous);
 previous=pps_invalidations;estimate_status=SYNC_ESTIMATOR_OK;observe_ok=false;Sample();assert(pps_invalidations>previous);
 previous=pps_invalidations;observe_ok=true;peer_reset=true;TimeSync_Process(now_tick,true);assert(pps_invalidations>previous);
 previous=pps_invalidations;++epoch;TimeSync_Process(now_tick,true);assert(pps_invalidations>previous);
 unsigned stopped=pps_deinits;deinit_ok=false;assert(!TimeSync_Deinit());assert(pps_deinits==stopped);deinit_ok=true;
 track_hooks=false;Reset();Init(BOARD_ROLE_A);unsigned a_invalidations=pps_invalidations;
 peer_reset=true;TimeSync_Process(now_tick,true);assert(pps_invalidations==a_invalidations);
 Reset();unsigned init_count=pps_inits;pps_init_ok=false;BoardIdentityProfile p={.role=BOARD_ROLE_A};
 assert(!TimeSync_Init(now_tick,&p,Tick,Log) && pps_inits==init_count+1U);pps_init_ok=true;
 puts("PASS PPS integration: before-commit lock, pre-step stop, frequency handoff, stop-failure no clock write, invalidation and lifecycle");return 0;
}
