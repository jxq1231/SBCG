/** @file test_app_udp_integration.c @brief app_main结果交换接线测试；UDP发送边界用桩。 */
#define main unused_app_main
#include "Tests/App/test_app_main.c"
#undef main
#include "Network/udp_sound_event.h"
#include "Sync/time_sync.h"
#include "Sync/time_domain.h"
#include "Detector/sound_event_time.h"
#include "BSP/bsp_board_identity.h"
#include <stdio.h>

/* Host test hooks exported by app_main.c under SBCG_HOST_TEST. */
void App_Test_ServiceAuthoritativeResult(uint32_t now_ms);
void App_Test_HandleMeasurementAck(const MeasurementAck *ack);
void App_Test_HandleDomainMessage(const DomainMessage *message);

static BoardIdentityProfile identity;
static bool send_ok=true;
static MeasurementResultSnapshot sent_result;
static unsigned result_sends;
static uint32_t now_ms;

const BoardIdentityProfile *BSP_BoardIdentity_Get(void){return &identity;}
bool TimeSync_Deinit(void){return true;}
bool TimeSync_Init(uint32_t now,const BoardIdentityProfile*p,uint32_t(*tick)(void),void(*log)(const char*,int))
{(void)now;(void)p;(void)tick;(void)log;return true;}
void TimeSync_Process(uint32_t now,bool allowed){(void)now;(void)allowed;}
void TimeSync_GetSnapshot(uint32_t now,TimeSyncSnapshot *out)
{(void)now;if(out){*out=(TimeSyncSnapshot){0};out->configured=true;out->role=BOARD_ROLE_A;}}

void SoundEventTime_Init(void(*log)(const char*,int)){(void)log;}
void SoundEventTime_Observe(uint32_t now,const AudioTimeAnchorObservation*out){(void)now;(void)out;}
void SoundEventTime_Process(uint32_t now,const BSP_AudioSnapshot*out){(void)now;(void)out;}
bool SoundEventTime_TakeEvent(SoundEventTimeEvent*out){if(out)memset(out,0,sizeof *out);return false;}
bool AudioTimeAnchor_TakeObservation(AudioTimeAnchorObservation*out){if(out)memset(out,0,sizeof *out);return false;}
void AudioTimeMapMonitor_Observe(uint32_t now,const AudioTimeAnchorObservation*obs){(void)now;(void)obs;}
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot*out)
{if(out){*out=(BSP_AudioSnapshot){0};out->running=true;out->session=1U;out->callback_count=1U;}}

/** @brief 结果发送边界桩：记录App实际交给UDP层的结果。 */
bool UDPSoundEvent_SendResult(uint32_t now,const MeasurementResultSnapshot *result)
{(void)now;assert(result!=NULL);sent_result=*result;++result_sends;return send_ok;}
bool UDPSoundEvent_Init(uint32_t now,const BoardIdentityProfile*p,uint32_t(*tick)(void),UDPSoundEvent_Log log)
{(void)now;(void)p;(void)tick;(void)log;return true;}
void UDPSoundEvent_Process(uint32_t now){(void)now;}
bool UDPSoundEvent_Send(uint32_t now,const SoundEventRecord*event){(void)now;(void)event;return true;}
void UDPSoundEvent_SetResultSink(UDPSoundEvent_ResultSink s){(void)s;}
void UDPSoundEvent_SetMeasurementSink(UDPSoundEvent_MeasurementSink s){(void)s;}
void UDPSoundEvent_SetEventSink(UDPSoundEvent_EventSink s){(void)s;}
void UDPSoundEvent_SetDomainSink(UDPSoundEvent_DomainSink s){(void)s;}
void UDPSoundEvent_SetAckSink(UDPSoundEvent_AckSink s){(void)s;}
bool UDPSoundEvent_SendDomainRequest(uint32_t now,uint32_t token){(void)now;(void)token;return true;}
bool UDPSoundEvent_SendDomainAck(uint32_t now,uint32_t token){(void)now;(void)token;return true;}
bool UDPSoundEvent_SendMeasurementAck(uint32_t now,uint32_t id,uint32_t domain)
{(void)now;(void)id;(void)domain;return true;}
void TemperatureService_GetSnapshot(uint32_t now,TemperatureSnapshot *out)
{(void)now;if(out){*out=(TemperatureSnapshot){0};out->valid=true;out->sound_speed_mm_s=343000U;out->version=1U;}}

static void InitApp(void)
{
    tick_ms=0U;
    memcpy(identity.uid_words,info_uid,sizeof info_uid);
    identity.role=BOARD_ROLE_A;
    identity.peer_ip[0]=192U;identity.peer_ip[1]=168U;identity.peer_ip[2]=10U;identity.peer_ip[3]=2U;
    App_Init();
}
static void SetDomain(uint32_t token,uint32_t at)
{
    TimeSyncSnapshot sync={0};
    sync.configured=true;sync.role=BOARD_ROLE_A;
    TimeDomain_InitWithId(token);
    TimeDomain_Update(&sync);
    (void)TimeDomain_OnPeerAck(token,at);
}
static void Publish(uint32_t now,uint32_t id_delta)
{
    SoundPairResult pair={0};TemperatureSnapshot temp={0};
    temp.valid=true;temp.sound_speed_mm_s=343000U;temp.version=1U;
    pair.valid=true;pair.status=SOUND_PAIR_OK;pair.time_domain=0x1234U;
    pair.quality=80U;pair.direction=SOUND_DIRECTION_A;
    pair.delta_ns=1000000U+(int64_t)id_delta;
    pair.a_sequence=1U;pair.b_sequence=2U;pair.sound_speed_mm_s=343000U;
    assert(MeasurementResult_PublishPair(&pair,&temp,now));
}
int main(void)
{
    InitApp();
    SetDomain(0x1234U,10U);
    Publish(100U,0U);
    now_ms=100U;
    App_Test_ServiceAuthoritativeResult(now_ms);
    assert(result_sends==1U && sent_result.id==1U);
    MeasurementAck ack={MEASUREMENT_ACK_OK,1U,0x1234U};
    App_Test_HandleMeasurementAck(&ack);
    App_Test_ServiceAuthoritativeResult(now_ms+1U);
    assert(result_sends==1U);
    /* B restart: ACK sequence regresses -> current result resent. */
    DomainMessage d={DOMAIN_MESSAGE_ACK,BOARD_ROLE_B,DOMAIN_LOCAL_VALID,0x1234U,0x1234U,3U};
    App_Test_HandleDomainMessage(&d);
    d.sequence=1U;App_Test_HandleDomainMessage(&d);
    App_Test_ServiceAuthoritativeResult(now_ms+2U);
    assert(result_sends==2U);
    /* Failed send is throttled by the 500ms result cadence. */
    Publish(1000U,1U);
    send_ok=false;
    App_Test_ServiceAuthoritativeResult(1000U);
    App_Test_ServiceAuthoritativeResult(1001U);
    assert(result_sends==3U);
    send_ok=true;
    App_Test_ServiceAuthoritativeResult(1500U);
    assert(result_sends==4U);
    /* Domain loss invalidates; recovery must not resend the old result. */
    TimeDomain_Invalidate();
    App_Test_ServiceAuthoritativeResult(2000U);
    MeasurementResultSnapshot out;
    assert(!MeasurementResult_Get(2000U,&out));
    unsigned before=result_sends;
    SetDomain(0x1234U,2100U);
    App_Test_ServiceAuthoritativeResult(2100U);
    assert(result_sends==before);
    puts("PASS app+UDP boundary: send/ACK/restart resend, failure throttle, domain-loss invalidation");
    return 0;
}
