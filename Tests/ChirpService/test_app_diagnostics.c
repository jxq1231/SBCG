/**
 * @file test_app_diagnostics.c
 * @brief 通过真实App日志出口区分队列满、发送失败和非法长度。
 * @details 复用原应用服务桩；启用与板端相同的统一日志排队策略。
 * @note 不访问串口或开发板，不改变原App测试。
 */
#define main unused_app_main
#include "../App/test_app_main.c"
#undef main
#include "Detector/sound_event_time.h"
#include "Network/udp_sound_event.h"
#include "Measure/measurement_result.h"
#include "Detector/standard_sequence.h"
#include "Sync/time_domain.h"
/** @brief 空锚点源，out借用不写，返回false。 */
bool AudioTimeAnchor_TakeObservation(AudioTimeAnchorObservation *out){(void)out;return false;}
/** @brief 未投递锚点的桩，参数借用不保存，无返回值。 */
void AudioTimeMapMonitor_Observe(uint32_t now,const AudioTimeAnchorObservation *out){(void)now;(void)out;}
/** @brief 初始化事件桩，log须非NULL，无返回值。 */
void SoundEventTime_Init(void (*log)(const char *,int)){assert(log!=NULL);}
/** @brief 事件观察桩，不保存输入，无返回值。 */
void SoundEventTime_Observe(uint32_t now,const AudioTimeAnchorObservation *out){(void)now;(void)out;}
/** @brief 输出由测试拥有的采集状态，无返回值。 */
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *out){*out=(BSP_AudioSnapshot){0};}
/** @brief 前台事件桩，不读PCM或输出日志，无返回值。 */
void SoundEventTime_Process(uint32_t now,const BSP_AudioSnapshot *out){(void)now;(void)out;}
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


/** @brief 验证诊断字段及原有排队语义，成功返回0。 */

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

/** @brief App测试桩：安装权威结果消费者。 */
void UDPSoundEvent_SetMeasurementSink(UDPSoundEvent_MeasurementSink sink){(void)sink;}
/** @brief App测试桩：A→B权威结果发送。 */
bool UDPSoundEvent_SendResult(uint32_t now,const MeasurementResultSnapshot *result){(void)now;(void)result;return true;}
/** @brief App测试桩：接收并发布A板权威结果。 */
bool MeasurementResult_PublishRemote(const MeasurementResultSnapshot *result,uint32_t now){(void)result;(void)now;return false;}
/** @brief App测试桩：不读取权威结果。 */
bool MeasurementResult_Get(uint32_t now,MeasurementResultSnapshot *out){(void)now;(void)out;return false;}
int main(void)
{
 Reset(0U,true);temperature_low=true;
#ifdef SBCG_USE_SOUND_TIME
 const unsigned capacity=32U;
#else
 const unsigned capacity=16U;
#endif
 for(unsigned i=0U;i<capacity+2U;++i)time_log("$TEST\r\n",7);
 temperature_low=false;for(unsigned i=0U;i<capacity;++i)App_Process();
 tick_ms=1000U;App_Process();
 char expected[160];snprintf(expected,sizeof expected,
     "TX_ERRORS=2,LOG_FULL=2,LOG_WRITE=0,LOG_BAD=0,LOG_Q=0,LOG_HWM=%u",capacity);
 assert(strstr(last_line,expected)!=NULL);
 /* A2同循环归因字段只在CHIRP心跳追加，原字段仍可核对。 */
 assert(strstr(last_line,",LOOP_MS=")!=NULL && strstr(last_line,",AUDIO_MS=")!=NULL);
 assert(strstr(last_line,",UI_MS=")!=NULL && strstr(last_line,",OTHER_MS=")!=NULL);
 Reset(0U,true);temperature_low=true;time_log("$TEST\r\n",7);
 temperature_low=false;write_ok=false;App_Process();
 write_ok=true;tick_ms=1000U;App_Process();
 assert(strstr(last_line,"TX_ERRORS=1,LOG_FULL=0,LOG_WRITE=1,LOG_BAD=0")!=NULL);
 Reset(0U,true);time_log("",0);time_log("",256);tick_ms=1000U;App_Process();
 assert(strstr(last_line,"TX_ERRORS=2,LOG_FULL=0,LOG_WRITE=0,LOG_BAD=2")!=NULL);
 Reset(0U,true);tick_ms=1000U;App_Process();
 assert(strstr(last_line,"TX_ERRORS=0,LOG_FULL=0,LOG_WRITE=0,LOG_BAD=0")!=NULL);
 Reset(0U,true);temperature_low=true;time_log("$A\r\n",4);time_log("$B\r\n",4);
 temperature_low=false;tick_ms=1000U;App_Process();App_Process();App_Process();
 assert(strstr(last_line,",LOG_Q=2,LOG_HWM=2")!=NULL);
#ifndef SBCG_USE_SOUND_TIME
 Reset(0U,true);write_ok=false;time_log("$TEST\r\n",7);write_ok=true;
 tick_ms=1000U;App_Process();assert(strstr(last_line,"TX_ERRORS=1,LOG_FULL=0,LOG_WRITE=1")!=NULL);
#endif
 puts("PASS App diagnostics: full/write/length, DHT gate, queue depth/high-water, reset and direct/deferred policy");return 0;
}
