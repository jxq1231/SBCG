/**
 * @file        test_udp_sound_event.c
 * @brief       在主机桩网络边界验证真实声音事件UDP服务与配对核心。
 * @details     覆盖右锚发布延迟、域撤销、等待边界、回绕、容量和歧义拒绝；
 *              成功配对提供确定PCM走真实相关，另测缺窗/低相关拒绝与1m/2m恢复。
 *              模式观测构建另验证MDP1格式、双板角色和UDP端点限制。
 * @note        仅替换LwIP收发，不替换UDP服务或配对算法，不代表实物网络验收。
 */
#include "Network/udp_sound_event.h"
#include "Network/business_wire.h"
#include "Network/temperature_wire.h"
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
/** 发送缓冲需容纳RST2/温度等小包和WFR1波形包(24+512*2=1048字节)。 */
static unsigned char sent[1200];
static unsigned char sent_ring[4][1200];
static unsigned short sent_ring_len[4];
static unsigned char wave_sent[1048];
static unsigned int wave_sends;
static unsigned int wave_pcm_writes;
static bool fail_wave_pcm_write;
static unsigned char rx_payload[SOUND_EVENT_WIRE_BYTES];
static unsigned int sends, allocs, frees;
static SoundPairResult paired;
static unsigned pair_count;
static uint32_t adopted_domain;
static DomainMessage received_domain;
static MeasurementAck received_ack;
static TemperatureWireMessage received_temperature;
static RangingStatusMessage received_ranging;
static RangingStatusAck received_ranging_ack;
static unsigned ranging_count, ranging_ack_count;
static RangingSessionMessage received_session;
static unsigned session_count;
static uint32_t tick_ms;
static bool saw_wave_rejection;

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
/**
 * @brief 模拟pbuf偏移写入，并支持仅对WFR1 PCM阶段注入故障。
 * @param p 非NULL且为测试发送缓冲。
 * @param data 非NULL只读数据，调用期间借用。
 * @param len 字节数，须在缓冲范围内。
 * @param offset 写入偏移，单位字节。
 * @return 注入失败时ERR_MEM，否则ERR_OK。
 */
err_t pbuf_take_at(struct pbuf *p, const void *data, u16_t len, u16_t offset)
{
    assert(p==&fake_pbuf && (unsigned)offset+len<=p->tot_len);
    if(len==1024U && (offset==24U || offset==BUSINESS_WIRE_HEADER_BYTES+24U))
    {
        ++wave_pcm_writes;
        if(fail_wave_pcm_write)return ERR_MEM;
    }
    memcpy((unsigned char*)p->payload+offset,data,len);
    return ERR_OK;
}
u8_t pbuf_free(struct pbuf *p) { (void)p; ++frees; return 1U; }
err_t udp_sendto(struct udp_pcb *p, struct pbuf *buf, const ip_addr_t *dest, u16_t port)
{
    (void)p;(void)dest;assert(port==5009U); ++sends;
    if (buf->tot_len == sizeof wave_sent)
    { memcpy(wave_sent, buf->payload, sizeof wave_sent); ++wave_sends; }
    {
        const unsigned slot=(sends-1U)&3U;
        memcpy(sent_ring[slot], buf->payload, buf->tot_len);
        sent_ring_len[slot]=buf->tot_len;
    }
    return ERR_OK;
}
/**
 * @brief 模拟跨pbuf片段读取，短链仅返回实际字节数。
 * @param p 非NULL测试报文，只读。
 * @param data 非NULL输出，调用方持有。
 * @param len 请求读取的字节数。
 * @param offset 从报文头起算的字节偏移。
 * @return 已复制的字节数。
 */
u16_t pbuf_copy_partial(const struct pbuf *p, void *data, u16_t len, u16_t offset)
{
    assert(p!=NULL && (unsigned)offset<=p->tot_len && len<=p->tot_len-offset);
    u16_t copied=0U;
    for(;p!=NULL && copied<len;p=p->next)
    {
        const u16_t size=p->len==0U?p->tot_len:p->len;
        if(offset>=size){offset=(u16_t)(offset-size);continue;}
        const u16_t available=(u16_t)(size-offset);
        const u16_t remaining=(u16_t)(len-copied);
        const u16_t take=available<remaining?available:remaining;
        memcpy((uint8_t*)data+copied,(const uint8_t*)p->payload+offset,take);
        copied=(u16_t)(copied+take);
        offset=0U;
    }
    return copied;
}
/** @brief 记录波形证据拒绝日志是否出现，不修改服务状态。 */
static void Log(const char *line,int length)
{ (void)length;if(strstr(line,"STATUS=WAVE_UNAVAILABLE,")!=NULL)saw_wave_rejection=true; }
static void Result(const SoundPairResult *result)
{ paired=*result; ++pair_count; }
static void EventSink(const SoundEventRecord *event)
{ adopted_domain=event->time_domain; }
static void DomainSink(const DomainMessage *message)
{ received_domain=*message; }
static void AckSink(const MeasurementAck *ack)
{ received_ack=*ack; }
static void TemperatureSink(const TemperatureWireMessage *message)
{ received_temperature=*message; }
static uint32_t Tick(void) { return tick_ms; }

/** @brief 保存真实RST2接收回调的只读副本并累计次数。 */
static void RangingSink(const RangingStatusMessage *message)
{ received_ranging=*message; ++ranging_count; }
/** @brief 保存真实RAK2接收回调；与DOMAIN的24字节分派独立。 */
static void RangingAckSink(const RangingStatusAck *message)
{ received_ranging_ack=*message; ++ranging_ack_count; }
/** @brief 保存第三种24字节RSC2，证明不被DOMAIN/RAK2抢先消费。 */
static void SessionSink(const RangingSessionMessage *message)
{ received_session=*message; ++session_count; }

static void ReceiveBytes(const uint8_t *wire,uint16_t length)
{
    struct pbuf packet;
    memset(&packet,0,sizeof packet);
    packet.payload=(void*)wire;
    packet.tot_len=length;
    receive_cb(receive_arg,&fake_pcb,&packet,&peer_ip,5009U);
}
/**
 * @brief 把报文作为三段pbuf送入真实接收回调。
 * @param wire 非NULL完整报文，调用期间借用。
 * @param length 报文字节数，至少41。
 * @param short_tail 为true时最后一个分段少一字节。
 * @return 无返回值；断言接收回调恰好释放一次。
 */
static void ReceiveSplit(const uint8_t *wire,uint16_t length,bool short_tail)
{
    assert(length>41U);
    struct pbuf tail={.payload=(void*)(wire+40U),.len=(u16_t)(length-40U-(short_tail?1U:0U)),
                      .tot_len=(u16_t)(length-40U)};
    struct pbuf middle={.next=&tail,.payload=(void*)(wire+17U),.len=23U,
                        .tot_len=(u16_t)(length-17U)};
    struct pbuf first={.next=&middle,.payload=(void*)wire,.len=17U,.tot_len=length};
    const unsigned before=frees;
    receive_cb(receive_arg,&fake_pcb,&first,&peer_ip,5009U);
    assert(frees==before+1U);
}

/**
 * @brief 生成确定的宽带PCM；两端相同输入通过真实互相关，非替换配对结果。
 * @param pcm 非NULL可写1408点缓冲，调用方持有。
 * @return 无返回值。
 */
static void TestPcm(int16_t pcm[1408])
{
    uint32_t seed=0x6a09e667U;
    for(unsigned i=0U;i<1408U;++i)
    {seed=seed*1664525U+1013904223U;pcm[i]=(int16_t)((int32_t)(seed>>17U)-16384);}
}
/**
 * @brief 为旧标量夹具补上本板PCM和帧坐标，再调用真实事件发送；已有PCM夹具原样发送。
 * @param now_ms 模拟本地毫秒，更新测试时钟。
 * @param event 非NULL只读事件，仅无帧号夹具复制补帧，不修改输入。
 * @return 真实UDPSoundEvent_Send返回值；失败仍保留生产拒绝行为。
 */
