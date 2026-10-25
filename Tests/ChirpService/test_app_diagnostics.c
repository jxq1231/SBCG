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
/** @brief 验证诊断字段及原有排队语义，成功返回0。 */
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
