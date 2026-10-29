/**
 * @file test_diagnostics.c
 * @brief 用真实服务验证低频诊断输出、原因计数及分段计时。
 * @details 复用冻结PCM及可控tick，主机计时只验证字段语义，不表示板端耗时。
 * @note 无硬件访问，服务和输入由本测试串行拥有。
 */
#define main unused_service_main
#include "test_service.c"
#undef main
/** @brief 执行诊断行为断言，成功返回0。 */
int main(void)
{
 CHECK(ChirpService_Init(0U,0U,Log));
 tick_ms=1000U;Work(1U);CHECK(strncmp(last_any,"$CHIRP_STATE,",13)==0);
 const unsigned before=lines;tick_ms=1999U;Work(1U);CHECK(lines==before);
 tick_ms=2000U;Work(1U);CHECK(strncmp(last_any,"$CHIRP_DIAG,",12)==0);
 CHECK(strstr(last_any,",TIMEOUT=0,HISTORY=0,FULL=0,RANGE=0,RESET=0,DONE=0,DONE_MAX_MS=0,DROP_MAX_MS=0")!=NULL);
 ChirpServiceSnapshot s;
 CHECK(ChirpService_Init(tick_ms,0U,Log));Block(0U,2000U,true);Work(80U);
 ChirpService_GetSnapshot(&s);CHECK(s.completed_jobs==1U && s.matches==1U && s.dropped==0U);
 /* 完成时延从关联块completed_ms起算：257窗/8窗每轮，第33次调用完成，前32个tick为等待。 */
 CHECK(s.max_complete_ms==32U && s.max_drop_ms==0U);
 CHECK(ChirpService_Init(tick_ms,0U,Log));const uint32_t start=tick_ms;
 Block(0U,2000U,true);Work(1U);
 tick_ms=start+1000U;Block(8192U,UINT64_MAX,true);
 tick_ms=start+1500U;Work(1U);ChirpService_GetSnapshot(&s);
 CHECK(s.drop_expired==1U && s.dropped==1U && s.drop_reset==0U);
 /* 丢弃也记录时延，不能只统计成功完成的样本。 */
 CHECK(s.max_drop_ms==1500U && s.max_complete_ms==0U);
 CHECK(ChirpService_Init(tick_ms,0U,Log));const uint32_t history_start=tick_ms;
 Block(0U,8100U,true);Work(1U);
 tick_ms=history_start+10U;
 Block(8192U,8100U,true);Block(16384U,UINT64_MAX,true);ChirpService_GetSnapshot(&s);
 CHECK(s.drop_history==1U && s.dropped==1U && s.drop_reset==0U);
 /* Complete内历史丢弃同样记录时延。 */
 CHECK(s.max_drop_ms==10U);
 CHECK(ChirpService_Init(tick_ms,0U,Log));const uint32_t reset_start=tick_ms;
 Block(0U,2000U,true);Work(1U);
 tick_ms=reset_start+7U;
 Block(8192U,UINT64_MAX,false);ChirpService_GetSnapshot(&s);
 CHECK(s.drop_reset==1U && s.dropped==1U && s.drop_expired==0U);
 /* Reset清队列也记录排队任务时延，不丢弃失败样本统计。 */
 CHECK(s.max_drop_ms==7U);
 CHECK(ChirpService_Init(tick_ms,0U,Log));
 Block(UINT64_MAX-8192U,UINT64_MAX-2000U,true);Work(1U);ChirpService_GetSnapshot(&s);
 CHECK(s.drop_range==1U && s.dropped==1U && s.pending==0U);
 CHECK(ChirpService_Init(tick_ms,0U,Log));
 for(uint64_t i=0U;i<=16U;++i){Block(i*8192U,i%4U==0U ? i*8192U+2000U:UINT64_MAX,true);Work(1U);}
 ChirpService_GetSnapshot(&s);
 CHECK(s.candidates==5U && s.drop_full==1U && s.dropped==1U && s.pending==4U);
 CHECK(s.dropped==s.drop_expired+s.drop_history+s.drop_full+s.drop_range+s.drop_reset);
 tick_ms=100U;CHECK(ChirpService_Init(tick_ms,0U,Log));Block(0U,UINT64_MAX,true);
 tick_step=3U;measurement_tick=UINT32_MAX-5U;Work(1U);tick_step=0U;
 ChirpService_GetSnapshot(&s);CHECK(s.max_scan_ms==3U && s.max_score_ms==3U && s.max_work_ms==6U);
 tick_ms=600U;Work(1U);ChirpService_GetSnapshot(&s);CHECK(s.max_process_gap_ms==500U);
 tick_ms=UINT32_MAX-20U;CHECK(ChirpService_Init(tick_ms,0U,Log));Work(1U);
 tick_ms=10U;Work(1U);ChirpService_GetSnapshot(&s);CHECK(s.max_process_gap_ms==31U);
 puts("PASS chirp diagnostics: cadence, five exclusive causes, completion, phase timing and tick wrap");return 0;
}
