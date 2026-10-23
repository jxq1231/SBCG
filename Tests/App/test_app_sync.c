/**
 * @file        test_app_sync.c
 * @brief       验证闭环协调器在真实App入口中的钩子卸载顺序与当前DHT门控。
 * @details     复用原服务桩，新增协调器桩；检查A/B、失败保留运行状态及未知身份禁用。
 * @note        仅主机行为测试，硬件时钟与网络由独立模块测试覆盖。
 */
#define main app_legacy_main
#include "test_app_main.c"
#undef main
#include "BSP/bsp_board_identity.h"
#include "Sync/time_sync.h"
static BoardIdentityProfile identity;
static const BoardIdentityProfile *current_identity;
static unsigned sync_deinits,sync_inits,sync_loops,sync_allowed;
static bool sync_deinit_ok=true,sync_init_ok=true;
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
 assert(now_ms==tick_ms && temperature_calls==lwip_calls && console_calls==temperature_calls);
 assert(processing_allowed==!temperature_low);++sync_loops;if(processing_allowed)++sync_allowed;
}
/** @brief 执行App闭环集成断言。 @return 全部通过为0。 */
int main(void)
{
 for(unsigned role=0;role<2;++role)
 {
  memcpy(info_uid,role ? BoardIdentity_DefaultMap.b_uid:BoardIdentity_DefaultMap.a_uid,sizeof info_uid);
  assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&identity));current_identity=&identity;
  sync_deinits=sync_inits=sync_loops=sync_allowed=0;sync_deinit_ok=sync_init_ok=true;
  Reset(0,true);assert(sync_deinits==1 && sync_inits==1 && deinit_calls==1 && map_inits==1);
  tick_ms=1000;App_Process();assert(sync_loops==1 && sync_allowed==1);
  start_low_in_process=true;tick_ms=2000;App_Process();assert(sync_loops==2 && sync_allowed==1);
  temperature_low=false;start_low_in_process=false;
  App_Init();assert(sync_inits==2 && deinit_calls==2 && map_inits==2);
  const unsigned old_deinit=deinit_calls,old_inits=sync_inits,old_map=map_inits;
  sync_deinit_ok=false;App_Init();assert(deinit_calls==old_deinit && sync_inits==old_inits && map_inits==old_map);
  assert(strstr(last_line,"$SYNC_INIT,RESULT=OWNERSHIP_ERROR"));
  tick_ms=3000;App_Process();assert(sync_loops==3);
 }
 sync_deinit_ok=true;current_identity=NULL;sync_inits=sync_loops=0;
 memset(info_uid,0,sizeof info_uid);lwip_calls=0;Reset(0,true);App_Process();
 assert(sync_inits==0 && sync_loops==0 && lwip_calls==0);
 memcpy(info_uid,BoardIdentity_DefaultMap.a_uid,sizeof info_uid);
 assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&identity));current_identity=&identity;
 sync_init_ok=false;Reset(0,true);assert(sync_inits==1 && map_inits==1 && temperature_initialized);
 puts("PASS sync App: A/B ordering, current DATA gate, failed outer detach preserves inner chain, unknown identity disabled");
 return 0;
}
