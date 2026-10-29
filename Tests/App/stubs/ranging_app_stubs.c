/**
 * @file        ranging_app_stubs.c
 * @brief       App级主机回归的RSC2/RST2/权威展示/PTP/以太网链接桩。
 * @details     仅供Tests/{UI,Sdram,SoundEventTime,ChirpService}的app_*测试链接app_main.c使用；
 *              全部返回未建立状态，测试自身仍直接断言被测App/Sync/UI路径。
 * @note        不包含任何业务逻辑；真实构建不使用本文件。
 */
#include "BSP/bsp_ptp.h"
#include "Measure/measurement_result.h"
#include "Measure/result_exchange_state.h"
#include "Network/ethernet_monitor.h"
#include "Network/ranging_session.h"
#include "Network/udp_sound_event.h"
#include <string.h>

void RangingSession_Init(RangingSession *self,BoardRole role)
{ if(self!=NULL){memset(self,0,sizeof *self);self->role=role;} }
void RangingSession_Update(RangingSession *self,bool domain_valid,uint32_t domain,
                           uint32_t local_clock_epoch,bool common_read_ok,
                           uint64_t now_common_ns,uint32_t now_ms)
{ (void)self;(void)domain_valid;(void)domain;(void)local_clock_epoch;
  (void)common_read_ok;(void)now_common_ns;(void)now_ms; }
void RangingSession_Get(const RangingSession *self,RangingSessionSnapshot *out)
{ (void)self;if(out!=NULL)memset(out,0,sizeof *out); }
bool RangingSession_OnMessage(RangingSession *self,const RangingSessionMessage *message,
                              uint32_t now_ms)
{ (void)self;(void)message;(void)now_ms;return false; }
bool RangingSession_TakeContextChanged(RangingSession *self)
{ (void)self;return false; }
bool RangingSession_TakeOutgoing(RangingSession *self,uint32_t now_ms,
                                 RangingSessionMessage *out)
{ (void)self;(void)now_ms;(void)out;return false; }

void RangingExchange_Init(RangingExchangeState *s){if(s!=NULL)memset(s,0,sizeof *s);}
bool RangingExchange_ShouldSend(const RangingExchangeState *s,uint32_t now_ms,
                                const RangingStatusAck *key)
{ (void)s;(void)now_ms;(void)key;return false; }
void RangingExchange_MarkAttempt(RangingExchangeState *s,uint32_t now_ms,
                                 const RangingStatusAck *key,bool sent)
{ (void)s;(void)now_ms;(void)key;(void)sent; }
bool RangingExchange_OnAck(RangingExchangeState *s,const RangingStatusAck *ack)
{ (void)s;(void)ack;return false; }

void MeasurementResult_Update(const MeasurementContext *context,
                              const TemperatureSnapshot *temperature,
                              uint32_t now_ms,uint64_t now_common_ns)
{ (void)context;(void)temperature;(void)now_ms;(void)now_common_ns; }
RangingDecision MeasurementResult_PublishTrackedPair(const SoundPairResult *pair,
                                                     const TemperatureSnapshot *temperature,
                                                     bool formal,uint32_t now_ms,
                                                     uint64_t now_common_ns)
{ (void)pair;(void)temperature;(void)formal;(void)now_ms;(void)now_common_ns;
  return RANGING_DECISION_REJECT; }
void MeasurementResult_GetPresentation(MeasurementPresentationSnapshot *out)
{ if(out!=NULL)memset(out,0,sizeof *out); }
bool MeasurementResult_PublishRemotePresentation(const MeasurementPresentationSnapshot *remote,
                                                 uint32_t time_domain,uint32_t now_ms)
{ (void)remote;(void)time_domain;(void)now_ms;return false; }

void EthernetMonitor_GetSnapshot(uint32_t now_ms,EthernetMonitorSnapshot *out)
{ (void)now_ms;if(out!=NULL)memset(out,0,sizeof *out); }
uint32_t BSP_PTP_GetEpoch(void){return 0U;}
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *time)
{ (void)time;return BSP_PTP_NOT_READY; }

void UDPSoundEvent_SetRangingSessionSink(UDPSoundEvent_RangingSessionSink sink){(void)sink;}
bool UDPSoundEvent_SendRangingSession(uint32_t now_ms,const RangingSessionMessage *message)
{ (void)now_ms;(void)message;return false; }
