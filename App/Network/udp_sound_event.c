/**
 * @file        udp_sound_event.c
 * @brief       两板声音事件UDP发布/接收与配对结果输出的前台实现。
 * @details     固定36字节线协议，校验来源、事件侧与有效域；为右锚发布保留3000ms配对预算。
 *              撤域或换域清除旧候选，配对纯核心输出距离/方向。
 * @note        只证明软件链路；不建立PTP同步、不把接收时刻当声音到达、不宣称物理精度。
 */
#include "udp_sound_event.h"
#include "Detector/sound_event_timing.h"
#include "sound_event_wire.h"
#include "measurement_wire.h"
#include "temperature_wire.h"
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
static UDPSoundEvent_DomainSink domain_sink;
static UDPSoundEvent_AckSink ack_sink;
static UDPSoundEvent_TemperatureSink temperature_sink;
static UDPSoundEvent_RangingStatusSink ranging_status_sink;
static UDPSoundEvent_RangingAckSink ranging_ack_sink;
static UDPSoundEvent_RangingSessionSink ranging_session_sink;
static uint32_t domain_sequence;
static UDPSoundEvent_MeasurementSink measurement_sink;
/* 配对状态只由前台CPU访问，放ITCM避免挤占已满的DTCM/RAM；无DMA/IRQ。 */
static SoundPairManager pair_manager __attribute__((section(".ItcmState"),aligned(32)));
static uint32_t last_bind_ms, last_state_ms, last_bind_log_ms;
static uint32_t tx_ok, tx_err, rx_ok, rx_bad, foreign, init_errors;
static uint32_t result_tx_ok, result_tx_err, result_rx_ok, result_rx_bad;
static uint32_t ack_tx_ok, ack_tx_err, ack_rx_ok, ack_rx_bad;
static uint32_t temperature_tx_ok, temperature_tx_err;
static uint32_t temperature_rx_ok, temperature_rx_bad;
static bool configured;
static SoundPairConfig pair_config;
static uint32_t current_time_domain;

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
/**
 * @brief 检查分片pbuf的四字节消息标识，避免同长度的DOMAIN与RAK2混淆。
 * @param packet 本次接收的非NULL只读pbuf；本函数不释放其所有权。
 * @param magic 非NULL且含四个有效字节的期望标识，不依赖NUL结尾。
 * @return 已完整读取并相符返回true，否则false；无网络或业务状态变化。
 */
static bool HasMagic(const struct pbuf *packet,const char *magic)
{
    uint8_t prefix[4];
    return pbuf_copy_partial(packet,prefix,sizeof prefix,0)==sizeof prefix &&
        memcmp(prefix,magic,sizeof prefix)==0;
}
/**
 * @brief 接收回调：验证来源、协议及声音事件当前有效域后交给前台消费者。
 * @param arg 未使用的LwIP用户参数。
 * @param pcb 未使用的已绑定PCB指针。
 * @param packet 本次接收所有权；非NULL时所有路径释放，NULL时直接返回。
 * @param source 本次调用有效的来源地址，NULL或非对端时丢弃。
 * @param source_port 来源端口，必须匹配板间固定端口。
 * @return 无返回值；DOMAIN握手不受声音事件域资格门控。
 * @note 仅NO_SYS网络轮询上下文，不在硬件IRQ中处理协议。
 */
