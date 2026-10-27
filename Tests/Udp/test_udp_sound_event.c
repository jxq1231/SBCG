/**
 * @file        test_udp_sound_event.c
 * @brief       在主机桩网络边界验证真实声音事件UDP服务与配对核心。
 * @details     覆盖右锚发布延迟、域撤销、等待边界、回绕、容量和歧义拒绝。
 * @note        仅替换LwIP收发，不替换UDP服务或配对算法，不代表实物网络验收。
 */
#include "Network/udp_sound_event.h"
#include "Network/sound_event_wire.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

struct netif gnetif;
static struct udp_pcb fake_pcb;
static struct pbuf fake_pbuf;
static ip_addr_t peer_ip;
static udp_recv_fn receive_cb;
static void *receive_arg;
static unsigned char sent[SOUND_EVENT_WIRE_BYTES];
static unsigned char rx_payload[SOUND_EVENT_WIRE_BYTES];
static unsigned int sends, allocs, frees;
static SoundPairResult paired;
static unsigned pair_count;
static uint32_t adopted_domain;
static DomainMessage received_domain;
static MeasurementAck received_ack;
static uint32_t tick_ms;

struct udp_pcb *udp_new(void) { return &fake_pcb; }
void udp_remove(struct udp_pcb *p) { (void)p; }
err_t udp_bind(struct udp_pcb *p, const ip_addr_t *ip, u16_t port)
{ (void)p;(void)ip;(void)port;return ERR_OK; }
void udp_bind_netif(struct udp_pcb *p, const struct netif *n) { (void)p;(void)n; }
void udp_recv(struct udp_pcb *p, udp_recv_fn cb, void *arg)
{ (void)p;receive_cb=cb;receive_arg=arg; }
struct pbuf *pbuf_alloc(pbuf_layer layer, u16_t len, pbuf_type type)
{ (void)layer;(void)type; ++allocs; fake_pbuf.tot_len=len; fake_pbuf.payload=sent; return &fake_pbuf; }
err_t pbuf_take(struct pbuf *p, const void *data, u16_t len)
{ assert(p==&fake_pbuf); memcpy(p->payload,data,len); return ERR_OK; }
u8_t pbuf_free(struct pbuf *p) { (void)p; ++frees; return 1U; }
err_t udp_sendto(struct udp_pcb *p, struct pbuf *buf, const ip_addr_t *dest, u16_t port)
{ (void)p;(void)buf;(void)dest;assert(port==5009U); ++sends; return ERR_OK; }
u16_t pbuf_copy_partial(const struct pbuf *p, void *data, u16_t len, u16_t offset)
{ assert(p!=NULL); assert(offset==0U && len<=p->tot_len); memcpy(data,(const uint8_t*)p->payload,len); return len; }
static void Log(const char *line,int length) { (void)line;(void)length; }
static void Result(const SoundPairResult *result)
{ paired=*result; ++pair_count; }
static void EventSink(const SoundEventRecord *event)
{ adopted_domain=event->time_domain; }
static void DomainSink(const DomainMessage *message)
{ received_domain=*message; }
static void AckSink(const MeasurementAck *ack)
{ received_ack=*ack; }
static uint32_t Tick(void) { return tick_ms; }

static void ReceiveBytes(const uint8_t *wire,uint16_t length)
{
    struct pbuf packet;
    memset(&packet,0,sizeof packet);
    packet.payload=(void*)wire;
    packet.tot_len=length;
    receive_cb(receive_arg,&fake_pcb,&packet,&peer_ip,5009U);
}
static void ReceiveWire(const uint8_t *wire)
{ ReceiveBytes(wire,SOUND_EVENT_WIRE_BYTES); }

/**
 * @brief 检查真实UDP服务在两种提交顺序下保留等待右锚的事件。
 * @param profile 已配置A角色和对端IP的非NULL只读身份。
 * @return 无返回值；断言配对使用TOA差而非提交延迟。
 */