static bool SendCapturedEvent(uint32_t now_ms,const SoundEventRecord *event)
{
    if(event->frame!=0U)return UDPSoundEvent_Send(now_ms,event);
    SoundEventRecord copy=*event;copy.frame=48000U;
    int16_t pcm[1408];TestPcm(pcm);
    const ChirpCaptureSlice slice={.session=copy.session,.first_frame=copy.frame,
        .raw_first_frame=copy.frame,.frames=1408U,.samples=pcm};
    tick_ms=now_ms;UDPSoundEvent_OnCapture(&slice);
    return UDPSoundEvent_Send(now_ms,&copy);
}
/**
 * @brief 给标量事件夹具提供对应的真实WFR1包，再交给真实事件接收器；不伪造相关输出。
 * @param wire 非NULL且为完整当前版本声音事件字节，调用方持有。
 * @return 无返回值；模拟网络按先波形后事件顺序交付。
 */
static void ReceiveCapturedWire(const uint8_t *wire)
{
    SoundEventRecord event;
    assert(SoundEventWire_Decode(wire,SOUND_EVENT_WIRE_BYTES,&event)==SOUND_EVENT_WIRE_OK);
    int16_t pcm[1408];TestPcm(pcm);
    uint8_t wave[1048]={0};memcpy(wave,"WFR1",4U);wave[4]=1U;wave[5]=SOUND_SIDE_B;
    for(unsigned i=0U;i<4U;++i)wave[8U+i]=(uint8_t)(event.sequence>>(8U*(3U-i)));
    const uint64_t start=event.time_ns+UINT64_C(10666667);
    for(unsigned i=0U;i<8U;++i)wave[12U+i]=(uint8_t)(start>>(8U*(7U-i)));
    wave[20]=2U;memcpy(wave+24U,pcm+512U,1024U);
    ReceiveSplit(wave,sizeof wave,false);
    ReceiveBytes(wire,SOUND_EVENT_WIRE_BYTES);
}

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
                                1U,1U,1U,1000000000U,0U,0U,0U,{0,0,0,0}};
        SoundEventRecord remote={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_B,
                                 1U,2U,2U,1001000000U,0U,0U,0U,{0,0,0,0}};
        unsigned before=pair_count;
        assert(SoundEventWire_Encode(&remote,rx_payload,sizeof rx_payload)==SOUND_EVENT_WIRE_OK);
        tick_ms=100U;
        if(local_first)assert(SendCapturedEvent(tick_ms,&local));
        else ReceiveCapturedWire(rx_payload);
        UDPSoundEvent_Process(100U+delays_ms[i]-1U);
        tick_ms=100U+delays_ms[i];
        if(local_first)ReceiveCapturedWire(rx_payload);
        else assert(SendCapturedEvent(tick_ms,&local));
        UDPSoundEvent_Process(tick_ms);
        assert(pair_count==before+1U && paired.valid && paired.delta_ns==1000000U);
    }
}

/** @brief 验证A发送/B接收共享温度，且温度不依赖声音事件域。 */
static void TestTemperatureShare(void)
{
    BoardIdentityProfile a,b;
    memset(&a,0,sizeof a);memset(&b,0,sizeof b);
    a.role=BOARD_ROLE_A;b.role=BOARD_ROLE_B;
    a.peer_ip[0]=b.peer_ip[0]=192U;a.peer_ip[1]=b.peer_ip[1]=168U;
    a.peer_ip[2]=b.peer_ip[2]=10U;a.peer_ip[3]=b.peer_ip[3]=2U;
    assert(UDPSoundEvent_Init(0U,&a,Tick,Log));
    UDPSoundEvent_Process(100U);
    UDPSoundEvent_SetTemperatureSink(TemperatureSink);
    TemperatureSnapshot temperature={0};
    temperature.valid=true;temperature.temperature_milli_c=23500;
    temperature.sound_speed_mm_s=345541U;temperature.version=7U;
    assert(UDPSoundEvent_SendTemperature(100U,&temperature));
    TemperatureWireMessage decoded;
    assert(TemperatureWire_Decode(sent,TEMPERATURE_WIRE_BYTES,&decoded));
    assert(decoded.temperature_milli_c==23500 && decoded.version==7U);
    uint8_t temp_wire[TEMPERATURE_WIRE_BYTES];
    TemperatureWireMessage expected={true,TEMPERATURE_SOURCE_AUTO,23500,345541U,7U};
    assert(TemperatureWire_Encode(&expected,temp_wire,sizeof temp_wire));
    assert(UDPSoundEvent_Init(200U,&b,Tick,Log));
    UDPSoundEvent_SetTemperatureSink(TemperatureSink);
    UDPSoundEvent_Process(200U);
    ReceiveBytes(temp_wire,TEMPERATURE_WIRE_BYTES);
    assert(received_temperature.valid && received_temperature.version==7U);
    assert(received_temperature.temperature_milli_c==23500);
}

/**
 * @brief 验证同长DOMAIN/RAK2分别进入正确消费者及全部无值状态确认。
 * @return 无返回值；只替换底层LwIP，编解码与Receive分派均使用生产代码。
 */
