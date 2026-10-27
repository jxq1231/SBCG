/** @file test_app_sdram.c @brief 真实App入口的SDRAM启动顺序/前台门控/日志节流测试。 */
#define main unused_app_main
#include "../App/test_app_main.c"
#undef main
#include "BSP/bsp_sdram.h"
#include "Detector/sound_event_time.h"
#include "Network/udp_sound_event.h"
#include "Measure/measurement_result.h"
#include "Measure/measurement_gate.h"
#include "Calibration/calibration.h"
#include "Measure/result_exchange_state.h"
#include "Detector/standard_sequence.h"
#include "Sync/time_domain.h"
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
/** @brief App测试桩：无待取窗口TOA事件，返回false。 */
bool SoundEventTime_TakeEvent(SoundEventTimeEvent *event){if(event)memset(event,0,sizeof *event);return false;}
/** @brief App测试桩：初始化UDP事件服务不访问网络。 */
bool UDPSoundEvent_Init(uint32_t now,const BoardIdentityProfile *profile,
                        uint32_t (*tick)(void),UDPSoundEvent_Log log)
{(void)now;(void)profile;(void)tick;(void)log;return true;}
/** @brief App测试桩：不推进UDP事件配对。 */
void UDPSoundEvent_Process(uint32_t now){(void)now;}
/** @brief App测试桩：不发送UDP事件。 */
bool UDPSoundEvent_Send(uint32_t now,const SoundEventRecord *event){(void)now;(void)event;return true;}
/** @brief App测试桩：清空权威测量缓存。 */
void MeasurementResult_Init(uint32_t now){(void)now;}
/** @brief App测试桩：不生成权威测量。 */
bool MeasurementResult_PublishPair(const SoundPairResult *pair,const TemperatureSnapshot *temperature,uint32_t now){(void)pair;(void)temperature;(void)now;return false;}
/** @brief App测试桩：不安装结果消费者。 */
void UDPSoundEvent_SetResultSink(UDPSoundEvent_ResultSink sink){(void)sink;}


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
#ifndef SBCG_TEST_SKIP_LEGACY_UI_MAIN

/** @brief App测试桩：无有效温度快照，测量结果不能显示。 */
void TemperatureService_GetSnapshot(uint32_t now,TemperatureSnapshot *out){(void)now;if(out)memset(out,0,sizeof *out);}

bool TemperatureService_SetRemote(uint32_t now,int32_t temperature_milli_c,
                                  uint32_t sound_speed_mm_s,uint32_t version,bool manual)
{(void)now;(void)temperature_milli_c;(void)sound_speed_mm_s;(void)version;(void)manual;return true;}

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

/** @brief App测试桩：安装权威结果消费者。 */
void UDPSoundEvent_SetMeasurementSink(UDPSoundEvent_MeasurementSink sink){(void)sink;}
/** @brief App测试桩：A→B权威结果发送。 */
bool UDPSoundEvent_SendResult(uint32_t now,const MeasurementResultSnapshot *result){(void)now;(void)result;return true;}
/** @brief App测试桩：接收并发布A板权威结果。 */
bool MeasurementResult_PublishRemote(const MeasurementResultSnapshot *result,uint32_t now){(void)result;(void)now;return false;}
/** @brief App测试桩：不读取权威结果。 */
bool MeasurementResult_Get(uint32_t now,MeasurementResultSnapshot *out){(void)now;(void)out;return false;}

/** @brief App测试桩：设置A域令牌。 */
void TimeDomain_InitWithId(uint32_t id){(void)id;}
/** @brief App测试桩：采用对端域令牌。 */
void TimeDomain_AdoptPeerId(uint32_t id){(void)id;}
/** @brief App测试桩：安装对端事件消费者。 */
void UDPSoundEvent_SetEventSink(UDPSoundEvent_EventSink sink){(void)sink;}

/** @brief App测试桩：安装共同域消费者。 */
void UDPSoundEvent_SetDomainSink(UDPSoundEvent_DomainSink sink){(void)sink;}
/** @brief App测试桩：发送共同域REQ。 */
bool UDPSoundEvent_SendDomainRequest(uint32_t now,uint32_t token){(void)now;(void)token;return true;}
/** @brief App测试桩：发送共同域ACK。 */
bool UDPSoundEvent_SendDomainAck(uint32_t now,uint32_t token){(void)now;(void)token;return true;}
/** @brief App测试桩：接收对端REQ。 */
bool TimeDomain_OnPeerRequest(uint32_t token,uint32_t now){(void)token;(void)now;return false;}
/** @brief App测试桩：接收对端ACK。 */
bool TimeDomain_OnPeerAck(uint32_t token,uint32_t now){(void)token;(void)now;return false;}
/** @brief App测试桩：域超时检查。 */
void TimeDomain_Tick(uint32_t now){(void)now;}

