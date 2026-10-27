/**
 * @file        test_app_udp_integration.c
 * @brief       验证真实App调度中的事件域接线及结果交换。
 * @details     使用真实域、序列、测量门控和结果模块，观察UDP边界调用与撤域顺序。
 * @note        UDP服务由边界桩替代；真实UDP逻辑由Tests/Udp独立测试，不证明硬件行为。
 */
#define main unused_app_main
#include "Tests/App/test_app_main.c"
#undef main
#include "Network/udp_sound_event.h"
#include "Sync/time_sync.h"
#include "Sync/time_domain.h"
#include "Detector/sound_event_time.h"
#include "Detector/event_classifier.h"
#include "Detector/standard_sequence.h"
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
static uint32_t udp_domain;
static unsigned domain_updates,domain_before_loop,event_sends,udp_processes;
static bool sync_faulted,event_pending,check_app_order,pair_pending;
static SoundEventTimeEvent next_event;
static SoundPairResult next_pair;
static UDPSoundEvent_ResultSink pair_sink;
static uint32_t audio_session=1U;
static unsigned domain_revocations;
static unsigned domain_requests,event_takes;

const BoardIdentityProfile *BSP_BoardIdentity_Get(void){return &identity;}
bool TimeSync_Deinit(void){return true;}
bool TimeSync_Init(uint32_t now,const BoardIdentityProfile*p,uint32_t(*tick)(void),void(*log)(const char*,int))
{(void)now;(void)p;(void)tick;(void)log;return true;}
void TimeSync_Process(uint32_t now,bool allowed){(void)now;(void)allowed;}
/** @brief 提供可控同步资格；now为本轮毫秒，out非NULL时写A板状态，无返回值。 */
void TimeSync_GetSnapshot(uint32_t now,TimeSyncSnapshot *out)
{(void)now;if(out){*out=(TimeSyncSnapshot){0};out->configured=true;out->role=identity.role;out->offset_valid=true;out->internal_locked=true;out->faulted=sync_faulted;}}

void SoundEventTime_Init(void(*log)(const char*,int)){(void)log;}
void SoundEventTime_Observe(uint32_t now,const AudioTimeAnchorObservation*out){(void)now;(void)out;}
void SoundEventTime_Process(uint32_t now,const BSP_AudioSnapshot*out){(void)now;(void)out;}
/** @brief 交付至多一个只读标量事件，out不可NULL；先检查本轮已同步域，返回是否有事件。 */
bool SoundEventTime_TakeEvent(SoundEventTimeEvent*out)
{
    assert(out!=NULL && !temperature_low);++event_takes;
    if(check_app_order){assert(domain_updates>domain_before_loop);}
    if(event_pending){*out=next_event;event_pending=false;return true;}
    memset(out,0,sizeof *out);return false;
}
bool AudioTimeAnchor_TakeObservation(AudioTimeAnchorObservation*out){if(out)memset(out,0,sizeof *out);return false;}
void AudioTimeMapMonitor_Observe(uint32_t now,const AudioTimeAnchorObservation*obs){(void)now;(void)obs;}
/** @brief 提供可控音频会话；out可NULL，无返回值，不提供PCM读取权。 */
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot*out)
{if(out){*out=(BSP_AudioSnapshot){0};out->running=true;out->session=audio_session;out->callback_count=1U;}}

/** @brief 结果发送边界桩：记录App实际交给UDP层的结果。 */
bool UDPSoundEvent_SendResult(uint32_t now,const MeasurementResultSnapshot *result)
{(void)now;assert(result!=NULL);sent_result=*result;++result_sends;return send_ok;}
bool UDPSoundEvent_Init(uint32_t now,const BoardIdentityProfile*p,uint32_t(*tick)(void),UDPSoundEvent_Log log)
{(void)now;(void)p;(void)tick;(void)log;return true;}
/** @brief 记录App传来的有效域；id为0表示撤域，无返回值，不代替真实UDP清队逻辑。 */
void UDPSoundEvent_SetTimeDomain(uint32_t id){udp_domain=id;++domain_updates;if(id==0U)++domain_revocations;}
/** @brief 检查本轮域已先同步，再按需交付配对结果；now为本轮毫秒，无返回值。 */
void UDPSoundEvent_Process(uint32_t now)
{
    (void)now;++udp_processes;
    if(check_app_order)assert(domain_updates>domain_before_loop);
    if(pair_pending){assert(pair_sink!=NULL);pair_pending=false;pair_sink(&next_pair);}
}
/** @brief 观察真实App声音发布，event不可NULL且必须匹配已同步有效域；now为毫秒，返回true。 */
bool UDPSoundEvent_Send(uint32_t now,const SoundEventRecord*event)
{(void)now;assert(event!=NULL && udp_domain!=0U && event->time_domain==udp_domain);++event_sends;return true;}
/** @brief 保存真实App注册的前台结果消费者；s可NULL，无返回值。 */
void UDPSoundEvent_SetResultSink(UDPSoundEvent_ResultSink s){pair_sink=s;}
void UDPSoundEvent_SetMeasurementSink(UDPSoundEvent_MeasurementSink s){(void)s;}
void UDPSoundEvent_SetEventSink(UDPSoundEvent_EventSink s){(void)s;}
void UDPSoundEvent_SetDomainSink(UDPSoundEvent_DomainSink s){(void)s;}
void UDPSoundEvent_SetAckSink(UDPSoundEvent_AckSink s){(void)s;}

