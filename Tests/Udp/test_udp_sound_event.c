#include "Network/udp_sound_event.h"
#include "Network/sound_event_wire.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    UDPSoundEvent_SetResultSink(Result);
    UDPSoundEvent_SetEventSink(EventSink);
    UDPSoundEvent_SetDomainSink(DomainSink);
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
    puts("PASS udp sound event: local+peer pairing, DOMN send/receive");
    return 0;
}