static void TestRangingTransport(void)
{
    BoardIdentityProfile a={0},b={0};
    a.role=BOARD_ROLE_A;b.role=BOARD_ROLE_B;
    a.peer_ip[0]=b.peer_ip[0]=192U;a.peer_ip[1]=b.peer_ip[1]=168U;
    a.peer_ip[2]=b.peer_ip[2]=10U;a.peer_ip[3]=b.peer_ip[3]=2U;
    assert(UDPSoundEvent_Init(0U,&a,Tick,Log));
    UDPSoundEvent_SetRangingAckSink(RangingAckSink);
    UDPSoundEvent_SetRangingSessionSink(SessionSink);
    UDPSoundEvent_SetDomainSink(DomainSink);
    UDPSoundEvent_Process(100U);
    uint8_t ack_bytes[RANGING_STATUS_ACK_BYTES];
    RangingStatusAck ack={RANGING_STATUS_ACK_OK,7U,3U,9U,2U};
    assert(RangingStatusAck_Encode(&ack,ack_bytes,sizeof ack_bytes)==RANGING_STATUS_WIRE_OK);
    unsigned before=ranging_ack_count;
    ReceiveBytes(ack_bytes,sizeof ack_bytes);
    assert(ranging_ack_count==before+1U && received_ranging_ack.state_revision==9U);
    DomainMessage domain={DOMAIN_MESSAGE_ACK,BOARD_ROLE_B,DOMAIN_LOCAL_VALID,7U,7U,77U};
    uint8_t domain_bytes[DOMAIN_WIRE_BYTES];
    assert(DomainWire_Encode(&domain,domain_bytes,sizeof domain_bytes)==DOMAIN_WIRE_OK);
    ReceiveBytes(domain_bytes,sizeof domain_bytes);
    assert(received_domain.sequence==77U && ranging_ack_count==before+1U);
    RangingSessionMessage session={RANGING_SESSION_CONFIRM,7U,41U,1000000000ULL};
    uint8_t session_bytes[RANGING_SESSION_WIRE_BYTES];
    assert(RangingSessionWire_Encode(&session,session_bytes,sizeof session_bytes));
    const unsigned session_before=session_count;
    ReceiveBytes(session_bytes,sizeof session_bytes);
    assert(session_count==session_before+1U && received_session.authority_epoch==41U);
    for(uint8_t state=RANGING_STATE_ACQUIRING;state<=RANGING_STATE_FAULT;++state)
    {
        if(state!=RANGING_STATE_ACQUIRING && state!=RANGING_STATE_STALE &&
           state!=RANGING_STATE_FAULT)continue;
        assert(UDPSoundEvent_Init(200U,&b,Tick,Log));
        UDPSoundEvent_SetRangingStatusSink(RangingSink);
        UDPSoundEvent_Process(200U);
        RangingStatusMessage status={0};
        status.state=state;status.domain=7U;status.authority_clock_epoch=3U;
        status.state_revision=10U+state;status.sound_speed_mm_s=343000U;
        uint8_t data[RANGING_STATUS_WIRE_BYTES];
        assert(RangingStatusWire_Encode(&status,data,sizeof data)==RANGING_STATUS_WIRE_OK);
        const unsigned status_before=ranging_count;
        ReceiveBytes(data,sizeof data);
        assert(ranging_count==status_before+1U && received_ranging.state==state);
        ack.state_revision=status.state_revision;ack.measurement_id=0U;
        assert(UDPSoundEvent_SendRangingAck(200U,&ack));
        assert(RangingStatusAck_Decode(sent,RANGING_STATUS_ACK_BYTES,&received_ranging_ack)==
            RANGING_STATUS_WIRE_OK && received_ranging_ack.measurement_id==0U);
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
    ReceiveCapturedWire(rx_payload);
}

/**
 * @brief 验证3000ms闭边界过期和无符号毫秒回绕不改变等待语义。
 * @param profile 非NULL只读A板身份。
 * @return 无返回值；两种提交顺序和回绕路径使用真实UDP服务。
 */
static void TestExpiryAndWrap(const BoardIdentityProfile *profile)
{
    SoundEventRecord local={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_A,1U,1U,1U,1000000000U,0U,0U,0U,{0,0,0,0}};
    SoundEventRecord remote={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_B,1U,2U,2U,1001000000U,0U,0U,0U,{0,0,0,0}};
    for(unsigned local_first=0U;local_first<2U;++local_first)
    {
        InitService(profile,100U);
        const unsigned before=pair_count;
        if(local_first)assert(SendCapturedEvent(100U,&local));
        else ReceiveEvent(&remote,100U);
        if(local_first)ReceiveEvent(&remote,3100U);
        else assert(SendCapturedEvent(3100U,&local));
        UDPSoundEvent_Process(3100U);
        SoundPairSnapshot snap;UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,NULL,NULL);
        assert(pair_count==before && snap.expired==1U);
    }
    for(unsigned local_first=0U;local_first<2U;++local_first)
    {
        const uint32_t start_ms=UINT32_MAX-1000U;
        InitService(profile,start_ms);
        const unsigned before=pair_count;
        if(local_first)assert(SendCapturedEvent(start_ms,&local));
        else ReceiveEvent(&remote,start_ms);
        UDPSoundEvent_Process(start_ms+2998U);
        if(local_first)ReceiveEvent(&remote,start_ms+2999U);
        else assert(SendCapturedEvent(start_ms+2999U,&local));
        UDPSoundEvent_Process(start_ms+2999U);
        assert(pair_count==before+1U && paired.valid && paired.delta_ns==1000000U);
        InitService(profile,start_ms);
        assert(SendCapturedEvent(start_ms,&local));
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
    SoundEventRecord local={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_A,1U,1U,1U,1000000000U,0U,0U,0U,{0,0,0,0}};
    SoundEventRecord remote={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_B,1U,2U,2U,1001000000U,0U,0U,0U,{0,0,0,0}};
    assert(UDPSoundEvent_Init(0U,profile,Tick,Log));
    UDPSoundEvent_Process(100U);
    assert(!SendCapturedEvent(100U,&local));
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
    assert(SendCapturedEvent(101U,&local));
    assert(!SendCapturedEvent(101U,&local)); /* 同侧队内重复仍拒绝。 */
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
    assert(SendCapturedEvent(103U,&local));
    ReceiveEvent(&remote,103U); /* 旧域报文不得重新占队。 */
    UDPSoundEvent_Process(103U);
    UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,&rx_bad,NULL);
    assert(pair_count==before && snap.accepted==3U && snap.pending_a==1U &&
           snap.pending_b==0U && rx_bad==2U);
    SoundEventRecord wrong=local;wrong.time_domain=1U;wrong.sequence=4U;
    assert(!SendCapturedEvent(103U,&wrong));
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
    SoundEventRecord local={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_A,1U,1U,1U,1000000000U,0U,0U,0U,{0,0,0,0}};
    SoundEventRecord remote={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_B,1U,2U,2U,1040000001U,0U,0U,0U,{0,0,0,0}};
    InitService(profile,100U);
    unsigned before=pair_count;
    assert(SendCapturedEvent(100U,&local));
    ReceiveEvent(&remote,2000U);
    UDPSoundEvent_Process(2000U);
    SoundPairSnapshot snap;UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,NULL,NULL);
    assert(pair_count==before && snap.pending_a==1U && snap.pending_b==1U);
    InitService(profile,100U);
    remote.time_ns=1040000000U;
    assert(SendCapturedEvent(100U,&local));
    ReceiveEvent(&remote,2000U);
    UDPSoundEvent_Process(2000U);
    assert(pair_count==before+1U && paired.status==SOUND_PAIR_DISTANCE_RANGE);
    InitService(profile,100U);
    before=pair_count;
    assert(SendCapturedEvent(100U,&local));
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
        assert(SendCapturedEvent(100U,&local));
        remote.sequence=i+1U;remote.time_ns=10000000000ULL+(uint64_t)i*1000000000U;
        ReceiveEvent(&remote,100U);
    }
    local.sequence=5U;assert(!SendCapturedEvent(100U,&local));
    remote.sequence=5U;ReceiveEvent(&remote,100U);
    UDPSoundEvent_GetSnapshot(&snap,NULL,NULL,NULL,&rx_bad,NULL);
    assert(snap.pending_a==4U && snap.pending_b==4U && snap.accepted==8U &&
           snap.rejected==2U && rx_bad==1U);
}


/**
 * @brief   验证B板按事件帧+512从捕获储备截取波形窗口并编码WFR1包。
 * @return  无返回值；窗口样本与时间戳不符时断言失败。
 * @note    合成PCM覆盖raw_first+384..+1280；事件帧比raw_first大22帧，
 *          用于锁定local=128-(raw_first-frame)的方向，防止再次把符号写反。
 */