void UDPSoundEvent_SetTemperatureSink(UDPSoundEvent_TemperatureSink sink){(void)sink;}
bool UDPSoundEvent_SendTemperature(uint32_t now,const TemperatureSnapshot *temperature)
{(void)now;(void)temperature;return true;}
void UDPSoundEvent_SetRangingStatusSink(UDPSoundEvent_RangingStatusSink sink){(void)sink;}
void UDPSoundEvent_SetRangingAckSink(UDPSoundEvent_RangingAckSink sink){(void)sink;}
bool UDPSoundEvent_SendRangingStatus(uint32_t now,const RangingStatusMessage *message){(void)now;(void)message;return false;}
bool UDPSoundEvent_SendRangingAck(uint32_t now,const RangingStatusAck *ack){(void)now;(void)ack;return false;}
/** @brief 观察主动DOMAIN发送；now为毫秒、token为域，DHT低态禁止调用，成功返回true。 */
bool UDPSoundEvent_SendDomainRequest(uint32_t now,uint32_t token)
{(void)now;(void)token;assert(!temperature_low);++domain_requests;return true;}
bool UDPSoundEvent_SendDomainAck(uint32_t now,uint32_t token){(void)now;(void)token;return true;}
bool UDPSoundEvent_SendMeasurementAck(uint32_t now,uint32_t id,uint32_t domain)
{(void)now;(void)id;(void)domain;return true;}
void TemperatureService_GetSnapshot(uint32_t now,TemperatureSnapshot *out)
{(void)now;if(out){*out=(TemperatureSnapshot){0};out->valid=true;out->sound_speed_mm_s=343000U;out->version=1U;}}

bool TemperatureService_SetRemote(uint32_t now,int32_t temperature_milli_c,
                                  uint32_t sound_speed_mm_s,uint32_t version,bool manual)
{(void)now;(void)temperature_milli_c;(void)sound_speed_mm_s;(void)version;(void)manual;return true;}