/** @brief App测试桩：安装MACK消费者。 */
void UDPSoundEvent_SetAckSink(UDPSoundEvent_AckSink sink){(void)sink;}

void UDPSoundEvent_SetTemperatureSink(UDPSoundEvent_TemperatureSink sink){(void)sink;}
bool UDPSoundEvent_SendTemperature(uint32_t now,const TemperatureSnapshot *temperature)
{(void)now;(void)temperature;return true;}
/** @brief App测试桩：B发送MACK。 */
bool UDPSoundEvent_SendMeasurementAck(uint32_t now,uint32_t id,uint32_t domain)
{(void)now;(void)id;(void)domain;return true;}

/** @brief App测试桩：初始化测量门控。 */
void MeasurementGate_Init(uint32_t now){(void)now;}
/** @brief App测试桩：提交CHIRP。 */
void MeasurementGate_OnChirp(uint32_t now,uint32_t session){(void)now;(void)session;}
/** @brief App测试桩：标准序列形成。 */
void MeasurementGate_OnSequence(uint32_t now){(void)now;}
/** @brief App测试桩：记录测量。 */
bool MeasurementGate_OnMeasurement(uint32_t now){(void)now;return true;}
/** @brief App测试桩：域失效。 */
void MeasurementGate_OnDomainInvalid(uint32_t now){(void)now;}
/** @brief App测试桩：域恢复。 */
void MeasurementGate_OnDomainValid(uint32_t now){(void)now;}
/** @brief App测试桩：流失效。 */
void MeasurementGate_OnStreamInvalid(uint32_t now){(void)now;}
/** @brief App测试桩：门控周期。 */
void MeasurementGate_Tick(uint32_t now){(void)now;}
/** @brief App测试桩：允许发布。 */
bool MeasurementGate_CanPublish(const MeasurementGateSnapshot *s){(void)s;return true;}
/** @brief App测试桩：无失效请求。 */
bool MeasurementGate_TakeInvalidation(void){return false;}
/** @brief App测试桩：复制空状态。 */
void MeasurementGate_Get(MeasurementGateSnapshot *out){if(out)memset(out,0,sizeof *out);}

/** @brief App测试桩：初始化标定。 */
void Calibration_Init(void){}
/** @brief App测试桩：读取零标定。 */
void Calibration_Get(CalibrationSnapshot *out){if(out)memset(out,0,sizeof *out);}
/** @brief App测试桩：设置结果标定。 */
void MeasurementResult_SetCalibration(int32_t offset,uint16_t version){(void)offset;(void)version;}

/** @brief App测试桩：初始化结果重发状态。 */
void ResultExchange_Init(ResultExchangeState *s){if(s)memset(s,0,sizeof *s);}
/** @brief App测试桩：域恢复。 */
void ResultExchange_OnDomainRestored(ResultExchangeState *s){(void)s;}
/** @brief App测试桩：ACK序号。 */
void ResultExchange_OnPeerAckSequence(ResultExchangeState *s,uint32_t q){(void)s;(void)q;}
/** @brief App测试桩：成功ACK。 */
void ResultExchange_OnAck(ResultExchangeState *s,uint32_t id){(void)s;(void)id;}
/** @brief App测试桩：允许发送。 */
bool ResultExchange_ShouldSend(const ResultExchangeState *s,uint32_t now,uint32_t id)
{(void)s;(void)now;(void)id;return true;}
/** @brief App测试桩：记录发送。 */
void ResultExchange_MarkSent(ResultExchangeState *s,uint32_t now,uint32_t id)
{(void)s;(void)now;(void)id;}

/** @brief App测试桩：设置/撤销当前声音事件域。 */
void UDPSoundEvent_SetTimeDomain(uint32_t id){(void)id;}
int main(void)
{
 memcpy(info_uid,BoardIdentity_DefaultMap.a_uid,sizeof info_uid);
 assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&identity));
 Reset(0,true);assert(display_calls==1U&&sdram_init_calls==0U&&ui_inits==1U);
 tick_ms=1000;App_Process();assert(ui_calls==1U&&sdram_steps==0U&&write_count==2U);
 start_low_in_process=true;tick_ms=2000;App_Process();assert(ui_calls==2U&&sdram_steps==0U&&write_count==2U);
 puts("PASS App display init before audio, UI after audio, no destructive test, DHT gate and one UART per loop");return 0;
}
#endif