static void TestWaveWindowCut(void)
{
    BoardIdentityProfile profile;
    memset(&profile, 0, sizeof profile);
    profile.role = BOARD_ROLE_B;
    profile.peer_ip[0] = 192U; profile.peer_ip[1] = 168U;
    profile.peer_ip[2] = 10U; profile.peer_ip[3] = 1U;
    assert(UDPSoundEvent_Init(1000U, &profile, Tick, Log));
    UDPSoundEvent_SetTimeDomain(1U);
    tick_ms = 1000U;
    UDPSoundEvent_Process(1000U);
    static int16_t pcm[CHIRP_CAPTURE_FRAMES];
    for (uint16_t i = 0U; i < CHIRP_CAPTURE_FRAMES; ++i)
    { pcm[i] = (int16_t)((int32_t)(i % 2000U) - 1000); }
    ChirpCaptureSlice slice = {0};
    slice.samples = pcm;
    slice.frames = CHIRP_CAPTURE_FRAMES;
    slice.session = 1U;
    slice.first_frame = 1000000U;
    slice.hint_frame = 1000192U;
    slice.raw_first_frame = 1000050U;
    UDPSoundEvent_OnCapture(&slice);
    SoundEventRecord event = {0};
    event.valid = true;
    event.type = SOUND_EVENT_CHIRP;
    event.quality = 80U;
    event.side = SOUND_SIDE_B;
    event.time_domain = 1U;
    event.session = 1U;
    event.sequence = 7U;
    event.time_ns = 1000000000U;
    event.frame = 1000072U; /* raw_first+22，与实测事件帧关系一致。 */
    event.fraction_q16 = 0x4000U;
    wave_sends = 0U;
    tick_ms = 1100U;
    assert(SendCapturedEvent(tick_ms, &event));
    assert(wave_sends == 1U);
    assert(memcmp(wave_sent, "WFR1", 4U) == 0);
    assert(wave_sent[4] == 1U && wave_sent[5] == (unsigned char)SOUND_SIDE_B);
    assert(((uint16_t)wave_sent[20] << 8U | wave_sent[21]) == 512U);
    assert(((uint32_t)wave_sent[8] << 24U | (uint32_t)wave_sent[9] << 16U |
            (uint32_t)wave_sent[10] << 8U | wave_sent[11]) == 7U);
    uint64_t start_ns = 0U;
    for (uint8_t i = 0U; i < 8U; ++i) { start_ns = (start_ns << 8U) | wave_sent[12U + i]; }
    assert(start_ns == UINT64_C(1010661458));
    const uint16_t expected_offset = (uint16_t)(event.frame + 512U - slice.first_frame);
    assert(memcmp(wave_sent + 24U, pcm + expected_offset, 1024U) == 0);
    const unsigned before_sends=sends, before_allocs=allocs, before_frees=frees;
    const unsigned before_writes=wave_pcm_writes;
    fail_wave_pcm_write=true;
    ++event.sequence;
    assert(SendCapturedEvent(tick_ms,&event));
    fail_wave_pcm_write=false;
    /* PCM失败的窗口不能发送，原SEVT仍照常提交，两份pbuf均释放。 */
    assert(wave_pcm_writes==before_writes+1U && wave_sends==1U);
    assert(sends==before_sends+1U && allocs==before_allocs+2U && frees==before_frees+2U);
}
/**
 * @brief 缺失/低相关波形必须拒绝，成功的1m/2m波形不受320mm初始参考影响。
 * @param profile 非NULL A身份，只读，调用期间有效。
 * @return 无返回值；只替换网络收发和PCM输入，运行真实服务/相关/配对。
 */
static void TestWaveformEvidence(const BoardIdentityProfile *profile)
{
    SoundEventRecord local={true,SOUND_EVENT_CHIRP,98U,SOUND_SIDE_A,1U,1U,71U,
        UINT64_C(1000000000),0U,0U,0U,{0,0,0,0}};
    SoundEventRecord remote=local;remote.side=SOUND_SIDE_B;
    remote.time_ns=UINT64_C(1002915452);remote.toa_candidate_count=2U;
    remote.toa_candidate_q16[0]=-6236500;remote.toa_candidate_q16[1]=0;
    for(unsigned failure=0U;failure<4U;++failure)
    {
        InitService(profile,100U);saw_wave_rejection=false;
        const unsigned before=pair_count;
        if(failure==0U)assert(UDPSoundEvent_Send(100U,&local)); /* 无本板捕获。 */
        else assert(SendCapturedEvent(100U,&local));
        if(failure>=2U)
        {
            /* 全零或最后一字节缺失均不得产生有效波形证据。 */
            uint8_t wave[1048]={0};memcpy(wave,"WFR1",4U);wave[4]=1U;wave[5]=SOUND_SIDE_B;
            wave[11]=71U;wave[12+7]=1U;wave[20]=2U;
            ReceiveSplit(wave,sizeof wave,failure==3U);
        }
        assert(SoundEventWire_Encode(&remote,rx_payload,sizeof rx_payload)==SOUND_EVENT_WIRE_OK);
        ReceiveBytes(rx_payload,sizeof rx_payload);UDPSoundEvent_Process(101U);
        assert(pair_count==before+1U && !paired.valid && saw_wave_rejection);
        assert(paired.status==SOUND_PAIR_WAVEFORM_UNAVAILABLE);
        assert(paired.b_toa_selected==0U && !paired.b_toa_held && paired.distance_mm==0U);
        SoundPairSnapshot snapshot;UDPSoundEvent_GetSnapshot(&snapshot,NULL,NULL,NULL,NULL,NULL);
        assert(snapshot.paired==0U && snapshot.rejected==1U);
    }
    for(uint32_t metres=1U;metres<=2U;++metres)
    {
        InitService(profile,100U);
        remote.time_ns=local.time_ns+(uint64_t)metres*UINT64_C(1000000000)/343U;
        assert(SendCapturedEvent(100U,&local));ReceiveEvent(&remote,101U);
        UDPSoundEvent_Process(101U);
        assert(paired.valid && paired.waveform_refined && paired.distance_mm==metres*1000U);
        assert(paired.b_toa_selected==0U && !paired.b_toa_held);
    }
    puts("PASS waveform evidence: missing local/remote and low correlation reject; real NCC 1m/2m recover");
}
/** @brief 运行真实UDP集成场景；无参数，成功返回0，失败由断言终止。 */
#ifdef SBCG_USE_MODE_OBSERVATION
static const uint8_t mode_ready_wire[MODE_CONTROL_WIRE_BYTES]={
    'M','D','P','1',1U,3U,2U,2U,
    0x01U,0x23U,0x45U,0x67U,0x89U,0xabU,0xcdU,0xefU,
    0x10U,0x32U,0x54U,0x76U,0x98U,0xbaU,0xdcU,0xfeU,
    0,0,0,0,0,0,0,7U,0,0,0,0,0,0,0,0
};
static ModeControlMessage received_mode;
static unsigned mode_count;

/**
 * @brief 保存MDP1接收回调副本并累计调用次数。
 * @param message 非NULL只读消息，仅本次借用。
 * @return 无返回值。
 */
static void ModeControlSink(const ModeControlMessage *message)
{
    received_mode=*message;
    ++mode_count;
}

/**
 * @brief 用独立字节向量验证大端布局及非法字段拒绝。
 * @return 无返回值，失败时终止测试。
 */
