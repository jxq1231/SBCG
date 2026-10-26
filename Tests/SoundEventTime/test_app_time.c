/** @file test_app_time.c @brief 真实App入口单次消费/双路分发/DHT门控及快照后刷新时间的主机验证。 */
#define main unused_app_main
#include "../App/test_app_main.c"
#undef main
#include "Detector/sound_event_time.h"
#include "Network/udp_sound_event.h"
#include "Measure/measurement_result.h"
#include "Detector/standard_sequence.h"
#include "Sync/time_domain.h"
#include "BSP/bsp_board_identity.h"
#include "Sync/time_sync.h"
static BoardIdentityProfile identity;
static const BoardIdentityProfile *current_identity;
const BoardIdentityProfile *BSP_BoardIdentity_Get(void){return current_identity;}
bool TimeSync_Deinit(void){return true;}
bool TimeSync_Init(uint32_t now,const BoardIdentityProfile *p,uint32_t(*tick)(void),void(*log)(const char*,int)){assert(now==tick_ms && p && tick && log);return true;}
void TimeSync_Process(uint32_t now,bool allowed){assert(now==tick_ms && allowed==!temperature_low);}
static unsigned takes,model_receives,event_receives,event_processes,event_inits;
static AudioTimeAnchorObservation delivered;
/** @brief 本轮唯一取出，复制给out并累计；无DMA或PTP读取，返回true。 */
bool AudioTimeAnchor_TakeObservation(AudioTimeAnchorObservation *out){assert(!temperature_low && out);++takes;delivered=(AudioTimeAnchorObservation){.sequence=takes,.captured_ms=tick_ms,.anchor={.valid=true,.session=1}};*out=delivered;return true;}
/** @brief 旧观察器取得同一副本，不应再消费单槽；now/out为App当前交付，无返回值。 */
void AudioTimeMapMonitor_Observe(uint32_t now,const AudioTimeAnchorObservation *out){assert(!temperature_low && now==tick_ms && memcmp(out,&delivered,sizeof delivered)==0);++model_receives;}
/** @brief 检查初始化顺序；log为非NULL前台出口，无返回值。 */
void SoundEventTime_Init(void(*log)(const char*,int)){assert(map_inits && log);++event_inits;}
/** @brief 新查询器收到与旧观察器相同副本；now/out来自App，无返回值。 */
void SoundEventTime_Observe(uint32_t now,const AudioTimeAnchorObservation *out){assert(!temperature_low && now==tick_ms && model_receives==takes && memcmp(out,&delivered,sizeof delivered)==0);++event_receives;}
/** @brief 模拟快照与DMA IRQ一起推进tick；out为输出，无返回值。 */
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *out){*out=(BSP_AudioSnapshot){.session=1,.running=true,.last_callback_ms=++tick_ms};}
/** @brief now必须在快照后读取，snapshot为最新副本；无返回值。 */
void SoundEventTime_Process(uint32_t now,const BSP_AudioSnapshot *snapshot){assert(!temperature_low && now==tick_ms && snapshot->last_callback_ms==now && takes==event_receives);++event_processes;}
/** @brief App测试桩：无待取成功TOA事件，返回false。 */
bool SoundEventTime_TakeEvent(SoundEventTimeEvent *event){if(event)memset(event,0,sizeof *event);return false;}
/** @brief App测试桩：初始化事件UDP不访问网络。 */
bool UDPSoundEvent_Init(uint32_t now,const BoardIdentityProfile *p,uint32_t(*tick)(void),UDPSoundEvent_Log log){(void)now;(void)p;(void)tick;(void)log;return true;}
/** @brief App测试桩：不推进事件UDP。 */
void UDPSoundEvent_Process(uint32_t now){(void)now;}
/** @brief App测试桩：不发送事件UDP。 */
bool UDPSoundEvent_Send(uint32_t now,const SoundEventRecord *event){(void)now;(void)event;return true;}
/** @brief App测试桩：清空权威测量缓存。 */
void MeasurementResult_Init(uint32_t now){(void)now;}
/** @brief App测试桩：不生成权威测量。 */
bool MeasurementResult_PublishPair(const SoundPairResult *pair,const TemperatureSnapshot *temperature,uint32_t now){(void)pair;(void)temperature;(void)now;return false;}
/** @brief App测试桩：不安装结果消费者。 */
void UDPSoundEvent_SetResultSink(UDPSoundEvent_ResultSink sink){(void)sink;}


/** @brief App测试桩：无有效温度快照，测量结果不能显示。 */
void TemperatureService_GetSnapshot(uint32_t now,TemperatureSnapshot *out){(void)now;if(out)memset(out,0,sizeof *out);}

/** @brief App测试桩：标准序列初始化不访问硬件。 */
bool StandardSequence_Init(StandardSequence *s,const StandardSequenceConfig *c){(void)s;(void)c;return true;}
/** @brief App测试桩：不检测周期序列。 */
bool StandardSequence_Push(StandardSequence *s,uint64_t t,uint8_t q){(void)s;(void)t;(void)q;return false;}
/** @brief App测试桩：清零序列快照。 */
void StandardSequence_Get(const StandardSequence *s,StandardSequenceSnapshot *o){(void)s;if(o)memset(o,0,sizeof *o);}

/** @brief App测试桩：撤销权威测量结果。 */
void MeasurementResult_Invalidate(uint32_t now){(void)now;}

/** @brief App测试桩：无同步快照，共同域无效。 */
void TimeSync_GetSnapshot(uint32_t now,TimeSyncSnapshot *out){(void)now;if(out)memset(out,0,sizeof *out);}
/** @brief App测试桩：清空共同时间域。 */
void TimeDomain_Init(void){}
/** @brief App测试桩：所有同步快照均不产生共同域。 */
void TimeDomain_Update(const TimeSyncSnapshot *sync){(void)sync;}
/** @brief App测试桩：共同时间域无效。 */
void TimeDomain_Get(TimeDomainSnapshot *out){if(out){out->valid=false;out->id=0U;}}
int main(void){memcpy(info_uid,BoardIdentity_DefaultMap.a_uid,sizeof info_uid);assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&identity));current_identity=&identity;
 Reset(0,true);assert(event_inits==1);assert(write_count==1);tick_ms=1000;App_Process();assert(write_count==2);assert(takes==1 && model_receives==1 && event_receives==1 && event_processes==1);
 start_low_in_process=true;tick_ms=2000;App_Process();assert(takes==1 && event_processes==1 && write_count==2);
 start_low_in_process=false;temperature_low=false;tick_ms=3000;App_Process();assert(write_count==3);assert(takes==2 && model_receives==2 && event_receives==2 && event_processes==2);
 assert(map_calls==0);puts("PASS real App time: sole TakeObservation, identical fanout, DHT gate, fresh snapshot tick");return 0;}