static void TestDelayedPeer(const BoardIdentityProfile *profile)
{
    static const uint32_t delays_ms[]={500U,1000U,2499U,2999U};
    for(unsigned local_first=0U;local_first<2U;++local_first)
    for(unsigned i=0U;i<sizeof delays_ms/sizeof delays_ms[0];++i)
    {
        assert(UDPSoundEvent_Init(0U,profile,Tick,Log));
        UDPSoundEvent_SetTimeDomain(1U);
        UDPSoundEvent_Process(100U);
        SoundEventRecord local={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_A,
                                1U,1U,1U,1000000000U};
        SoundEventRecord remote={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_B,
                                 1U,2U,2U,1001000000U};
        unsigned before=pair_count;
        assert(SoundEventWire_Encode(&remote,rx_payload,sizeof rx_payload)==SOUND_EVENT_WIRE_OK);
        tick_ms=100U;
        if(local_first)assert(UDPSoundEvent_Send(tick_ms,&local));
        else ReceiveWire(rx_payload);
        UDPSoundEvent_Process(100U+delays_ms[i]-1U);
        tick_ms=100U+delays_ms[i];
        if(local_first)ReceiveWire(rx_payload);
        else assert(UDPSoundEvent_Send(tick_ms,&local));
        UDPSoundEvent_Process(tick_ms);
        assert(pair_count==before+1U && paired.valid && paired.delta_ns==1000000U);
    }
}

/**
 * @brief 初始化真实UDP服务并放行域1，清除上一用例的软件队列。
 * @param profile 非NULL只读A板身份。
 * @param start_ms 本用例单调毫秒起点，可在UINT32_MAX附近。
 * @return 无返回值；后续ReceiveWire通过真实接收回调交付数据。
 */
static void InitService(const BoardIdentityProfile *profile,uint32_t start_ms)
{
    assert(UDPSoundEvent_Init(start_ms,profile,Tick,Log));
    UDPSoundEvent_SetTimeDomain(1U);
    tick_ms=start_ms;
    UDPSoundEvent_Process(start_ms);
}

/**
 * @brief 复制一个已编码对端声音事件到真实UDP接收回调。
 * @param event 非NULL只读B板事件，返回后不保留。
 * @param now_ms 本机接收时刻，单位毫秒。
 * @return 无返回值；协议编码失败或服务未绑定由断言报错。
 */
static void ReceiveEvent(const SoundEventRecord *event,uint32_t now_ms)
{
    assert(SoundEventWire_Encode(event,rx_payload,sizeof rx_payload)==SOUND_EVENT_WIRE_OK);
    tick_ms=now_ms;
    ReceiveWire(rx_payload);
}

/**
 * @brief 验证3000ms闭边界过期和无符号毫秒回绕不改变等待语义。
 * @param profile 非NULL只读A板身份。
 * @return 无返回值；两种提交顺序和回绕路径使用真实UDP服务。
 */
static void TestExpiryAndWrap(const BoardIdentityProfile *profile)
{
    SoundEventRecord local={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_A,1U,1U,1U,1000000000U};
    SoundEventRecord remote={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_B,1U,2U,2U,1001000000U};
    for(unsigned local_first=0U;local_first<2U;++local_first)
    {
        InitService(profile,100U);
        const unsigned before=pair_count;
        if(local_first)assert(UDPSoundEvent_Send(100U,&local));
        else ReceiveEvent(&remote,100U);
        if(local_first)ReceiveEvent(&remote,3100U);
        else assert(UDPSoundEvent_Send(3100U,&local));
        UDPSoundEvent_Process(3100U);
        SoundPairSnapshot snap;UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,NULL,NULL);
        assert(pair_count==before && snap.expired==1U);
    }
    for(unsigned local_first=0U;local_first<2U;++local_first)
    {
        const uint32_t start_ms=UINT32_MAX-1000U;
        InitService(profile,start_ms);
        const unsigned before=pair_count;
        if(local_first)assert(UDPSoundEvent_Send(start_ms,&local));
        else ReceiveEvent(&remote,start_ms);
        UDPSoundEvent_Process(start_ms+2998U);
        if(local_first)ReceiveEvent(&remote,start_ms+2999U);
        else assert(UDPSoundEvent_Send(start_ms+2999U,&local));
        UDPSoundEvent_Process(start_ms+2999U);
        assert(pair_count==before+1U && paired.valid && paired.delta_ns==1000000U);
        InitService(profile,start_ms);
        assert(UDPSoundEvent_Send(start_ms,&local));
        ReceiveEvent(&remote,start_ms+3000U);
        UDPSoundEvent_Process(start_ms+3000U);
        SoundPairSnapshot snap;UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,NULL,NULL);
        assert(pair_count==before+1U && snap.expired==1U);
    }
}

