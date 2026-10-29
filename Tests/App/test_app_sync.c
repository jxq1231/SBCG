/**
 * @file        test_app_sync.c
 * @brief       验证闭环协调器在真实App入口中的钩子卸载顺序与当前DHT门控。
 * @details     复用原服务桩和真实共同域模块；检查A/B快照、PPS资格、失败保留及未知身份禁用。
 * @note        仅主机行为测试，硬件时钟与网络由独立模块测试覆盖。
 */
#define main app_legacy_main
#include "test_app_main.c"
#undef main
#include "BSP/bsp_board_identity.h"
#include "Sync/time_sync.h"
#include "Sync/time_domain.h"
static BoardIdentityProfile identity;
static const BoardIdentityProfile *current_identity;
static unsigned sync_deinits,sync_inits,sync_loops,sync_allowed;
static bool sync_deinit_ok=true,sync_init_ok=true;
static unsigned snapshot_calls;
static bool snapshot_locked=true,snapshot_faulted;
#ifdef SBCG_USE_PPS
#include "Sync/pps_service.h"
static bool pps_due;
static bool pps_readable=true,pps_qualified=true;
/**
 * @brief 提供可控PPS资格，并核对快照发生在本轮安全处理之后。
 * @param now_ms 本轮HAL毫秒，必须与测试时钟一致。
 * @param out 非NULL输出；不可读时清零并返回false。
 * @return 用例设置的可读标志；仅主机桩，不读硬件。
 */
bool PpsService_GetSnapshot(uint32_t now_ms,PpsServiceSnapshot *out)
{
 assert(now_ms==tick_ms && !pps_due && out!=NULL);
 *out=(PpsServiceSnapshot){0};
 if(!pps_readable)return false;
 out->configured=true;out->qualified=pps_qualified;
 out->state=pps_qualified?PPS_SERVICE_QUALIFIED:PPS_SERVICE_UNQUALIFIED;
 return true;
}
/** @brief 验证DHT低态也逐轮调用PPS安全处理；now_ms为当前毫秒，allowed仅限制新排程，无返回值。 */
void PpsService_Process(uint32_t now_ms,bool allowed)
{ assert(pps_due && now_ms==tick_ms && allowed==!temperature_low);pps_due=false; }
#endif
/** @brief 返回用例准备的只读身份；无硬件读取。 @return 静态profile或NULL。 */
const BoardIdentityProfile *BSP_BoardIdentity_Get(void) { return current_identity; }
/** @brief 模拟最外层钩子的安全卸载。 @return 用例选定的成功标志，失败不碰内层。 */
bool TimeSync_Deinit(void) { ++sync_deinits;return sync_deinit_ok; }
/**
 * @brief 核对协调器在原网络初始化后、音频启动前得到真实身份。
 * @param now_ms 用例当前毫秒。
 * @param profile 实际身份，不可为空。
 * @param tick 长期读取接口，不可为空。
 * @param log 观测日志接口，不可为空。
 * @return 用例选定的初始化结果。
 */
bool TimeSync_Init(uint32_t now_ms,const BoardIdentityProfile *profile,uint32_t(*tick)(void),void(*log)(const char*,int))
{
 assert(now_ms==tick_ms && profile==current_identity && tick && log);
 assert(time_initialized && observation_initialized && audio_inits+1==completed_init_calls);
 ++sync_inits;return sync_init_ok;
}
/**
 * @brief 核对每次前台处理使用温度服务本轮更新后的DATA状态。
 * @param now_ms 当前测试tick。
 * @param processing_allowed 必须与当前DHT低电平状态相反。
 * @return 无返回值。
 */
void TimeSync_Process(uint32_t now_ms,bool processing_allowed)
{
#ifdef SBCG_USE_PPS
 assert(!pps_due);pps_due=true;
#endif
 assert(now_ms==tick_ms && temperature_calls==lwip_calls && console_calls==temperature_calls);
 assert(processing_allowed==!temperature_low);++sync_loops;if(processing_allowed)++sync_allowed;
}
/**
 * @brief 交付本轮同步只读副本，让真实共同域模块验证A/B资格。
 * @param now_ms 本轮HAL毫秒，须已完成同步处理。
 * @param out 非NULL快照输出；角色来自实际App身份。
 * @return 无返回值；锁定/故障由用例控制，未初始化时不伪造资格。
 */