/** @brief 用A身份初始化真实App及原有主机外设桩；无参数、无返回值。 */
static void InitApp(void)
{
    tick_ms=0U;
    memcpy(identity.uid_words,info_uid,sizeof info_uid);
    identity.role=BOARD_ROLE_A;
    identity.peer_ip[0]=192U;identity.peer_ip[1]=168U;identity.peer_ip[2]=10U;identity.peer_ip[3]=2U;
    Reset(0U,true);
}
/** @brief 以当前板角色完成真实时间域握手；token为非零域，at为毫秒，无返回值。 */
static void SetDomain(uint32_t token,uint32_t at)
{
    TimeSyncSnapshot sync={0};
    sync.configured=true;sync.role=identity.role;sync.offset_valid=true;sync.internal_locked=true;
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

/**
 * @brief 运行一轮真实App，验证域同步早于事件取出、发送及配对处理。
 * @param at 本轮单调毫秒。
 * @param sequence 非零时提供一条该序号CHIRP，0表示无事件。
 * @return 无返回值；模型时间按真实标准声2s周期构造，便于建立序列资格。
 */
static void ProcessApp(uint32_t at,uint32_t sequence)
{
    tick_ms=at;domain_before_loop=domain_updates;
    next_event=(SoundEventTimeEvent){0};
    next_event.sequence=sequence;next_event.session=audio_session;next_event.quality=80U;
    next_event.event_class=EVENT_CLASS_CHIRP;
    next_event.time_ns=(uint64_t)sequence*STANDARD_SEQUENCE_PERIOD_NS;
    event_pending=sequence!=0U;
    App_Process();
}

/** @brief 检查真实App实际调度和配对发布对当前域的门控；无参数、无返回值。 */
static void TestAppDomainWiring(void)
{
    check_app_order=true;
    SetDomain(0x1234U,10U);
    ProcessApp(100U,1U);
    assert(udp_domain==0x1234U && event_sends==1U && udp_processes==1U);
    ProcessApp(1100U,2U);
    ProcessApp(2100U,3U);
    assert(event_sends==3U);
    next_pair=(SoundPairResult){0};next_pair.valid=true;next_pair.status=SOUND_PAIR_OK;
    next_pair.time_domain=0x1234U;next_pair.delta_ns=1000000U;
    next_pair.direction=SOUND_DIRECTION_A;next_pair.quality=80U;
    pair_pending=true;ProcessApp(2150U,0U);
    MeasurementResultSnapshot result;
    assert(MeasurementResult_Get(2150U,&result));
    next_pair.time_domain=0x5678U;
    pair_pending=true;ProcessApp(2200U,0U);
    assert(!MeasurementResult_Get(2200U,&result));
    assert(pair_sink!=NULL);pair_sink(NULL);
    assert(!MeasurementResult_Get(2200U,&result));
    sync_faulted=true;
    ProcessApp(2250U,4U);
    assert(udp_domain==0U && event_sends==3U && !MeasurementResult_Get(2250U,&result));
    sync_faulted=false;
    SetDomain(0x1234U,2300U);
    ProcessApp(2300U,0U);
    assert(udp_domain==0x1234U && event_sends==3U);
    unsigned revoked=domain_revocations;
    audio_session=2U;
    ProcessApp(2350U,0U);
    assert(domain_revocations==revoked+1U && udp_domain==0x1234U);
    identity.role=BOARD_ROLE_B;
    SetDomain(0x1234U,2400U);
    revoked=domain_revocations;
    audio_session=3U;
    ProcessApp(2400U,0U);
    assert(domain_revocations==revoked+1U && udp_domain==0x1234U);
    ProcessApp(5400U,0U);
    assert(udp_domain==0U);
}

/**
 * @brief 验证DHT低态仍逐轮同步撤域和同令牌恢复，并保持音频和主动DOMAIN发送门控。
 * @return 无返回值；使用真实App_Process、真实TimeDomain和可观察UDP边界，不替代UDP清队测试。
 */
static void TestDomainDuringDhtLow(void)
{
    identity.role=BOARD_ROLE_A;
    SetDomain(0x1234U,6000U);
    ProcessApp(6000U,0U);
    assert(udp_domain==0x1234U);
    const unsigned revoked=domain_revocations;
    const unsigned takes_before=event_takes,processed_before=udp_processes;
    const unsigned sent_before=event_sends,requests_before=domain_requests;
    const unsigned audio_before=audio_allowed,anchors_before=anchor_allowed;
    temperature_low=true;sync_faulted=true;
    ProcessApp(6010U,0U);
    assert(udp_domain==0U && domain_revocations==revoked+1U &&
           domain_updates>domain_before_loop);
    sync_faulted=false;
    SetDomain(0x1234U,6020U);
    ProcessApp(6020U,0U);
    assert(udp_domain==0x1234U && domain_revocations==revoked+1U &&
           domain_updates>domain_before_loop);
    assert(event_takes==takes_before && udp_processes==processed_before &&
           event_sends==sent_before && domain_requests==requests_before &&
           audio_allowed==audio_before && anchor_allowed==anchors_before);
    temperature_low=false;
}

/** @brief 验证首对高质量CHIRP在标准序列确认前可临时显示；无参数、无返回值。 */
static void TestProvisionalFirstResult(void)
{
    InitApp();SetDomain(0x1234U,10U);
    ProcessApp(100U,1U);
    ProcessApp(1100U,2U); /* 两次事件尚未形成3次标准序列 */
    next_pair=(SoundPairResult){0};next_pair.valid=true;next_pair.status=SOUND_PAIR_OK;
    next_pair.time_domain=0x1234U;next_pair.delta_ns=1000000U;
    next_pair.direction=SOUND_DIRECTION_A;next_pair.quality=95U;
    pair_pending=true;ProcessApp(1200U,0U);
    MeasurementResultSnapshot result;
    assert(MeasurementResult_Get(1200U,&result));
    assert(result.direction==SOUND_DIRECTION_A);
    puts("PASS provisional first result: high-quality first pair displays before 3-event sequence");
}

/** @brief 运行App结果交换及域同步测试；无参数，成功返回0。 */
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
    TestAppDomainWiring();
    TestDomainDuringDhtLow();
    TestProvisionalFirstResult();
    puts("PASS app+UDP boundary: send/ACK/restart resend, failure throttle, domain before events/process, wrong/null pair rejected, A/B session reset, timeout, DHT-low revoke/restore");
    return 0;
}