/**
 * @brief 验证撤域和换域清旧队列、保留统计，且无域时仍允许DOMAIN握手。
 * @param profile 非NULL只读A板身份。
 * @return 无返回值；仅同一有效域的声音事件可以进入真实配对器。
 */
static void TestDomainBoundary(const BoardIdentityProfile *profile)
{
    SoundEventRecord local={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_A,1U,1U,1U,1000000000U};
    SoundEventRecord remote={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_B,1U,2U,2U,1001000000U};
    assert(UDPSoundEvent_Init(0U,profile,Tick,Log));
    UDPSoundEvent_Process(100U);
    assert(!UDPSoundEvent_Send(100U,&local));
    ReceiveEvent(&remote,100U);
    SoundPairSnapshot snap;uint32_t rx_bad;
    UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,&rx_bad,NULL);
    assert(snap.accepted==0U && snap.pending_a==0U && snap.pending_b==0U && rx_bad==1U);
    assert(UDPSoundEvent_SendDomainRequest(100U,1U));
    DomainMessage ack={DOMAIN_MESSAGE_ACK,BOARD_ROLE_B,DOMAIN_LOCAL_VALID,1U,1U,17U};
    uint8_t domain_wire[DOMAIN_WIRE_BYTES];
    assert(DomainWire_Encode(&ack,domain_wire,sizeof domain_wire)==DOMAIN_WIRE_OK);
    ReceiveBytes(domain_wire,DOMAIN_WIRE_BYTES);
    assert(received_domain.sequence==17U);
    UDPSoundEvent_SetTimeDomain(1U);
    assert(UDPSoundEvent_Send(101U,&local));
    assert(!UDPSoundEvent_Send(101U,&local)); /* 同侧队内重复仍拒绝。 */
    UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,NULL,NULL);
    assert(snap.accepted==1U && snap.duplicate==1U);
    UDPSoundEvent_SetTimeDomain(0U);
    UDPSoundEvent_SetTimeDomain(1U);
    const unsigned before=pair_count;
    ReceiveEvent(&remote,102U);
    UDPSoundEvent_Process(102U);
    UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,NULL,NULL);
    assert(pair_count==before && snap.accepted==2U && snap.duplicate==1U &&
           snap.pending_a==0U && snap.pending_b==1U);
    UDPSoundEvent_SetTimeDomain(2U);
    local.time_domain=2U;local.sequence=3U;
    assert(UDPSoundEvent_Send(103U,&local));
    ReceiveEvent(&remote,103U); /* 旧域报文不得重新占队。 */
    UDPSoundEvent_Process(103U);
    UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,&rx_bad,NULL);
    assert(pair_count==before && snap.accepted==3U && snap.pending_a==1U &&
           snap.pending_b==0U && rx_bad==2U);
    SoundEventRecord wrong=local;wrong.time_domain=1U;wrong.sequence=4U;
    assert(!UDPSoundEvent_Send(103U,&wrong));
    remote.time_domain=2U;remote.sequence=5U;
    ReceiveEvent(&remote,104U);
    UDPSoundEvent_SetTimeDomain(2U); /* 稳态同步同域不得清掉合法候选。 */
    UDPSoundEvent_Process(104U);
    assert(pair_count==before+1U && paired.valid && paired.time_domain==2U && paired.a_sequence==3U);
    UDPSoundEvent_SetTimeDomain(0U);
    UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,NULL,NULL);
    assert(snap.accepted==4U && snap.paired==1U && snap.duplicate==1U &&
           snap.pending_a==0U && snap.pending_b==0U && snap.pending_results==0U);
}