static void Receive(void *arg,struct udp_pcb *pcb,struct pbuf *packet,
                    const ip_addr_t *source,u16_t source_port)
{
    (void)arg;(void)pcb;
    if(packet==NULL)return;
    if(source==NULL || !ip_addr_cmp(source,&peer) || source_port!=UDP_SOUND_EVENT_BOARD_PORT)
    {Increment(&foreign);pbuf_free(packet);return;}
    if(packet->tot_len==TEMPERATURE_WIRE_BYTES)
    {
        uint8_t data[TEMPERATURE_WIRE_BYTES];
        if(pbuf_copy_partial(packet,data,sizeof data,0)!=sizeof data)
        {Increment(&temperature_rx_bad);pbuf_free(packet);return;}
        pbuf_free(packet);
        if(local_profile.role!=BOARD_ROLE_B)
        {Increment(&temperature_rx_bad);return;}
        TemperatureWireMessage message;
        if(!TemperatureWire_Decode(data,sizeof data,&message))
        {Increment(&temperature_rx_bad);return;}
        if(temperature_sink!=NULL){temperature_sink(&message);}
        Increment(&temperature_rx_ok);
        return;
    }
    if(packet->tot_len==MEASUREMENT_ACK_BYTES)
    {
        uint8_t ack_data[MEASUREMENT_ACK_BYTES];
        if(pbuf_copy_partial(packet,ack_data,sizeof ack_data,0)!=sizeof ack_data)
        {Increment(&rx_bad);Increment(&ack_rx_bad);pbuf_free(packet);return;}
        pbuf_free(packet);
        if(local_profile.role!=BOARD_ROLE_A)
        {Increment(&rx_bad);Increment(&ack_rx_bad);return;}
        MeasurementAck ack;
        if(MeasurementAck_Decode(ack_data,sizeof ack_data,&ack)!=MEASUREMENT_ACK_WIRE_OK)
        {Increment(&rx_bad);Increment(&ack_rx_bad);return;}
        if(ack_sink!=NULL){ack_sink(&ack);}
        Increment(&rx_ok);Increment(&ack_rx_ok);
        return;
    }
    if(packet->tot_len==RANGING_SESSION_WIRE_BYTES && HasMagic(packet,"RSC2"))
    {
        uint8_t data[RANGING_SESSION_WIRE_BYTES];
        const bool copied=pbuf_copy_partial(packet,data,sizeof data,0)==sizeof data;
        pbuf_free(packet);
        RangingSessionMessage message;
        if(!copied || !RangingSessionWire_Decode(data,sizeof data,&message))
        { Increment(&rx_bad);return; }
        const bool from_b=message.type==RANGING_SESSION_CHALLENGE ||
            message.type==RANGING_SESSION_CONFIRM;
        if((local_profile.role==BOARD_ROLE_A)!=from_b)
        { Increment(&rx_bad);return; }
        if(ranging_session_sink!=NULL)ranging_session_sink(&message);
        Increment(&rx_ok);return;
    }
    if(packet->tot_len==DOMAIN_WIRE_BYTES && HasMagic(packet,"DOMN"))
    {
        uint8_t domain_data[DOMAIN_WIRE_BYTES];
        if(pbuf_copy_partial(packet,domain_data,sizeof domain_data,0)!=sizeof domain_data)
        {Increment(&rx_bad);pbuf_free(packet);return;}
        pbuf_free(packet);
        DomainMessage message;
        if(DomainWire_Decode(domain_data,sizeof domain_data,&message)!=DOMAIN_WIRE_OK)
        {Increment(&rx_bad);return;}
        if((local_profile.role==BOARD_ROLE_A &&
            (message.type!=DOMAIN_MESSAGE_ACK || message.role!=BOARD_ROLE_B)) ||
           (local_profile.role==BOARD_ROLE_B &&
            (message.type!=DOMAIN_MESSAGE_REQUEST || message.role!=BOARD_ROLE_A)))
        {Increment(&rx_bad);return;}
        if(domain_sink!=NULL){domain_sink(&message);}
        Increment(&rx_ok);
        return;
    }
    if(packet->tot_len==RANGING_STATUS_WIRE_BYTES)
    {
        uint8_t status_data[RANGING_STATUS_WIRE_BYTES];
        if(pbuf_copy_partial(packet,status_data,sizeof status_data,0)!=sizeof status_data)
        {Increment(&rx_bad);Increment(&result_rx_bad);pbuf_free(packet);return;}
        pbuf_free(packet);
        if(local_profile.role!=BOARD_ROLE_B)
        {Increment(&rx_bad);Increment(&result_rx_bad);return;}
        RangingStatusMessage status;
        if(RangingStatusWire_Decode(status_data,sizeof status_data,&status)!=
           RANGING_STATUS_WIRE_OK)
        {Increment(&rx_bad);Increment(&result_rx_bad);return;}
        if(ranging_status_sink!=NULL){ranging_status_sink(&status);}
        Increment(&rx_ok);Increment(&result_rx_ok);
        return;
    }
    if(packet->tot_len==RANGING_STATUS_ACK_BYTES)
    {
        uint8_t ack_data[RANGING_STATUS_ACK_BYTES];
        if(pbuf_copy_partial(packet,ack_data,sizeof ack_data,0)!=sizeof ack_data)
        {Increment(&rx_bad);Increment(&ack_rx_bad);pbuf_free(packet);return;}
        pbuf_free(packet);
        if(local_profile.role!=BOARD_ROLE_A)
        {Increment(&rx_bad);Increment(&ack_rx_bad);return;}
        RangingStatusAck ack;
        if(RangingStatusAck_Decode(ack_data,sizeof ack_data,&ack)!=
           RANGING_STATUS_WIRE_OK)
        {Increment(&rx_bad);Increment(&ack_rx_bad);return;}
        if(ranging_ack_sink!=NULL){ranging_ack_sink(&ack);}
        Increment(&rx_ok);Increment(&ack_rx_ok);
        return;
    }
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
    const uint16_t wire_length=packet->tot_len;
    if(wire_length!=SOUND_EVENT_WIRE_V1_BYTES && wire_length!=SOUND_EVENT_WIRE_BYTES)
    {Increment(&rx_bad);pbuf_free(packet);return;}
    uint8_t data[SOUND_EVENT_WIRE_BYTES];
    if(pbuf_copy_partial(packet,data,wire_length,0)!=wire_length)
    {Increment(&rx_bad);pbuf_free(packet);return;}
    pbuf_free(packet);
    SoundEventRecord event;
    if(SoundEventWire_Decode(data,wire_length,&event)!=SOUND_EVENT_WIRE_OK)
    {Increment(&rx_bad);return;}
    SoundEventSide local;
    if(!LocalSide(&local) || event.side==local)
    {Increment(&rx_bad);return;}
    if(current_time_domain==0U || event.time_domain!=current_time_domain)
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
    if(service_pcb==NULL)
    {
        Increment(&init_errors);
        if(emit!=NULL && (uint32_t)(now_ms-last_bind_log_ms)>=1000U)
        {
            last_bind_log_ms=now_ms;
            char line[96];
            const int length=snprintf(line,sizeof line,
                "$SOUND_EVENT_BIND,RESULT=NEW_FAIL\r\n");
            emit(line,length);
        }
        return;
    }
    const err_t bind_error=udp_bind(service_pcb,netif_ip_addr4(&gnetif),
                                    UDP_SOUND_EVENT_BOARD_PORT);
    if(bind_error!=ERR_OK)
    {
        udp_remove(service_pcb);service_pcb=NULL;Increment(&init_errors);
        if(emit!=NULL && (uint32_t)(now_ms-last_bind_log_ms)>=1000U)
        {
            last_bind_log_ms=now_ms;
            const ip_addr_t *local=netif_ip_addr4(&gnetif);
            const uint32_t ip=local!=NULL?(uint32_t)ip4_addr_get_u32(ip_2_ip4(local)):0U;
            char line[128];
            const int length=snprintf(line,sizeof line,
                "$SOUND_EVENT_BIND,RESULT=BIND_FAIL,ERR=%d,LOCAL_IP=%08" PRIX32
                "\r\n",(int)bind_error,ip);
            emit(line,length);
        }
        return;
    }
    udp_bind_netif(service_pcb,&gnetif);
    udp_recv(service_pcb,Receive,NULL);
}
/**
 * @brief 初始化身份、3000ms配对等待预算与统计，初始声音事件域无效。
 * @param now_ms 本地单调毫秒起点，支持无符号回绕。
 * @param profile 非NULL只读A/B身份，返回前复制，非法角色拒绝。
 * @param tick 非NULL毫秒读取函数，须在服务期间持续有效。
 * @param log 非NULL前台日志消费者，须在返回前消费或复制临时字节。
 * @return true为软件配置成功；false为参数或配对配置非法。
 * @note 释放已有PCB并复位统计；真正绑定在Process完成，不配置外设。
 */