static void TestModeControlWire(void)
{
    ModeControlMessage message;
    assert(ModeControlWire_Decode(mode_ready_wire,sizeof mode_ready_wire,&message));
    assert(message.op==MODE_CONTROL_READY && message.mode==MODE_CLAP &&
           message.role==BOARD_ROLE_B && message.mode_seq==7U && message.probe_id==0U);
    assert(message.a_boot==UINT64_C(0x0123456789abcdef) &&
           message.b_boot==UINT64_C(0x1032547698badcfe));
    uint8_t wire[MODE_CONTROL_WIRE_BYTES];
    assert(ModeControlWire_Encode(&message,wire,sizeof wire));
    assert(memcmp(wire,mode_ready_wire,sizeof wire)==0);
    assert(!ModeControlWire_Decode(wire,sizeof wire-1U,&message));

    const struct { uint8_t offset; uint8_t value; } invalid[]={
        {0U,'X'},{4U,2U},{5U,0xffU},{6U,0U},{6U,5U},{7U,1U},{7U,0U},{31U,0U},{39U,1U}
    };
    for(size_t i=0U;i<sizeof invalid/sizeof invalid[0];++i)
    {
        memcpy(wire,mode_ready_wire,sizeof wire);
        wire[invalid[i].offset]=invalid[i].value;
        assert(!ModeControlWire_Decode(wire,sizeof wire,&message));
        assert(message.op==MODE_CONTROL_READY && message.mode_seq==7U);
    }

    ModeControlMessage hello={
        .op=MODE_CONTROL_HELLO,.mode=MODE_NONE,.role=BOARD_ROLE_B,.b_boot=3U
    };
    assert(ModeControlWire_Encode(&hello,wire,sizeof wire));
    assert(ModeControlWire_Decode(wire,sizeof wire,&message) && message.a_boot==0U);
    hello.b_boot=0U;
    assert(!ModeControlWire_Encode(&hello,wire,sizeof wire) && wire[23]==3U);
    hello.b_boot=3U;
    hello.mode=MODE_RANGE;
    assert(!ModeControlWire_Encode(&hello,wire,sizeof wire));
    hello.mode=MODE_NONE;
    hello.mode_seq=1U;
    assert(!ModeControlWire_Encode(&hello,wire,sizeof wire));
    hello.mode_seq=0U;
    hello.probe_id=1U;
    assert(!ModeControlWire_Encode(&hello,wire,sizeof wire));

    ModeControlMessage ping={
        .op=MODE_CONTROL_PING,.mode=MODE_WAVE,.role=BOARD_ROLE_A,
        .a_boot=1U,.b_boot=3U,.mode_seq=4U,.probe_id=UINT64_MAX
    };
    assert(ModeControlWire_Encode(&ping,wire,sizeof wire));
    assert(ModeControlWire_Decode(wire,sizeof wire,&message) && message.probe_id==UINT64_MAX);
    ping.op=MODE_CONTROL_PONG;
    ping.role=BOARD_ROLE_B;
    assert(ModeControlWire_Encode(&ping,wire,sizeof wire));
    ping.probe_id=0U;
    assert(!ModeControlWire_Encode(&ping,wire,sizeof wire));
    ping.probe_id=1U;
    ping.a_boot=0U;
    assert(!ModeControlWire_Encode(&ping,wire,sizeof wire));
}

/**
 * @brief 检查真实UDP分派、端点拒绝、发送角色及pbuf归还。
 * @param profile 非NULL只读A板身份，由测试入口提供，仅本次借用。
 * @return 无返回值，失败时终止测试。
 */
static void TestModeControlTransport(const BoardIdentityProfile *profile)
{
    const ModeControlMessage prepare={
        .op=MODE_CONTROL_PREPARE,.mode=MODE_CLAP,.role=BOARD_ROLE_A,
        .a_boot=UINT64_C(0x0123456789abcdef),.b_boot=UINT64_C(0x1032547698badcfe),
        .mode_seq=7U
    };
    assert(UDPSoundEvent_Init(0U,profile,Tick,Log));
    UDPSoundEvent_SetModeControlSink(ModeControlSink);
    assert(!UDPSoundEvent_SendModeControl(&prepare));
    UDPSoundEvent_Process(0U);

    const unsigned before_send=sends,before_alloc=allocs,before_free=frees;
    assert(UDPSoundEvent_SendModeControl(&prepare));
    assert(sends==before_send+1U && allocs==before_alloc+1U && frees==before_free+1U);
    assert(fake_pbuf.tot_len==MODE_CONTROL_WIRE_BYTES);
    uint8_t prepare_wire[MODE_CONTROL_WIRE_BYTES];
    memcpy(prepare_wire,mode_ready_wire,sizeof prepare_wire);
    prepare_wire[5]=2U;
    prepare_wire[7]=1U;
    assert(memcmp(sent,prepare_wire,sizeof prepare_wire)==0);

    const unsigned before_rx_free=frees;
    ReceiveBytes(mode_ready_wire,sizeof mode_ready_wire);
    assert(mode_count==1U && received_mode.op==MODE_CONTROL_READY &&
           received_mode.b_boot==prepare.b_boot && received_mode.mode_seq==7U);
    ReceiveBytes(prepare_wire,sizeof prepare_wire); /* 自身角色报文 */
    ReceiveBytes(mode_ready_wire,sizeof mode_ready_wire-1U); /* 长度不符 */
    uint8_t malformed[MODE_CONTROL_WIRE_BYTES];
    memcpy(malformed,mode_ready_wire,sizeof malformed);
    malformed[4]=0U;
    ReceiveBytes(malformed,sizeof malformed);
    struct pbuf packet={0};
    packet.payload=(void *)mode_ready_wire;
    packet.tot_len=sizeof mode_ready_wire;
    receive_cb(receive_arg,&fake_pcb,&packet,&peer_ip,5008U);
    ip_addr_t unknown_ip;
    IP_ADDR4(&unknown_ip,192,168,10,3);
    receive_cb(receive_arg,&fake_pcb,&packet,&unknown_ip,5009U);

    uint32_t rx_ok,rx_bad,foreign;
    UDPSoundEvent_GetSnapshot(NULL,NULL,NULL,&rx_ok,&rx_bad,&foreign);
    assert(rx_ok==1U && rx_bad==3U && foreign==2U);
    assert(mode_count==1U && frees==before_rx_free+6U);
    assert(!UDPSoundEvent_SendModeControl(&received_mode)); /* 对端角色 */
    gnetif.flags=NETIF_FLAG_UP;
    assert(!UDPSoundEvent_SendModeControl(&prepare));
    gnetif.flags=NETIF_FLAG_UP|NETIF_FLAG_LINK_UP;
    assert(sends==before_send+1U && allocs==before_alloc+1U);

    BoardIdentityProfile board_b=*profile;
    board_b.role=BOARD_ROLE_B;
    assert(UDPSoundEvent_Init(0U,&board_b,Tick,Log));
    UDPSoundEvent_Process(0U);
    ReceiveBytes(prepare_wire,sizeof prepare_wire);
    assert(mode_count==1U); /* 初始化清掉前一个消费者 */
    UDPSoundEvent_SetModeControlSink(ModeControlSink);
    ReceiveBytes(prepare_wire,sizeof prepare_wire);
    assert(mode_count==2U && received_mode.op==MODE_CONTROL_PREPARE &&
           received_mode.role==BOARD_ROLE_A);
    assert(!UDPSoundEvent_SendModeControl(&prepare));
}

/* ==================== MDB1业务封装 ==================== */
static UDPSoundEvent_BusinessContext test_business_context;
static bool test_business_enabled;

/** @brief 读取最近第back个已发送载荷。 @param back 0为最后一个。 @param length 非NULL输出长度。 @return 只读环形槽。 */
static const uint8_t *RecentSent(unsigned back,uint16_t *length)
{
    const unsigned slot=(sends>=1U+back)?((sends-1U-back)&3U):0U;
    if(length!=NULL)*length=sent_ring_len[slot];
    return sent_ring[slot];
}