/**
 * @brief 验证等待预算调整不扩大40ms窗口、队列容量或歧义容忍度。
 * @param profile 非NULL只读A板身份。
 * @return 无返回值；超窗和满队保持拒绝，多个时间候选保持AMBIGUOUS。
 */
static void TestPairingLimits(const BoardIdentityProfile *profile)
{
    uint32_t rx_bad;
    SoundEventRecord local={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_A,1U,1U,1U,1000000000U};
    SoundEventRecord remote={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_B,1U,2U,2U,1040000001U};
    InitService(profile,100U);
    unsigned before=pair_count;
    assert(UDPSoundEvent_Send(100U,&local));
    ReceiveEvent(&remote,2000U);
    UDPSoundEvent_Process(2000U);
    SoundPairSnapshot snap;UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,NULL,NULL);
    assert(pair_count==before && snap.pending_a==1U && snap.pending_b==1U);
    InitService(profile,100U);
    remote.time_ns=1040000000U;
    assert(UDPSoundEvent_Send(100U,&local));
    ReceiveEvent(&remote,2000U);
    UDPSoundEvent_Process(2000U);
    assert(pair_count==before+1U && paired.status==SOUND_PAIR_DISTANCE_RANGE);
    InitService(profile,100U);
    before=pair_count;
    assert(UDPSoundEvent_Send(100U,&local));
    remote.time_ns=1001000000U;
    ReceiveEvent(&remote,2000U);
    remote.sequence=3U;remote.time_ns=1002000000U;
    ReceiveEvent(&remote,2000U);
    UDPSoundEvent_Process(2000U);
    assert(pair_count==before+1U && !paired.valid && paired.status==SOUND_PAIR_AMBIGUOUS);
    UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,NULL,NULL);
    assert(snap.ambiguous==1U && snap.pending_a==0U && snap.pending_b==0U);
    InitService(profile,100U);
    for(unsigned i=0U;i<4U;++i)
    {
        local.sequence=i+1U;local.time_ns=1000000000U+(uint64_t)i*1000000000U;
        assert(UDPSoundEvent_Send(100U,&local));
        remote.sequence=i+1U;remote.time_ns=10000000000ULL+(uint64_t)i*1000000000U;
        ReceiveEvent(&remote,100U);
    }
    local.sequence=5U;assert(!UDPSoundEvent_Send(100U,&local));
    remote.sequence=5U;ReceiveEvent(&remote,100U);
    UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,&rx_bad,NULL);
    assert(snap.pending_a==4U && snap.pending_b==4U && snap.accepted==8U &&
           snap.rejected==2U && rx_bad==1U);
}

