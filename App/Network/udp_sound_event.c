/**
 * @file        udp_sound_event.c
 * @brief       两板声音事件UDP发布/接收与配对结果输出的前台实现。
 * @details     固定36字节线协议、来源IP/端口与事件侧校验；配对纯核心输出距离/方向。
 * @note        只证明软件链路；不建立PTP同步、不把接收时刻当声音到达、不宣称物理精度。
 */
#include "udp_sound_event.h"
#include "sound_event_wire.h"
#include "measurement_wire.h"
#include "udp_config.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

extern struct netif gnetif;
static struct udp_pcb *service_pcb;
static BoardIdentityProfile local_profile;
static ip_addr_t peer;
static uint32_t (*read_tick)(void);
static UDPSoundEvent_Log emit;
static UDPSoundEvent_ResultSink result_sink;
static UDPSoundEvent_EventSink event_sink;
static UDPSoundEvent_MeasurementSink measurement_sink;
static SoundPairManager pair_manager;
static uint32_t last_bind_ms, last_state_ms;
static uint32_t tx_ok, tx_err, rx_ok, rx_bad, foreign, init_errors;
static uint32_t result_tx_ok, result_tx_err, result_rx_ok, result_rx_bad;
static bool configured;
static SoundPairConfig pair_config;

/** @brief 饱和计数；无返回值。 */
static void Increment(uint32_t *value){if(*value!=UINT32_MAX)++*value;}
/** @brief 十进制无符号；仅本地格式化，无返回值。 */
static void Decimal(uint64_t value,char out[21])
{
    char reversed[20];unsigned count=0U;
    do{reversed[count++]=(char)('0'+value%10U);value/=10U;}while(value);
    for(unsigned i=0U;i<count;++i)out[i]=reversed[count-i-1U];
    out[count]=0;
}
/** @brief 十进制有符号；仅本地格式化，无返回值。 */
static void Signed(int64_t value,char out[22])
{
    if(value<0){out[0]='-';Decimal((uint64_t)(-(value+1))+1U,out+1);}
    else Decimal((uint64_t)value,out);
}
/** @brief 取得本板side；未配置返回false。 */
static bool LocalSide(SoundEventSide *side)
{
    if(local_profile.role==BOARD_ROLE_A){*side=SOUND_SIDE_A;return true;}
    if(local_profile.role==BOARD_ROLE_B){*side=SOUND_SIDE_B;return true;}
    return false;
}
/** @brief 接收回调：校验来源/长度/事件侧后交给纯配对器；pbuf所有路径释放。 */
static void Receive(void *arg,struct udp_pcb *pcb,struct pbuf *packet,
                    const ip_addr_t *source,u16_t source_port)
{
    (void)arg;(void)pcb;
    if(packet==NULL)return;
    if(source==NULL || !ip_addr_cmp(source,&peer) || source_port!=UDP_SOUND_EVENT_BOARD_PORT)
    {Increment(&foreign);pbuf_free(packet);return;}
    if(packet->tot_len==MEASUREMENT_WIRE_BYTES)
    {
        uint8_t measurement_data[MEASUREMENT_WIRE_BYTES];
        if(pbuf_copy_partial(packet,measurement_data,sizeof measurement_data,0)!=
           sizeof measurement_data)
        {Increment(&rx_bad);Increment(&result_rx_bad);pbuf_free(packet);return;}
        pbuf_free(packet);
        if(local_profile.role!=BOARD_ROLE_B){Increment(&rx_bad);Increment(&result_rx_bad);return;}
        MeasurementResultSnapshot result;
        if(MeasurementWire_Decode(measurement_data,sizeof measurement_data,&result)!=
           MEASUREMENT_WIRE_OK)
        {Increment(&rx_bad);Increment(&result_rx_bad);return;}
        result.captured_ms=read_tick!=NULL?read_tick():0U;
        if(measurement_sink!=NULL){measurement_sink(&result);}
        Increment(&rx_ok);Increment(&result_rx_ok);
        return;
    }
    if(packet->tot_len!=SOUND_EVENT_WIRE_BYTES)
    {Increment(&rx_bad);pbuf_free(packet);return;}
    uint8_t data[SOUND_EVENT_WIRE_BYTES];
    if(pbuf_copy_partial(packet,data,sizeof data,0)!=sizeof data)
    {Increment(&rx_bad);pbuf_free(packet);return;}
    pbuf_free(packet);
    SoundEventRecord event;
    if(SoundEventWire_Decode(data,sizeof data,&event)!=SOUND_EVENT_WIRE_OK)
    {Increment(&rx_bad);return;}
    SoundEventSide local;
    if(!LocalSide(&local) || event.side==local)
    {Increment(&rx_bad);return;}
    const uint32_t now=read_tick!=NULL?read_tick():0U;
    if(!SoundPair_Submit(&pair_manager,&event,now))
    {Increment(&rx_bad);return;}
    if(event_sink!=NULL){event_sink(&event);}
    Increment(&rx_ok);
}
/** @brief 在链路可用时重试绑定；1秒节流，失败不阻塞。 */
static void TryBind(uint32_t now_ms)
{
    if(service_pcb!=NULL || (uint32_t)(now_ms-last_bind_ms)<1000U)return;
    last_bind_ms=now_ms;
    if(!netif_is_up(&gnetif) || !netif_is_link_up(&gnetif))return;
    service_pcb=udp_new();
    if(service_pcb==NULL){Increment(&init_errors);return;}
    if(udp_bind(service_pcb,netif_ip_addr4(&gnetif),UDP_SOUND_EVENT_BOARD_PORT)!=ERR_OK)
    {udp_remove(service_pcb);service_pcb=NULL;Increment(&init_errors);return;}
    udp_bind_netif(service_pcb,&gnetif);
    udp_recv(service_pcb,Receive,NULL);
}
/** @brief 初始化本地身份、配对配置与统计；参数非法返回false。 */
bool UDPSoundEvent_Init(uint32_t now_ms,const BoardIdentityProfile *profile,
                        uint32_t (*tick)(void),UDPSoundEvent_Log log)
{
    if(service_pcb!=NULL){udp_remove(service_pcb);service_pcb=NULL;}
    configured=false;
    if(profile==NULL || tick==NULL || log==NULL ||
       (profile->role!=BOARD_ROLE_A && profile->role!=BOARD_ROLE_B))return false;
    local_profile=*profile;read_tick=tick;emit=log;
    IP_ADDR4(&peer,profile->peer_ip[0],profile->peer_ip[1],
             profile->peer_ip[2],profile->peer_ip[3]);
    pair_config=(SoundPairConfig){40000000U,10000U,343000U,0U,100U};
    if(!SoundPair_Init(&pair_manager,&pair_config))return false;
    last_bind_ms=now_ms-1000U;last_state_ms=now_ms;
    tx_ok=tx_err=rx_ok=rx_bad=foreign=init_errors=0U;
    result_tx_ok=result_tx_err=result_rx_ok=result_rx_bad=0U;
    configured=true;return true;
}
/** @brief 安装配对结果消费者；NULL仅恢复日志输出。 */
void UDPSoundEvent_SetResultSink(UDPSoundEvent_ResultSink sink)
{
    result_sink=sink;
}
/** @brief 安装权威结果消费者；NULL仅丢弃。 */
void UDPSoundEvent_SetMeasurementSink(UDPSoundEvent_MeasurementSink sink)
{
    measurement_sink=sink;
}
/** @brief 安装对端事件消费者；NULL不回调。 */
void UDPSoundEvent_SetEventSink(UDPSoundEvent_EventSink sink)
{
    event_sink=sink;
}
/** @brief A板发送权威结果；非A/未绑定/编码失败返回false并计数。 */
bool UDPSoundEvent_SendResult(uint32_t now_ms,const MeasurementResultSnapshot *result)
{
    (void)now_ms;
    if(!configured || service_pcb==NULL || result==NULL ||
       local_profile.role!=BOARD_ROLE_A || !netif_is_link_up(&gnetif))
    {Increment(&result_tx_err);return false;}
    uint8_t data[MEASUREMENT_WIRE_BYTES];
    if(MeasurementWire_Encode(result,data,sizeof data)!=MEASUREMENT_WIRE_OK)
    {Increment(&result_tx_err);return false;}
    struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,sizeof data,PBUF_RAM);
    if(packet==NULL){Increment(&result_tx_err);return false;}
    err_t err=pbuf_take(packet,data,sizeof data);
    if(err==ERR_OK)err=udp_sendto(service_pcb,packet,&peer,UDP_SOUND_EVENT_BOARD_PORT);
    pbuf_free(packet);
    if(err!=ERR_OK){Increment(&result_tx_err);return false;}
    Increment(&result_tx_ok);return true;
}
/** @brief 发布本板事件；未绑定/编码失败返回false并计数。 */
bool UDPSoundEvent_Send(uint32_t now_ms,const SoundEventRecord *event)
{
    if(!configured || event==NULL){Increment(&tx_err);return false;}
    /* 本端事件必须同时进入配对器；Receive只接收对端，配对器需要A+B两侧。 */
    if(!SoundPair_Submit(&pair_manager,event,now_ms))
    {Increment(&tx_err);return false;}
    if(service_pcb==NULL || !netif_is_link_up(&gnetif))
    {Increment(&tx_err);return false;}
    uint8_t data[SOUND_EVENT_WIRE_BYTES];
    if(SoundEventWire_Encode(event,data,sizeof data)!=SOUND_EVENT_WIRE_OK)
    {Increment(&tx_err);return false;}
    struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,sizeof data,PBUF_RAM);
    if(packet==NULL){Increment(&tx_err);return false;}
    err_t err=pbuf_take(packet,data,sizeof data);
    if(err==ERR_OK)err=udp_sendto(service_pcb,packet,&peer,UDP_SOUND_EVENT_BOARD_PORT);
    pbuf_free(packet);
    if(err!=ERR_OK){Increment(&tx_err);return false;}
    Increment(&tx_ok);return true;
}
/** @brief 推进绑定、配对和1Hz状态/结果输出；无返回值。 */
void UDPSoundEvent_Process(uint32_t now_ms)
{
    if(!configured)return;
    TryBind(now_ms);
    SoundPair_Process(&pair_manager,now_ms);
    SoundPairResult result;
    char line[256];
    while(SoundPair_Take(&pair_manager,&result))
    {
        if(result.valid)
        {
            char delta[22];
            Signed(result.delta_ns,delta);
            const char *dir=result.direction==SOUND_DIRECTION_A?"A":
                            (result.direction==SOUND_DIRECTION_B?"B":"UNKNOWN");
            emit(line,snprintf(line,sizeof line,
                "$SOUND_PAIR,STATUS=OK,DELTA_NS=%s,DIST_MM=%" PRIu32 ",DIR=%s,"
                "A_SEQ=%" PRIu32 ",B_SEQ=%" PRIu32 ",DOMAIN=%" PRIu32 ",QUALITY=%u,"
                "SPEED_MM_S=%" PRIu32 ",DIAG=1,LOCAL=1,SYNC=0\r\n",delta,result.distance_mm,
                dir,result.a_sequence,result.b_sequence,result.time_domain,
                result.quality,result.sound_speed_mm_s));
        }
        else
        {
            emit(line,snprintf(line,sizeof line,
                "$SOUND_PAIR,STATUS=%s,CORE=%u,A_SEQ=%" PRIu32 ",B_SEQ=%" PRIu32
                ",DIAG=1,LOCAL=1,SYNC=0\r\n",result.status==SOUND_PAIR_AMBIGUOUS?"AMBIGUOUS":
                (result.status==SOUND_PAIR_DISTANCE_RANGE?"DIST_RANGE":
                 (result.status==SOUND_PAIR_TDOA_RANGE?"TDOA_RANGE":"REJECT")),
                (unsigned)result.status,result.a_sequence,result.b_sequence));
        }
        if(result_sink!=NULL){result_sink(&result);}
    }
    if((uint32_t)(now_ms-last_state_ms)>=1000U)
    {
        last_state_ms=now_ms;
        SoundPairSnapshot snap;SoundPair_GetSnapshot(&pair_manager,&snap);
        emit(line,snprintf(line,sizeof line,
            "$SOUND_EVENT_STATE,TX_OK=%" PRIu32 ",TX_ERR=%" PRIu32 ",RX_OK=%" PRIu32
            ",RX_BAD=%" PRIu32 ",FOREIGN=%" PRIu32 ",INIT_ERR=%" PRIu32
            ",PAIRED=%" PRIu32 ",AMBIG=%" PRIu32 ",EXPIRED=%" PRIu32 ",PENDING=%u/%u,"
            "LOCAL=1,SYNC=0\r\n",tx_ok,tx_err,rx_ok,rx_bad,foreign,init_errors,
            snap.paired,snap.ambiguous,snap.expired,snap.pending_a,snap.pending_b));
        emit(line,snprintf(line,sizeof line,
            "$MEASURE_RESULT_STATE,TX_OK=%" PRIu32 ",TX_ERR=%" PRIu32
            ",RX_OK=%" PRIu32 ",RX_BAD=%" PRIu32 ",DIAG=1,LOCAL=1,SYNC=0\r\n",
            result_tx_ok,result_tx_err,result_rx_ok,result_rx_bad));
    }
}
/** @brief 复制统计；NULL参数不操作。 */
void UDPSoundEvent_GetSnapshot(SoundPairSnapshot *pair,uint32_t *out_tx_ok,
                               uint32_t *out_tx_err,uint32_t *out_rx_ok,
                               uint32_t *out_rx_bad,uint32_t *out_foreign)
{
    if(pair!=NULL)SoundPair_GetSnapshot(&pair_manager,pair);
    if(out_tx_ok!=NULL)*out_tx_ok=tx_ok;
    if(out_tx_err!=NULL)*out_tx_err=tx_err;
    if(out_rx_ok!=NULL)*out_rx_ok=rx_ok;
    if(out_rx_bad!=NULL)*out_rx_bad=rx_bad;
    if(out_foreign!=NULL)*out_foreign=foreign;
}

/** @brief 复制权威结果报文计数；NULL参数不操作。 */
void UDPSoundEvent_GetResultCounters(uint32_t *out_tx_ok,uint32_t *out_tx_err,
                                     uint32_t *out_rx_ok,uint32_t *out_rx_bad)
{
    if(out_tx_ok!=NULL)*out_tx_ok=result_tx_ok;
    if(out_tx_err!=NULL)*out_tx_err=result_tx_err;
    if(out_rx_ok!=NULL)*out_rx_ok=result_rx_ok;
    if(out_rx_bad!=NULL)*out_rx_bad=result_rx_bad;
}