/** @brief 测试用闸门提供者；关闭时模拟未登记代次的阶段一兼容路径。 @param out 上下文输出。 @return 启用且填写成功为true。 */
static bool TestBusinessProvider(UDPSoundEvent_BusinessContext *out)
{
    if(!test_business_enabled)return false;
    *out=test_business_context;
    return true;
}
/** @brief 配置测试闸门上下文。 @param role 本板角色。 @param a_boot/b_boot/mode_seq 世代元组。 @param allowed 是否允许业务。 @return 无返回值。 */
static void EnableBusiness(BoardRole role,uint64_t a_boot,uint64_t b_boot,
                           uint64_t mode_seq,bool allowed)
{
    test_business_context=(UDPSoundEvent_BusinessContext){
        true,allowed,false,false,role,a_boot,b_boot,mode_seq};
    test_business_enabled=true;
}
/** @brief 配置CLAP闸门上下文：只有clap_allowed，测距类型关闭。 @return 无返回值。 */
static void EnableClapBusiness(BoardRole role,uint64_t a_boot,uint64_t b_boot,
                               uint64_t mode_seq)
{
    test_business_context=(UDPSoundEvent_BusinessContext){
        true,false,true,false,role,a_boot,b_boot,mode_seq};
    test_business_enabled=true;
}
/** @brief 配置WAVE闸门上下文：只有wave_allowed，其他模式关闭。 @return 无返回值。 */
static void EnableWaveBusiness(BoardRole role,uint64_t a_boot,uint64_t b_boot,
                               uint64_t mode_seq)
{
    test_business_context=(UDPSoundEvent_BusinessContext){
        true,false,false,true,role,a_boot,b_boot,mode_seq};
    test_business_enabled=true;
}
/** @brief 按给定世代封装一条内层报文。 @return 封装后的总字节数。 */
static uint16_t WrapBusiness(const uint8_t *inner,uint16_t inner_len,BoardRole role,
                             uint64_t a_boot,uint64_t b_boot,uint64_t mode_seq,
                             uint8_t *out)
{
    const BusinessWireMessage message={role,inner_len,a_boot,b_boot,mode_seq,inner};
    assert(BusinessWire_Encode(&message,out,BUSINESS_WIRE_MAX_BYTES));
    return (uint16_t)(BUSINESS_WIRE_HEADER_BYTES+inner_len);
}
/**
 * @brief 验证MDB1对RST2/RAK2的封装、世代拒绝、原始旧包拒绝和模式阻断。
 * @return 无返回值，失败由断言终止。
 */
static void TestBusinessRangingGate(void)
{
    BoardIdentityProfile a={0},b={0};
    a.role=BOARD_ROLE_A;b.role=BOARD_ROLE_B;
    a.peer_ip[0]=b.peer_ip[0]=192U;a.peer_ip[1]=b.peer_ip[1]=168U;
    a.peer_ip[2]=b.peer_ip[2]=10U;a.peer_ip[3]=b.peer_ip[3]=2U;
    RangingStatusMessage status={0};
    status.state=RANGING_STATE_ACQUIRING;status.domain=1U;
    status.authority_clock_epoch=3U;status.state_revision=10U;status.sound_speed_mm_s=343000U;
    uint8_t status_inner[RANGING_STATUS_WIRE_BYTES];
    assert(RangingStatusWire_Encode(&status,status_inner,sizeof status_inner)==
           RANGING_STATUS_WIRE_OK);
    RangingStatusAck ack={RANGING_STATUS_ACK_OK,7U,3U,10U,0U};
    uint8_t ack_inner[RANGING_STATUS_ACK_BYTES];
    assert(RangingStatusAck_Encode(&ack,ack_inner,sizeof ack_inner)==RANGING_STATUS_WIRE_OK);
    static uint8_t wrapped[BUSINESS_WIRE_MAX_BYTES];

    /* B：接收A的RST2，发送RAK2。 */
    assert(UDPSoundEvent_Init(0U,&b,Tick,Log));
    UDPSoundEvent_SetTimeDomain(1U);tick_ms=0U;UDPSoundEvent_Process(0U);
    UDPSoundEvent_SetRangingStatusSink(RangingSink);
    EnableBusiness(BOARD_ROLE_B,11U,22U,3U,true);
    UDPSoundEvent_SetBusinessContextProvider(TestBusinessProvider);
    const unsigned status_before=ranging_count;
    uint16_t length=WrapBusiness(status_inner,sizeof status_inner,BOARD_ROLE_A,
                                 11U,22U,3U,wrapped);
    ReceiveBytes(wrapped,length);
    assert(ranging_count==status_before+1U && received_ranging.state==RANGING_STATE_ACQUIRING);
    UDPSoundEvent_BusinessCounters counters;
    UDPSoundEvent_GetBusinessCounters(&counters);
    assert(counters.rx_ok==1U && counters.rx_blocked==0U);

    length=WrapBusiness(status_inner,sizeof status_inner,BOARD_ROLE_A,11U,22U,4U,wrapped);
    ReceiveBytes(wrapped,length);
    assert(ranging_count==status_before+1U);
    UDPSoundEvent_GetBusinessCounters(&counters);
    assert(counters.rx_blocked==1U);

    ReceiveBytes(status_inner,sizeof status_inner); /* 原始旧RST2不得写入状态。 */
    assert(ranging_count==status_before+1U);
    UDPSoundEvent_GetBusinessCounters(&counters);
    assert(counters.rx_unwrapped==1U);

    unsigned send_before=sends;
    assert(UDPSoundEvent_SendRangingAck(1U,&ack));
    assert(sends==send_before+1U);
    uint16_t sent_length=0U;
    const uint8_t *packet=RecentSent(0U,&sent_length);
    assert(sent_length==BUSINESS_WIRE_HEADER_BYTES+sizeof ack_inner);
    BusinessWireMessage decoded;
    assert(BusinessWire_Decode(packet,sent_length,&decoded));
    assert(decoded.role==BOARD_ROLE_B && decoded.a_boot==11U && decoded.b_boot==22U &&
           decoded.mode_seq==3U && memcmp(decoded.inner,ack_inner,sizeof ack_inner)==0);

    EnableBusiness(BOARD_ROLE_B,11U,22U,3U,false); /* 非ACTIVE模式阻断。 */
    send_before=sends;
    assert(!UDPSoundEvent_SendRangingAck(1U,&ack));
    assert(sends==send_before);
    const unsigned blocked_count=ranging_count;
    length=WrapBusiness(status_inner,sizeof status_inner,BOARD_ROLE_A,11U,22U,3U,wrapped);
    ReceiveBytes(wrapped,length);
    assert(ranging_count==blocked_count);
    UDPSoundEvent_GetBusinessCounters(&counters);
    assert(counters.tx_blocked==1U && counters.rx_blocked==2U);

    test_business_enabled=false; /* 未登记代次：保持阶段一原始兼容。 */
    ReceiveBytes(status_inner,sizeof status_inner);
    assert(ranging_count==blocked_count+1U);

    /* A：接收B的RAK2，发送RST2。 */
    assert(UDPSoundEvent_Init(0U,&a,Tick,Log));
    UDPSoundEvent_SetTimeDomain(1U);tick_ms=0U;UDPSoundEvent_Process(0U);
    UDPSoundEvent_SetRangingAckSink(RangingAckSink);
    EnableBusiness(BOARD_ROLE_A,11U,22U,3U,true);
    UDPSoundEvent_SetBusinessContextProvider(TestBusinessProvider);
    const unsigned ack_before=ranging_ack_count;
    length=WrapBusiness(ack_inner,sizeof ack_inner,BOARD_ROLE_B,11U,22U,3U,wrapped);
    ReceiveBytes(wrapped,length);
    assert(ranging_ack_count==ack_before+1U && received_ranging_ack.state_revision==10U);
    send_before=sends;
    assert(UDPSoundEvent_SendRangingStatus(1U,&status));
    assert(sends==send_before+1U);
    packet=RecentSent(0U,&sent_length);
    assert(sent_length==BUSINESS_WIRE_HEADER_BYTES+sizeof status_inner);
    assert(BusinessWire_Decode(packet,sent_length,&decoded));
    assert(decoded.role==BOARD_ROLE_A && memcmp(decoded.inner,status_inner,
                                                sizeof status_inner)==0);
    puts("PASS MDB1 ranging: RST2/RAK2 wrapped, stale generation/raw packet rejected, mode block");
}

/**
 * @brief 验证B板SEV2与WFR1均以MDB1封装发送且内层逐字节保留。
 * @return 无返回值，失败由断言终止。
 */