/** @brief 运行真实UDP集成场景；无参数，成功返回0，失败由断言终止。 */
int main(void)
{
    BoardIdentityProfile profile;
    memset(&profile,0,sizeof profile);
    profile.role=BOARD_ROLE_A;profile.peer_ip[0]=192U;profile.peer_ip[1]=168U;
    profile.peer_ip[2]=10U;profile.peer_ip[3]=2U;
    IP_ADDR4(&peer_ip,192,168,10,2);
    IP_ADDR4(&gnetif.ip_addr,192,168,10,1);
    gnetif.flags=NETIF_FLAG_UP|NETIF_FLAG_LINK_UP;
    assert(UDPSoundEvent_Init(0U,&profile,Tick,Log));
    UDPSoundEvent_SetTimeDomain(1U);
    UDPSoundEvent_SetResultSink(Result);
    UDPSoundEvent_SetEventSink(EventSink);
    UDPSoundEvent_SetDomainSink(DomainSink);
    UDPSoundEvent_SetAckSink(AckSink);
    UDPSoundEvent_Process(100U); /* lazy bind before send */
    SoundEventRecord local={0};
    local.valid=true;local.type=SOUND_EVENT_CHIRP;local.quality=80U;
    local.side=SOUND_SIDE_A;local.time_domain=1U;local.session=1U;local.sequence=1U;
    local.time_ns=1000000000U;
    assert(UDPSoundEvent_Send(100U,&local));
    assert(sends==1U);
    /* Local event alone cannot pair. */
    UDPSoundEvent_Process(101U);
    assert(pair_count==0U);
    SoundEventRecord peer={0};
    peer.valid=true;peer.type=SOUND_EVENT_CHIRP;peer.quality=80U;
    peer.side=SOUND_SIDE_B;peer.time_domain=1U;peer.session=2U;peer.sequence=2U;
    peer.time_ns=1001000000U;
    assert(SoundEventWire_Encode(&peer,rx_payload,sizeof rx_payload)==SOUND_EVENT_WIRE_OK);
    tick_ms=102U;
    ReceiveWire(rx_payload);
    UDPSoundEvent_Process(102U);
    assert(adopted_domain==1U);
    assert(pair_count==1U && paired.valid && paired.distance_mm==343U &&
           paired.direction==SOUND_DIRECTION_A && paired.a_sequence==1U &&
           paired.b_sequence==2U);
    DomainMessage request={DOMAIN_MESSAGE_REQUEST,BOARD_ROLE_A,
        DOMAIN_LOCAL_VALID,0x12345678U,0x12345678U,1U},decoded;
    uint8_t domain_wire[DOMAIN_WIRE_BYTES];
    assert(DomainWire_Encode(&request,domain_wire,sizeof domain_wire)==DOMAIN_WIRE_OK);
    assert(DomainWire_Decode(domain_wire,sizeof domain_wire,&decoded)==DOMAIN_WIRE_OK);
    assert(decoded.type==DOMAIN_MESSAGE_REQUEST && decoded.role==BOARD_ROLE_A &&
           decoded.token==0x12345678U && decoded.sequence==1U);
    domain_wire[12]=0U;domain_wire[13]=0U;domain_wire[14]=0U;domain_wire[15]=0U;
    assert(DomainWire_Decode(domain_wire,sizeof domain_wire,&decoded)==DOMAIN_WIRE_INVALID_FIELD);
    assert(UDPSoundEvent_SendDomainRequest(200U,0x12345678U));
    assert(DomainWire_Decode(sent,DOMAIN_WIRE_BYTES,&decoded)==DOMAIN_WIRE_OK);
    assert(decoded.type==DOMAIN_MESSAGE_REQUEST && decoded.token==0x12345678U);
    assert(!UDPSoundEvent_SendDomainAck(201U,0x12345678U));
    DomainMessage ack={DOMAIN_MESSAGE_ACK,BOARD_ROLE_B,DOMAIN_LOCAL_VALID,
        0x12345678U,0x12345678U,2U};
    assert(DomainWire_Encode(&ack,domain_wire,sizeof domain_wire)==DOMAIN_WIRE_OK);
    ReceiveBytes(domain_wire,DOMAIN_WIRE_BYTES);
    assert(received_domain.type==DOMAIN_MESSAGE_ACK && received_domain.role==BOARD_ROLE_B &&
           received_domain.token==0x12345678U);
    assert(!UDPSoundEvent_SendMeasurementAck(202U,7U,0x12345678U));
    MeasurementAck ack_out={MEASUREMENT_ACK_OK,7U,0x12345678U};
    uint8_t ack_wire[MEASUREMENT_ACK_BYTES];
    assert(MeasurementAck_Encode(&ack_out,ack_wire,sizeof ack_wire)==MEASUREMENT_ACK_WIRE_OK);
    ReceiveBytes(ack_wire,MEASUREMENT_ACK_BYTES);
    assert(received_ack.status==MEASUREMENT_ACK_OK && received_ack.measurement_id==7U &&
           received_ack.time_domain==0x12345678U);
    ack_wire[0]='X';
    assert(MeasurementAck_Decode(ack_wire,sizeof ack_wire,&received_ack)==MEASUREMENT_ACK_WIRE_INVALID_MAGIC);
    TestDelayedPeer(&profile);
    TestExpiryAndWrap(&profile);
    TestDomainBoundary(&profile);
    TestPairingLimits(&profile);
    puts("PASS udp sound event: both orders 500/1000/2499/2999ms, 3000ms expiry, tick wrap, domain reset, 40ms/ambiguity/capacity gates, DOMN/MACK");
    return 0;
}