bool UDPSoundEvent_Init(uint32_t now_ms,const BoardIdentityProfile *profile,
                        uint32_t (*tick)(void),UDPSoundEvent_Log log)
{
    if(service_pcb!=NULL){udp_remove(service_pcb);service_pcb=NULL;}
    configured=false;
    current_time_domain=0U;
    if(profile==NULL || tick==NULL || log==NULL ||
       (profile->role!=BOARD_ROLE_A && profile->role!=BOARD_ROLE_B))return false;
    local_profile=*profile;read_tick=tick;emit=log;
    IP_ADDR4(&peer,profile->peer_ip[0],profile->peer_ip[1],
             profile->peer_ip[2],profile->peer_ip[3]);
    pair_config=(SoundPairConfig){40000000U,10000U,343000U,0U,SOUND_EVENT_PAIR_TIMEOUT_MS,
                                  320U,32U,3U};
    if(!SoundPair_Init(&pair_manager,&pair_config))return false;
    last_bind_ms=now_ms-1000U;last_state_ms=now_ms;
    tx_ok=tx_err=rx_ok=rx_bad=foreign=init_errors=0U;
    result_tx_ok=result_tx_err=result_rx_ok=result_rx_bad=0U;
    domain_sequence=0U;
    ack_tx_ok=ack_tx_err=ack_rx_ok=ack_rx_bad=0U;
    temperature_tx_ok=temperature_tx_err=temperature_rx_ok=temperature_rx_bad=0U;
    ranging_status_sink=NULL;ranging_ack_sink=NULL;ranging_session_sink=NULL;
    configured=true;return true;
}
/**
 * @brief 复制有效域，并在资格边界撤销旧配对候选及未消费结果。
 * @param id 上层确认有效的共同域；0撤域，同一有效域重复调用不清队。
 * @return 无返回值；未初始化时不操作，累计统计原样保留。
 * @note 仅串行前台，复用既有固定队列，不分配新事件数组。
 */