void TimeSync_GetSnapshot(uint32_t now_ms,TimeSyncSnapshot *out)
{
 assert(now_ms==tick_ms && out!=NULL && snapshot_calls<sync_loops);
 ++snapshot_calls;
 *out=(TimeSyncSnapshot){0};
 out->configured=sync_init_ok;
 out->role=current_identity!=NULL?current_identity->role:BOARD_ROLE_UNCONFIGURED;
 out->internal_locked=snapshot_locked;out->offset_valid=snapshot_locked;
 out->faulted=snapshot_faulted;
}
/** @brief 执行App闭环集成断言。 @return 全部通过为0。 */
int main(void)
{
 for(unsigned role=0;role<2;++role)
 {
  memcpy(info_uid,role ? BoardIdentity_DefaultMap.b_uid:BoardIdentity_DefaultMap.a_uid,sizeof info_uid);
  assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&identity));current_identity=&identity;
  sync_deinits=sync_inits=sync_loops=sync_allowed=snapshot_calls=0;sync_deinit_ok=sync_init_ok=true;
  snapshot_locked=true;snapshot_faulted=false;
  Reset(0,true);assert(sync_deinits==1 && sync_inits==1 && deinit_calls==1 && map_inits==1);
  tick_ms=1000;App_Process();assert(sync_loops==1 && sync_allowed==1);
  TimeDomainSnapshot domain;TimeDomain_Get(&domain);
  assert(domain.local_valid && !domain.valid && !domain.peer_seen);
  start_low_in_process=true;tick_ms=2000;App_Process();assert(sync_loops==2 && sync_allowed==1);
  assert(snapshot_calls==sync_loops);
  TimeDomain_Get(&domain);assert(domain.local_valid);
  temperature_low=false;start_low_in_process=false;
  App_Init();assert(sync_inits==2 && deinit_calls==2 && map_inits==2);
  const unsigned old_deinit=deinit_calls,old_inits=sync_inits,old_map=map_inits;
  sync_deinit_ok=false;App_Init();assert(deinit_calls==old_deinit && sync_inits==old_inits && map_inits==old_map);
  assert(strstr(last_line,"$SYNC_INIT,RESULT=OWNERSHIP_ERROR"));
  tick_ms=3000;App_Process();assert(sync_loops==3 && snapshot_calls==3);
  snapshot_locked=false;tick_ms=3100;App_Process();
  TimeDomain_Get(&domain);assert(domain.local_valid==(role==0U));
  snapshot_locked=true;snapshot_faulted=true;tick_ms=3200;App_Process();
  TimeDomain_Get(&domain);assert(!domain.local_valid && !domain.valid);
  snapshot_faulted=false;
#ifdef SBCG_USE_PPS
  pps_qualified=false;tick_ms=3300;App_Process();
  TimeDomain_Get(&domain);assert(!domain.local_valid && !domain.valid);
  pps_qualified=true;tick_ms=3350;App_Process();
  TimeDomain_Get(&domain);assert(domain.local_valid);
  pps_readable=false;tick_ms=3400;App_Process();
  TimeDomain_Get(&domain);assert(!domain.local_valid && !domain.valid);
  pps_readable=true;
#endif
 }
 sync_deinit_ok=true;current_identity=NULL;sync_inits=sync_loops=0;
 memset(info_uid,0,sizeof info_uid);lwip_calls=0;Reset(0,true);App_Process();
 assert(sync_inits==0 && sync_loops==0 && lwip_calls==0);
 memcpy(info_uid,BoardIdentity_DefaultMap.a_uid,sizeof info_uid);
 assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&identity));current_identity=&identity;
 sync_init_ok=false;Reset(0,true);assert(sync_inits==1 && map_inits==1 && temperature_initialized);
 puts("PASS sync App: real time domain, A/B snapshot/PPS qualification, DATA gate and ownership failures");
 return 0;
}