static void TestBusinessSoundWaveGate(void)
{
    BoardIdentityProfile b={0};
    b.role=BOARD_ROLE_B;
    b.peer_ip[0]=192U;b.peer_ip[1]=168U;b.peer_ip[2]=10U;b.peer_ip[3]=1U;
    assert(UDPSoundEvent_Init(100U,&b,Tick,Log));
    UDPSoundEvent_SetTimeDomain(1U);tick_ms=100U;UDPSoundEvent_Process(100U);
    EnableBusiness(BOARD_ROLE_B,11U,22U,3U,true);
    UDPSoundEvent_SetBusinessContextProvider(TestBusinessProvider);
    SoundEventRecord event={true,SOUND_EVENT_CHIRP,80U,SOUND_SIDE_B,1U,1U,5U,
                            UINT64_C(1000000000),0U,0U,0U,{0,0,0,0}};
    const unsigned before=sends;
    const unsigned before_writes=wave_pcm_writes;
    assert(SendCapturedEvent(100U,&event));
    assert(sends==before+2U && wave_pcm_writes==before_writes+1U);
    uint16_t wave_length=0U,event_length=0U;
    const uint8_t *event_packet=RecentSent(0U,&event_length);
    const uint8_t *wave_packet=RecentSent(1U,&wave_length);
    BusinessWireMessage decoded;
    assert(wave_length==BUSINESS_WIRE_HEADER_BYTES+1048U);
    assert(BusinessWire_Decode(wave_packet,wave_length,&decoded));
    assert(decoded.role==BOARD_ROLE_B && decoded.inner_length==1048U &&
           decoded.a_boot==11U && decoded.b_boot==22U && decoded.mode_seq==3U &&
           memcmp(decoded.inner,"WFR1",4U)==0);
    assert(event_length==BUSINESS_WIRE_HEADER_BYTES+SOUND_EVENT_WIRE_BYTES);
    assert(BusinessWire_Decode(event_packet,event_length,&decoded));
    assert(decoded.inner_length==SOUND_EVENT_WIRE_BYTES &&
           memcmp(decoded.inner,"SEVT",4U)==0);

    uint8_t wave_wire[BUSINESS_WIRE_HEADER_BYTES+1048U];
    uint8_t event_wire[BUSINESS_WIRE_HEADER_BYTES+SOUND_EVENT_WIRE_BYTES];
    memcpy(wave_wire,wave_packet,sizeof wave_wire);
    memcpy(event_wire,event_packet,sizeof event_wire);
    BoardIdentityProfile a=b;
    a.role=BOARD_ROLE_A;
    a.peer_ip[3]=2U;
    InitService(&a,100U);
    EnableBusiness(BOARD_ROLE_A,11U,22U,3U,true);
    UDPSoundEvent_SetBusinessContextProvider(TestBusinessProvider);
    SoundEventRecord local=event;
    local.side=SOUND_SIDE_A;
    local.time_ns-=UINT64_C(900000);
    assert(SendCapturedEvent(100U,&local));
    ReceiveSplit(wave_wire,sizeof wave_wire,false);
    ReceiveBytes(event_wire,sizeof event_wire);
    const unsigned pairs_before=pair_count;
    UDPSoundEvent_Process(101U);
    assert(pair_count==pairs_before+1U && paired.valid && paired.waveform_refined);
    puts("PASS MDB1 sound/wave: exact WFR1 bytes and refinement after chained receive");
}

static uint8_t aux_rx[128];
static uint16_t aux_rx_length;
static unsigned aux_rx_count;
/** @brief 保存MDB1解封后的CLP1/CLA1内层，供角色/世代测试断言。 */
static void AuxSink(const uint8_t *data,uint16_t length)
{
    if(length>sizeof aux_rx)return;
    memcpy(aux_rx,data,length);aux_rx_length=length;++aux_rx_count;
}
/**
 * @brief 验证CLP1/CLA1在CLAP模式下的MDB1封装、方向/世代/原始旧包拒绝。
 * @return 无返回值，失败由断言终止。
 */
static void TestBusinessShortGate(void)
{
    BoardIdentityProfile a={0},b={0};
    a.role=BOARD_ROLE_A;b.role=BOARD_ROLE_B;
    a.peer_ip[0]=b.peer_ip[0]=192U;a.peer_ip[1]=b.peer_ip[1]=168U;
    a.peer_ip[2]=b.peer_ip[2]=10U;a.peer_ip[3]=b.peer_ip[3]=2U;
    static uint8_t clp1[80],cla1[24],wrapped[BUSINESS_WIRE_MAX_BYTES];
    memset(clp1,0,sizeof clp1);memcpy(clp1,"CLP1",4U);
    memset(cla1,0,sizeof cla1);memcpy(cla1,"CLA1",4U);
    UDPSoundEvent_BusinessCounters counters;
    BusinessWireMessage decoded;

    /* A在CLAP模式发送CLP1，必须带MDB1外头且内层逐字节保留。 */
    assert(UDPSoundEvent_Init(0U,&a,Tick,Log));
    UDPSoundEvent_SetTimeDomain(1U);tick_ms=0U;UDPSoundEvent_Process(0U);
    UDPSoundEvent_SetAuxiliarySink(AuxSink);
    EnableClapBusiness(BOARD_ROLE_A,11U,22U,5U);
    UDPSoundEvent_SetBusinessContextProvider(TestBusinessProvider);
    unsigned before=sends;
    assert(UDPSoundEvent_SendProtectedShort(clp1,sizeof clp1));
    assert(sends==before+1U);
    uint16_t length=0U;
    const uint8_t *packet=RecentSent(0U,&length);
    assert(length==BUSINESS_WIRE_HEADER_BYTES+sizeof clp1);
    assert(BusinessWire_Decode(packet,length,&decoded));
    assert(decoded.role==BOARD_ROLE_A && decoded.inner_length==sizeof clp1 &&
           decoded.a_boot==11U && decoded.b_boot==22U && decoded.mode_seq==5U &&
           memcmp(decoded.inner,clp1,sizeof clp1)==0);
    EnableBusiness(BOARD_ROLE_A,11U,22U,5U,true); /* 测距模式不能发送CLP1。 */
    before=sends;
    assert(!UDPSoundEvent_SendProtectedShort(clp1,sizeof clp1));
    assert(sends==before);
    UDPSoundEvent_GetBusinessCounters(&counters);
    assert(counters.tx_blocked==1U);

    /* B在CLAP模式接收封装CLP1并交付辅助消费者。 */
    assert(UDPSoundEvent_Init(0U,&b,Tick,Log));
    UDPSoundEvent_SetTimeDomain(1U);tick_ms=0U;UDPSoundEvent_Process(0U);
    UDPSoundEvent_SetAuxiliarySink(AuxSink);
    EnableClapBusiness(BOARD_ROLE_B,11U,22U,5U);
    UDPSoundEvent_SetBusinessContextProvider(TestBusinessProvider);
    uint16_t wrapped_length=WrapBusiness(clp1,sizeof clp1,BOARD_ROLE_A,11U,22U,5U,wrapped);
    aux_rx_count=0U;
    ReceiveBytes(wrapped,wrapped_length);
    assert(aux_rx_count==1U && aux_rx_length==sizeof clp1 &&
           memcmp(aux_rx,clp1,sizeof clp1)==0);
    wrapped_length=WrapBusiness(clp1,sizeof clp1,BOARD_ROLE_A,11U,22U,6U,wrapped);
    ReceiveBytes(wrapped,wrapped_length); /* 旧世代 */
    assert(aux_rx_count==1U);
    ReceiveBytes(clp1,sizeof clp1); /* 原始旧包 */
    assert(aux_rx_count==1U);
    UDPSoundEvent_GetBusinessCounters(&counters);
    assert(counters.rx_blocked==1U && counters.rx_unwrapped==1U);
    EnableBusiness(BOARD_ROLE_B,11U,22U,5U,true); /* 测距模式不能接收CLP1。 */
    wrapped_length=WrapBusiness(clp1,sizeof clp1,BOARD_ROLE_A,11U,22U,5U,wrapped);
    ReceiveBytes(wrapped,wrapped_length);
    assert(aux_rx_count==1U);

    /* B发送CLA1，A接收；错误方向被拒绝。 */
    EnableClapBusiness(BOARD_ROLE_B,11U,22U,5U);
    before=sends;
    assert(UDPSoundEvent_SendProtectedShort(cla1,sizeof cla1));
    assert(sends==before+1U);
    packet=RecentSent(0U,&length);
    assert(length==BUSINESS_WIRE_HEADER_BYTES+sizeof cla1);
    assert(BusinessWire_Decode(packet,length,&decoded));
    assert(decoded.role==BOARD_ROLE_B && memcmp(decoded.inner,cla1,sizeof cla1)==0);
    assert(UDPSoundEvent_Init(0U,&a,Tick,Log));
    UDPSoundEvent_SetTimeDomain(1U);tick_ms=0U;UDPSoundEvent_Process(0U);
    UDPSoundEvent_SetAuxiliarySink(AuxSink);
    EnableClapBusiness(BOARD_ROLE_A,11U,22U,5U);
    UDPSoundEvent_SetBusinessContextProvider(TestBusinessProvider);
    aux_rx_count=0U;
    wrapped_length=WrapBusiness(cla1,sizeof cla1,BOARD_ROLE_B,11U,22U,5U,wrapped);
    ReceiveBytes(wrapped,wrapped_length);
    assert(aux_rx_count==1U && aux_rx_length==sizeof cla1 && memcmp(aux_rx,cla1,4U)==0);
    wrapped_length=WrapBusiness(clp1,sizeof clp1,BOARD_ROLE_B,11U,22U,5U,wrapped);
    ReceiveBytes(wrapped,wrapped_length); /* A不接受B发来的CLP1方向。 */
    assert(aux_rx_count==1U);
    puts("PASS MDB1 short: CLP1/CLA1 wrapped, direction/generation/raw/mode gates enforced");
}
/**
 * @brief 验证WVP2在WAVE模式下封装为88字节，且原始/错世代/错方向/错模式被拒绝。
 * @return 无返回值，失败由断言终止。
 */