void UDPSoundEvent_SetTimeDomain(uint32_t id)
{
    if(!configured || id==current_time_domain)return;
    const SoundPairSnapshot saved_stats=pair_manager.stats;
    (void)SoundPair_Init(&pair_manager,&pair_config);
    pair_manager.stats=saved_stats;
    current_time_domain=id;
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
/** @brief 安装RST2消费者；NULL不回调。 */
void UDPSoundEvent_SetRangingStatusSink(UDPSoundEvent_RangingStatusSink sink)
{
    ranging_status_sink=sink;
}
/** @brief 安装RAK2消费者；NULL不回调。 */
void UDPSoundEvent_SetRangingAckSink(UDPSoundEvent_RangingAckSink sink)
{
    ranging_ack_sink=sink;
}
/** @brief 安装会话回调；sink可NULL，不保留任何报文指针，无返回值。 */
void UDPSoundEvent_SetRangingSessionSink(UDPSoundEvent_RangingSessionSink sink)
{ ranging_session_sink=sink; }

/**
 * @brief 在原5009 PCB发送固定RSC2字节，不另申请PCB或外设。
 * @param now_ms 当前前台毫秒，重试节奏由RangingSession记录尝试时刻。
 * @param message 非NULL调用内借用副本，返回后不保留。
 * @return 编码与UDP发送成功为true，失败计数并释放临时pbuf。
 */
bool UDPSoundEvent_SendRangingSession(uint32_t now_ms,const RangingSessionMessage *message)
{
    (void)now_ms;
    if(!configured || service_pcb==NULL || message==NULL || !netif_is_link_up(&gnetif))
    { Increment(&tx_err);return false; }
    uint8_t data[RANGING_SESSION_WIRE_BYTES];
    if(!RangingSessionWire_Encode(message,data,sizeof data))
    { Increment(&tx_err);return false; }
    struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,sizeof data,PBUF_RAM);
    if(packet==NULL){Increment(&tx_err);return false;}
    err_t err=pbuf_take(packet,data,sizeof data);
    if(err==ERR_OK)err=udp_sendto(service_pcb,packet,&peer,UDP_SOUND_EVENT_BOARD_PORT);
    pbuf_free(packet);
    if(err!=ERR_OK){Increment(&tx_err);return false;}
    Increment(&tx_ok);return true;
}
/** @brief 安装对端事件消费者；NULL不回调。 */
void UDPSoundEvent_SetEventSink(UDPSoundEvent_EventSink sink)
{
    event_sink=sink;
}
/** @brief 安装共同域消费者；NULL不回调。 */
void UDPSoundEvent_SetDomainSink(UDPSoundEvent_DomainSink sink)
{
    domain_sink=sink;
}
/** @brief 发送一条DOMAIN消息；type/role由本板角色决定。 */
static bool SendDomain(uint32_t token,DomainMessageType type)
{
    if(!configured || service_pcb==NULL || token==0U ||
       !netif_is_link_up(&gnetif)){Increment(&tx_err);return false;}
    DomainMessage message;
    message.type=type;message.role=local_profile.role;
    message.state=DOMAIN_LOCAL_VALID;message.token=token;
    message.boot_nonce=token;message.sequence=++domain_sequence;
    if(domain_sequence==0U)message.sequence=++domain_sequence;
    uint8_t data[DOMAIN_WIRE_BYTES];
    if(DomainWire_Encode(&message,data,sizeof data)!=DOMAIN_WIRE_OK)
    {Increment(&tx_err);return false;}
    struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,sizeof data,PBUF_RAM);
    if(packet==NULL){Increment(&tx_err);return false;}
    err_t err=pbuf_take(packet,data,sizeof data);
    if(err==ERR_OK)err=udp_sendto(service_pcb,packet,&peer,UDP_SOUND_EVENT_BOARD_PORT);
    pbuf_free(packet);
    if(err!=ERR_OK){Increment(&tx_err);return false;}
    Increment(&tx_ok);return true;
}
bool UDPSoundEvent_SendDomainRequest(uint32_t now_ms,uint32_t token)
{
    (void)now_ms;
    if(local_profile.role!=BOARD_ROLE_A){Increment(&tx_err);return false;}
    return SendDomain(token,DOMAIN_MESSAGE_REQUEST);
}
bool UDPSoundEvent_SendDomainAck(uint32_t now_ms,uint32_t token)
{
    (void)now_ms;
    if(local_profile.role!=BOARD_ROLE_B){Increment(&tx_err);return false;}
    return SendDomain(token,DOMAIN_MESSAGE_ACK);
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
/** @brief A发送RST2；仅A角色/已绑定/链路可用。 */
bool UDPSoundEvent_SendRangingStatus(uint32_t now_ms,const RangingStatusMessage *message)
{
    (void)now_ms;
    if(!configured || service_pcb==NULL || message==NULL ||
       local_profile.role!=BOARD_ROLE_A || !netif_is_link_up(&gnetif))
    {Increment(&result_tx_err);return false;}
    uint8_t data[RANGING_STATUS_WIRE_BYTES];
    if(RangingStatusWire_Encode(message,data,sizeof data)!=RANGING_STATUS_WIRE_OK)
    {Increment(&result_tx_err);return false;}
    struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,sizeof data,PBUF_RAM);
    if(packet==NULL){Increment(&result_tx_err);return false;}
    err_t err=pbuf_take(packet,data,sizeof data);
    if(err==ERR_OK)err=udp_sendto(service_pcb,packet,&peer,UDP_SOUND_EVENT_BOARD_PORT);
    pbuf_free(packet);
    if(err!=ERR_OK){Increment(&result_tx_err);return false;}
    Increment(&result_tx_ok);return true;
}

/** @brief B发送RAK2；仅B角色/已绑定/链路可用。 */
bool UDPSoundEvent_SendRangingAck(uint32_t now_ms,const RangingStatusAck *ack)
{
    (void)now_ms;
    if(!configured || service_pcb==NULL || ack==NULL ||
       local_profile.role!=BOARD_ROLE_B || !netif_is_link_up(&gnetif))
    {Increment(&ack_tx_err);return false;}
    uint8_t data[RANGING_STATUS_ACK_BYTES];
    if(RangingStatusAck_Encode(ack,data,sizeof data)!=RANGING_STATUS_WIRE_OK)
    {Increment(&ack_tx_err);return false;}
    struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,sizeof data,PBUF_RAM);
    if(packet==NULL){Increment(&ack_tx_err);return false;}
    err_t err=pbuf_take(packet,data,sizeof data);
    if(err==ERR_OK)err=udp_sendto(service_pcb,packet,&peer,UDP_SOUND_EVENT_BOARD_PORT);
    pbuf_free(packet);
    if(err!=ERR_OK){Increment(&ack_tx_err);return false;}
    Increment(&tx_ok);return true;
}

/**
 * @brief 在当前有效域内发布本板事件，并提交一份给本地配对器。
 * @param now_ms 本地提交时刻，单位毫秒，供配对保留期计算。
 * @param event 非NULL只读事件，返回前复制，不保留调用方指针。
 * @return true为UDP发送成功；false为未配置、域不符、队列或收发失败并计数。
 * @note 域拒绝不进入配对器；网络发送失败时已提交的本地候选仍等待对端。
 */