static void TestBusinessWaveGate(void)
{
    BoardIdentityProfile a={0},b={0};
    a.role=BOARD_ROLE_A;b.role=BOARD_ROLE_B;
    a.peer_ip[0]=b.peer_ip[0]=192U;a.peer_ip[1]=b.peer_ip[1]=168U;
    a.peer_ip[2]=b.peer_ip[2]=10U;a.peer_ip[3]=b.peer_ip[3]=2U;
    static uint8_t wvp2[56],wrapped[BUSINESS_WIRE_MAX_BYTES];
    memset(wvp2,0,sizeof wvp2);memcpy(wvp2,"WVP2",4U);
    BusinessWireMessage decoded;
    UDPSoundEvent_BusinessCounters counters;

    assert(UDPSoundEvent_Init(0U,&a,Tick,Log));
    UDPSoundEvent_SetTimeDomain(1U);tick_ms=0U;UDPSoundEvent_Process(0U);
    UDPSoundEvent_SetAuxiliarySink(AuxSink);
    EnableWaveBusiness(BOARD_ROLE_A,11U,22U,7U);
    UDPSoundEvent_SetBusinessContextProvider(TestBusinessProvider);
    unsigned before=sends;
    assert(UDPSoundEvent_SendProtectedWave(wvp2,sizeof wvp2));
    assert(sends==before+1U);
    uint16_t length=0U;
    const uint8_t *packet=RecentSent(0U,&length);
    assert(length==BUSINESS_WIRE_HEADER_BYTES+sizeof wvp2);
    assert(length==88U);
    assert(BusinessWire_Decode(packet,length,&decoded));
    assert(decoded.role==BOARD_ROLE_A && decoded.inner_length==sizeof wvp2 &&
           decoded.a_boot==11U && decoded.b_boot==22U && decoded.mode_seq==7U &&
           memcmp(decoded.inner,wvp2,sizeof wvp2)==0);
    EnableBusiness(BOARD_ROLE_A,11U,22U,7U,true); /* 测距模式不能发WVP2 */
    before=sends;
    assert(!UDPSoundEvent_SendProtectedWave(wvp2,sizeof wvp2));
    assert(sends==before);

    assert(UDPSoundEvent_Init(0U,&b,Tick,Log));
    UDPSoundEvent_SetTimeDomain(1U);tick_ms=0U;UDPSoundEvent_Process(0U);
    UDPSoundEvent_SetAuxiliarySink(AuxSink);
    EnableWaveBusiness(BOARD_ROLE_B,11U,22U,7U);
    UDPSoundEvent_SetBusinessContextProvider(TestBusinessProvider);
    uint16_t wrapped_length=WrapBusiness(wvp2,sizeof wvp2,BOARD_ROLE_A,11U,22U,7U,wrapped);
    aux_rx_count=0U;
    ReceiveBytes(wrapped,wrapped_length);
    assert(aux_rx_count==1U && aux_rx_length==sizeof wvp2 &&
           memcmp(aux_rx,wvp2,sizeof wvp2)==0);
    wrapped_length=WrapBusiness(wvp2,sizeof wvp2,BOARD_ROLE_A,11U,22U,8U,wrapped);
    ReceiveBytes(wrapped,wrapped_length); /* 旧世代 */
    assert(aux_rx_count==1U);
    ReceiveBytes(wvp2,sizeof wvp2); /* 原始旧包 */
    assert(aux_rx_count==1U);
    EnableBusiness(BOARD_ROLE_B,11U,22U,7U,true); /* 测距模式不能收WVP2 */
    wrapped_length=WrapBusiness(wvp2,sizeof wvp2,BOARD_ROLE_A,11U,22U,7U,wrapped);
    ReceiveBytes(wrapped,wrapped_length);
    assert(aux_rx_count==1U);
    UDPSoundEvent_GetBusinessCounters(&counters);
    assert(counters.rx_blocked>=1U && counters.rx_unwrapped>=1U);
    EnableWaveBusiness(BOARD_ROLE_B,11U,22U,7U);
    wrapped_length=WrapBusiness(wvp2,sizeof wvp2,BOARD_ROLE_B,11U,22U,7U,wrapped);
    ReceiveBytes(wrapped,wrapped_length); /* B只接受A计划 */
    assert(aux_rx_count==1U);
    puts("PASS MDB1 wave: WVP2 56-byte inner wrapped to 88, raw/old/direction/mode rejected");
}
#endif

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
    assert(SendCapturedEvent(100U,&local));
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
    ReceiveCapturedWire(rx_payload);
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
    TestTemperatureShare();
    TestRangingTransport();
    TestDelayedPeer(&profile);
    TestExpiryAndWrap(&profile);
    TestDomainBoundary(&profile);
    TestPairingLimits(&profile);
    TestWaveWindowCut();
    TestWaveformEvidence(&profile);
#ifdef SBCG_USE_MODE_OBSERVATION
    TestModeControlWire();
    TestModeControlTransport(&profile);
    puts("PASS MDP1: wire layout, malformed fields, roles, endpoint and pbuf handling");
    TestBusinessRangingGate();
    TestBusinessSoundWaveGate();
    TestBusinessShortGate();
    TestBusinessWaveGate();
#endif
    puts("PASS udp sound event: pairing bounds, DOMN/RAK2/RSC2 same-length dispatch, RST2 and no-value ACK");
    return 0;
}