bool UDPSoundEvent_Send(uint32_t now_ms,const SoundEventRecord *event)
{
    if(!configured || event==NULL){Increment(&tx_err);return false;}
    if(current_time_domain==0U || event->time_domain!=current_time_domain)
    {Increment(&tx_err);return false;}
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
/** @brief 安装MACK消费者；NULL不回调。 */
void UDPSoundEvent_SetAckSink(UDPSoundEvent_AckSink sink)
{
    ack_sink=sink;
}
/** @brief B发送MACK；非B/未配置/链路不可用返回false。 */
bool UDPSoundEvent_SendMeasurementAck(uint32_t now_ms,uint32_t measurement_id,
                                      uint32_t time_domain)
{
    (void)now_ms;
    if(!configured || service_pcb==NULL || local_profile.role!=BOARD_ROLE_B ||
       !netif_is_link_up(&gnetif) || measurement_id==0U || time_domain==0U)
    {Increment(&ack_tx_err);return false;}
    MeasurementAck ack={MEASUREMENT_ACK_OK,measurement_id,time_domain};
    uint8_t data[MEASUREMENT_ACK_BYTES];
    if(MeasurementAck_Encode(&ack,data,sizeof data)!=MEASUREMENT_ACK_WIRE_OK)
    {Increment(&ack_tx_err);return false;}
    struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,sizeof data,PBUF_RAM);
    if(packet==NULL){Increment(&ack_tx_err);return false;}
    err_t err=pbuf_take(packet,data,sizeof data);
    if(err==ERR_OK)err=udp_sendto(service_pcb,packet,&peer,UDP_SOUND_EVENT_BOARD_PORT);
    pbuf_free(packet);
    if(err!=ERR_OK){Increment(&ack_tx_err);return false;}
    Increment(&ack_tx_ok);return true;
}
/** @brief 安装B板共享温度消费者；NULL丢弃。 */
void UDPSoundEvent_SetTemperatureSink(UDPSoundEvent_TemperatureSink sink)
{
    temperature_sink=sink;
}
/** @brief A板发送当前有效温度；失败只计入共享温度通道计数，不污染事件计数。 */
bool UDPSoundEvent_SendTemperature(uint32_t now_ms,const TemperatureSnapshot *temperature)
{
    (void)now_ms;
    if(!configured || service_pcb==NULL || local_profile.role!=BOARD_ROLE_A ||
       temperature==NULL || !temperature->valid ||
       !netif_is_link_up(&gnetif))
    {Increment(&temperature_tx_err);return false;}
    TemperatureWireMessage message={true,
        temperature->manual?TEMPERATURE_SOURCE_MANUAL:TEMPERATURE_SOURCE_AUTO,
        temperature->temperature_milli_c,temperature->sound_speed_mm_s,
        temperature->version};
    uint8_t data[TEMPERATURE_WIRE_BYTES];
    if(!TemperatureWire_Encode(&message,data,sizeof data))
    {Increment(&temperature_tx_err);return false;}
    struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,sizeof data,PBUF_RAM);
    if(packet==NULL){Increment(&temperature_tx_err);return false;}
    err_t err=pbuf_take(packet,data,sizeof data);
    if(err==ERR_OK)err=udp_sendto(service_pcb,packet,&peer,UDP_SOUND_EVENT_BOARD_PORT);
    pbuf_free(packet);
    if(err!=ERR_OK){Increment(&temperature_tx_err);return false;}
    Increment(&temperature_tx_ok);return true;
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
                "SPEED_MM_S=%" PRIu32 ",BCAND=%u,SEL=%u,BQ0=%" PRId32 ",BQ1=%" PRId32
                ",DIAG=1,LOCAL=1,SYNC=0\r\n",delta,result.distance_mm,
                dir,result.a_sequence,result.b_sequence,result.time_domain,
                result.quality,result.sound_speed_mm_s,(unsigned)result.b_toa_candidate_count,
                (unsigned)result.b_toa_selected,result.b_toa_candidate_q16[0],
                result.b_toa_candidate_q16[1]));
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
            ",RX_OK=%" PRIu32 ",RX_BAD=%" PRIu32 ",ACK_TX=%" PRIu32
            ",ACK_RX=%" PRIu32 ",ACK_BAD=%" PRIu32 ",DIAG=1,LOCAL=1,SYNC=0\r\n",
            result_tx_ok,result_tx_err,result_rx_ok,result_rx_bad,
            ack_tx_ok,ack_rx_ok,ack_rx_bad));
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
