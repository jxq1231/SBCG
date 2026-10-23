/**
 * @file ptp_board_sync.c
 * @brief 使用MAC硬件RX/TX快照完成固定A/B单播两步PTPv2交换。
 * @details 319接收事件、320接收一般消息；全部软件状态为固定CPU存储，仅LwIP分配pbuf/PCB。
 * @note 不调整硬件时钟；共享现有BSP单缓冲Begin/End令牌，无新IRQ/DMA/Cache配置。
 */
#include "ptp_board_sync.h"
#include "ptp_wire.h"
#include "BSP/bsp_ptp_tx.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#if NO_SYS != 1
#error "Board PTP exchange requires synchronous NO_SYS input/output"
#endif
#define GUARD_MS 6000U
#define PERIOD_MS 250U
#define TTL_MS 1000U
#define RETRY_MS 1000U
#define NS_PER_SEC UINT64_C(1000000000)
typedef enum { IDLE, WAIT_FU, SEND_REQ, WAIT_TX, WAIT_RESP } BState;
/** @brief 当前候选只保存CPU时间值，绝不保存帧/DMA指针。 */
typedef struct
{
    BState state;
    uint16_t sync_sequence,delay_sequence;
    uint32_t created_ms;
    SyncExchange exchange;
} Candidate;
/** @brief 不支持、失败和丢弃均以累计计数对前台可见。 */
typedef struct
{
    uint32_t invalid,foreign,correction,sequence,expired,missing,tx_error,send_error,bind_error,completed;
} Stats;
extern struct netif gnetif;
static struct udp_pcb *event_pcb,*general_pcb;
static netif_input_fn original_input;
static netif_linkoutput_fn original_output;
static BoardIdentityProfile board;
static ip_addr_t peer;
static uint8_t local_identity[10],peer_identity[10];
static uint32_t (*read_tick)(void);
static void (*emit)(const char *,int);
static bool installed,peer_reset,have_peer_sequence,have_master_time,have_output;
static unsigned int active;
static uint16_t tx_sequence,peer_sequence;
static uint32_t guard_start_ms,last_send_ms,last_bind_ms,last_log_ms,last_peer_ms,clock_epoch;
static uint64_t last_master_ns;
static Candidate candidate;
static Stats stats;
static SyncExchange output_exchange;
static uint32_t output_ms;
static struct { bool available;uint8_t data[54];uint16_t length;BSP_PtpTime stamp; } rx_context;
static struct { bool waiting,ready;uint16_t sequence;uint32_t created_ms;uint64_t ns; } sync_tx;
static struct { bool ready;uint16_t sequence;uint64_t ns;uint32_t created_ms; } response;
/** @brief 读取大端16位字段。 @param p 非NULL只读2字节。 @return 主机整数。 */
static uint16_t Get16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]<<8U|p[1]); }
/** @brief 合并MAC秒及纳秒快照。 @param t 本地时间值。 @return 纳秒整数。 */
static uint64_t Ns(BSP_PtpTime t) { return (uint64_t)t.seconds*NS_PER_SEC+t.nanoseconds; }
/** @brief 检查时间子字段。 @param t 时间值。 @return 纳秒字段有效为true。 */
static bool Valid(BSP_PtpTime t) { return t.nanoseconds<1000000000U; }
/** @brief 检查序号是否在16位向前半空间。 @param n 新序号。 @param old 已收水位。 @return 严格较新为true。 */
static bool Newer(uint16_t n,uint16_t old) { uint16_t d=(uint16_t)(n-old);return d!=0U&&d<32768U; }
/** @brief 判断启动/重新获取静默是否结束。 @param now 当前毫秒。 @return 允许交换为true。 */
static bool GuardDone(uint32_t now) { return (uint32_t)(now-guard_start_ms)>=GUARD_MS; }
/** @brief 清除跨时钟修订不可用的CPU状态。 @return 无返回值。 @note 不复位任何序号水位。 */
static void ClearPending(void)
{ memset(&candidate,0,sizeof(candidate));memset(&sync_tx,0,sizeof(sync_tx));memset(&response,0,sizeof(response));rx_context.available=false;have_output=false; }
/** @brief 对端异常后重新静默获取。 @param now 当前毫秒。 @return 无返回值。 */
static void ResetPeer(uint32_t now)
{ ClearPending();have_peer_sequence=false;have_master_time=false;peer_reset=true;guard_start_ms=now; }
/** @brief 清理过期候选。 @param now HAL毫秒。 @return 无返回值。 */
static void Expire(uint32_t now)
{
    if(candidate.state!=IDLE&&(uint32_t)(now-candidate.created_ms)>=TTL_MS) { candidate.state=IDLE;++stats.expired; }
    if((sync_tx.waiting||sync_tx.ready)&&(uint32_t)(now-sync_tx.created_ms)>=TTL_MS) { sync_tx.waiting=sync_tx.ready=false;++stats.expired; }
    if(response.ready&&(uint32_t)(now-response.created_ms)>=TTL_MS) { response.ready=false;++stats.expired; }
    if(have_output&&(uint32_t)(now-output_ms)>=TTL_MS) { have_output=false;++stats.expired; }
}
/**
 * @brief 从以太网帧筛出本模块固定端点报文。
 * @param packet 借用pbuf，不改动、不释放，可为NULL并拒绝。
 * @param netif 借用网卡，仅接受gnetif。
 * @param outgoing true为本机发往登记对端。
 * @param data 调用者提供的54字节输出。
 * @param length 成功时输出消息字节数。
 * @return true表示封装和消息有效。
 * @note 只决定硬件戳资格；真实LwIP输入继续负责协议校验和报文所有权。
 */
static bool Frame(const struct pbuf *packet,const struct netif *netif,bool outgoing,uint8_t data[54],uint16_t *length)
{
    uint8_t header[34],udp[62];
    if(netif!=&gnetif||packet==NULL||packet->tot_len<86U||pbuf_copy_partial(packet,header,34,0)!=34U) { return false; }
    const uint8_t *ip=header+14;uint16_t ihl=(uint16_t)((ip[0]&15U)*4U);
    if(Get16(header+12)!=0x0800U||(ip[0]>>4U)!=4U||ihl<20U||ip[9]!=17U||(Get16(ip+6)&0xBFFFU)!=0U||
       memcmp(ip+12,outgoing?board.local_ip:board.peer_ip,4)!=0||memcmp(ip+16,outgoing?board.peer_ip:board.local_ip,4)!=0) { return false; }
    uint16_t total=Get16(ip+2);
    if(total<ihl+8U||total>ihl+62U||packet->tot_len<14U+total) { return false; }
    uint16_t size=(uint16_t)(total-ihl);
    if(pbuf_copy_partial(packet,udp,size,(u16_t)(14U+ihl))!=size||Get16(udp+4)!=size) { return false; }
    PtpWireMessage m;
    if(PtpWire_Decode(udp+8,size-8U,&m)!=PTP_WIRE_OK) { return false; }
    const uint16_t port=(m.type==PTP_WIRE_SYNC||m.type==PTP_WIRE_DELAY_REQ)?319U:320U;
    if(Get16(udp)!=port||Get16(udp+2)!=port||memcmp(m.source,outgoing?local_identity:peer_identity,10)!=0) { return false; }
    *length=(uint16_t)(size-8U);memcpy(data,udp+8,*length);return true;
}
/**
 * @brief 进入同步协议栈前捕获当前事件帧的RX快照。
 * @param packet 原输入借用pbuf；所有权交由原函数。
 * @param netif 原输入网卡。
 * @return 原输入错误码。
 * @note RX上下文只在本次输入链有效；非事件消息不能认领旧快照。
 */
static err_t Input(struct pbuf *packet,struct netif *netif)
{
    ++active;rx_context.available=false;
    if(Frame(packet,netif,false,rx_context.data,&rx_context.length)&&
       (rx_context.data[0]==PTP_WIRE_SYNC||rx_context.data[0]==PTP_WIRE_DELAY_REQ))
    {
        BSP_PtpTime after;
        rx_context.available=BSP_PTP_ReadRx(&rx_context.stamp)==BSP_PTP_OK&&Valid(rx_context.stamp)&&
            BSP_PTP_Read(&after)==BSP_PTP_OK&&Valid(after)&&Ns(after)>=Ns(rx_context.stamp);
    }
    err_t result=original_input(packet,netif);rx_context.available=false;--active;return result;
}
/**
 * @brief 围绕实际单缓冲链路发送获取一次TX硬件戳。
 * @param netif 借用的输出网卡。
 * @param packet 实际发送帧，借用且不释放。
 * @return 原链路输出错误码。
 * @note 非本模块报文完整穿过旧钩子；失败/超时不得重用同序号申请第二个硬件戳。
 */
static err_t Output(struct netif *netif,struct pbuf *packet)
{
    ++active;Expire(read_tick());uint8_t data[54];uint16_t length;bool match=false,is_sync=false;
    if(Frame(packet,netif,true,data,&length))
    {
        uint16_t seq=Get16(data+30);
        is_sync=data[0]==PTP_WIRE_SYNC;
        match=(board.role==BOARD_ROLE_A&&is_sync&&sync_tx.waiting&&sync_tx.sequence==seq)||
              (board.role==BOARD_ROLE_B&&data[0]==PTP_WIRE_DELAY_REQ&&candidate.state==WAIT_TX&&candidate.delay_sequence==seq);
    }
    bool armed=false;
    if(match)
    {
        if(is_sync) { sync_tx.waiting=false; } else { candidate.state=IDLE; }
        if(packet->next==NULL&&packet->len==packet->tot_len) { armed=BSP_PTP_TxBegin(packet->payload,packet->len)==BSP_PTP_OK; }
        if(!armed) { ++stats.tx_error; }
    }
    err_t result=original_output(netif,packet);
    if(armed)
    {
        BSP_PtpTime stamp;BSP_PtpStatus status=BSP_PTP_TxEnd(&stamp);
        if(result==ERR_OK&&status==BSP_PTP_OK&&Valid(stamp))
        {
            if(is_sync) { sync_tx.ns=Ns(stamp);sync_tx.ready=true; }
            else if(Ns(stamp)>=candidate.exchange.t2_b_rx_ns)
            { candidate.exchange.t3_b_tx_ns=Ns(stamp);candidate.state=WAIT_RESP; }
            else { ++stats.tx_error; }
        }
        else { ++stats.tx_error; }
    }
    --active;return result;
}
/**
 * @brief 分配临时pbuf并向登记对端发送标准消息。
 * @param message 只读CPU消息，调用期间有效，不可为NULL。
 * @return LwIP提交结果；成功不等于已经取得TX硬件戳。
 * @note 自己的pbuf引用始终释放，ARP可能保留独立引用；无自有堆状态。
 */
static err_t Send(const PtpWireMessage *message)
{
    uint8_t data[54];size_t size=PtpWire_Encode(message,data);if(size==0U) { return ERR_VAL; }
    struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,(u16_t)size,PBUF_RAM);if(packet==NULL) { ++stats.send_error;return ERR_MEM; }
    bool event=message->type==PTP_WIRE_SYNC||message->type==PTP_WIRE_DELAY_REQ;
    err_t result=pbuf_take(packet,data,(u16_t)size);
    if(result==ERR_OK) { result=udp_sendto(event?event_pcb:general_pcb,packet,&peer,event?319U:320U); }
    pbuf_free(packet);if(result!=ERR_OK) { ++stats.send_error; }return result;
}
/** @brief 构造本机消息公共字段。 @param type 消息类型。 @param seq 序号。 @return CPU值副本。 */
static PtpWireMessage Message(PtpWireType type,uint16_t seq)
{ PtpWireMessage m={0};m.type=type;m.sequence=seq;memcpy(m.source,local_identity,10);return m; }
/**
 * @brief 消费当前同步输入中已验证端点的协议消息。
 * @param arg 未使用回调上下文。
 * @param pcb 当前绑定PCB，用于区分事件/一般消息。
 * @param packet 接收pbuf；可为NULL，否则每条路径释放自己的引用。
 * @param source 回调期间有效的源IP，可为NULL并拒绝。
 * @param source_port 源UDP端口，必须与对应PCB端口相同。
 * @return 无返回值。
 * @note 只复制CPU字段；时钟校正和格式化均由前台处理，不在本回调执行。
 */
static void Receive(void *arg,struct udp_pcb *pcb,struct pbuf *packet,const ip_addr_t *source,u16_t source_port)
{
    (void)arg;if(packet==NULL) { return; }uint8_t data[54];uint16_t length=packet->tot_len;
    bool endpoint=source!=NULL&&ip_addr_cmp(source,&peer)&&source_port==(pcb==event_pcb?319U:320U);
    bool copied=length<=54U&&pbuf_copy_partial(packet,data,length,0)==length;
    pbuf_free(packet);
    if(!endpoint) { ++stats.foreign;rx_context.available=false;return; }
    PtpWireMessage m;PtpWireStatus status=copied?PtpWire_Decode(data,length,&m):PTP_WIRE_INVALID;
    if(status!=PTP_WIRE_OK) { if(status==PTP_WIRE_CORRECTION) { ++stats.correction; }else { ++stats.invalid; }rx_context.available=false;return; }
    bool is_event=m.type==PTP_WIRE_SYNC||m.type==PTP_WIRE_DELAY_REQ;
    if(memcmp(m.source,peer_identity,10)!=0||is_event!=(pcb==event_pcb)) { ++stats.foreign;rx_context.available=false;return; }
    bool rx_valid=rx_context.available&&rx_context.length==length&&memcmp(rx_context.data,data,length)==0;
    uint64_t rx_ns=rx_valid?Ns(rx_context.stamp):0;rx_context.available=false;
    uint32_t now=read_tick();Expire(now);if(!GuardDone(now)) { return; }
    if(board.role==BOARD_ROLE_A)
    {
        if(m.type!=PTP_WIRE_DELAY_REQ) { ++stats.invalid;return; }
        if(have_peer_sequence&&!Newer(m.sequence,peer_sequence)) { ++stats.sequence;return; }
        if(!rx_valid) { ++stats.missing;return; }
        if(response.ready) { ++stats.sequence;return; }
        peer_sequence=m.sequence;have_peer_sequence=true;last_peer_ms=now;
        response.ready=true;response.sequence=m.sequence;response.ns=rx_ns;response.created_ms=now;return;
    }
    if(m.type==PTP_WIRE_SYNC)
    {
        if(candidate.state!=IDLE||have_output||(have_peer_sequence&&!Newer(m.sequence,peer_sequence))) { ++stats.sequence;return; }
        if(!rx_valid) { ++stats.missing;return; }
        peer_sequence=m.sequence;have_peer_sequence=true;last_peer_ms=now;
        memset(&candidate,0,sizeof(candidate));candidate.state=WAIT_FU;candidate.sync_sequence=m.sequence;candidate.created_ms=now;candidate.exchange.t2_b_rx_ns=rx_ns;return;
    }
    if(m.type==PTP_WIRE_FOLLOW_UP)
    {
        if(candidate.state!=WAIT_FU||candidate.sync_sequence!=m.sequence) { ++stats.sequence;return; }
        if(have_master_time&&m.timestamp_ns<=last_master_ns) { ++stats.invalid;ResetPeer(now);return; }
        last_master_ns=m.timestamp_ns;have_master_time=true;
        candidate.exchange.t1_a_tx_ns=m.timestamp_ns;candidate.state=SEND_REQ;return;
    }
    if(m.type==PTP_WIRE_DELAY_RESP)
    {
        if(candidate.state!=WAIT_RESP||candidate.delay_sequence!=m.sequence||memcmp(m.requester,local_identity,10)!=0) { ++stats.sequence;return; }
        if(m.timestamp_ns<candidate.exchange.t1_a_tx_ns||m.timestamp_ns-candidate.exchange.t1_a_tx_ns>UINT64_C(800000000)||
           candidate.exchange.t3_b_tx_ns<candidate.exchange.t2_b_rx_ns||candidate.exchange.t3_b_tx_ns-candidate.exchange.t2_b_rx_ns>UINT64_C(800000000))
        { ++stats.invalid;candidate.state=IDLE;return; }
        candidate.exchange.t4_a_rx_ns=m.timestamp_ns;output_exchange=candidate.exchange;output_ms=now;have_output=true;candidate.state=IDLE;++stats.completed;return;
    }
    ++stats.invalid;
}
bool PtpBoardSync_Deinit(void)
{
    if(active!=0U) { return false; }
    if(installed&&(gnetif.input!=Input||gnetif.linkoutput!=Output)) { return false; }
    if(installed) { gnetif.input=original_input;gnetif.linkoutput=original_output; }
    if(event_pcb!=NULL) { udp_remove(event_pcb);event_pcb=NULL; }
    if(general_pcb!=NULL) { udp_remove(general_pcb);general_pcb=NULL; }
    installed=false;ClearPending();return true;
}
bool PtpBoardSync_Init(uint32_t now,const BoardIdentityProfile *profile,uint32_t (*tick)(void),void (*log)(const char *,int))
{
    if(!PtpBoardSync_Deinit()) { return false; }
    BoardIdentityProfile expected,registered_peer;
    if(profile==NULL||tick==NULL||!BoardIdentity_Resolve(&BoardIdentity_DefaultMap,profile->uid_words,&expected)||
       expected.role!=profile->role||memcmp(expected.local_ip,profile->local_ip,4)!=0||memcmp(expected.peer_ip,profile->peer_ip,4)!=0||
       memcmp(expected.local_mac,profile->local_mac,6)!=0||memcmp(expected.peer_uid_words,profile->peer_uid_words,12)!=0||
       !BoardIdentity_Resolve(&BoardIdentity_DefaultMap,profile->peer_uid_words,&registered_peer)||
       gnetif.input==NULL||gnetif.linkoutput==NULL||gnetif.input==Input||gnetif.linkoutput==Output) { return false; }
    board=*profile;read_tick=tick;emit=log;memset(&stats,0,sizeof(stats));
    PtpWire_Identity(board.local_mac,local_identity);
    PtpWire_Identity(registered_peer.local_mac,peer_identity);
    IP_ADDR4(&peer,board.peer_ip[0],board.peer_ip[1],board.peer_ip[2],board.peer_ip[3]);
    peer_reset=have_peer_sequence=have_master_time=false;clock_epoch=0;tx_sequence=0;
    guard_start_ms=last_peer_ms=last_log_ms=now;last_send_ms=now-PERIOD_MS;last_bind_ms=now-RETRY_MS;
    original_input=gnetif.input;original_output=gnetif.linkoutput;gnetif.input=Input;gnetif.linkoutput=Output;installed=true;return true;
}
/** @brief 创建并绑定一个网卡专属PCB。 @param port 本地端口。 @return 成功PCB或NULL，失败自动释放。 */
static struct udp_pcb *Bind(uint16_t port)
{
    struct udp_pcb *pcb=udp_new();if(pcb==NULL) { return NULL; }
    if(udp_bind(pcb,netif_ip_addr4(&gnetif),port)!=ERR_OK) { udp_remove(pcb);return NULL; }
    udp_bind_netif(pcb,&gnetif);udp_recv(pcb,Receive,NULL);return pcb;
}
/** @brief 将64位纳秒转为安全十进制文本。 @param value 输入纳秒。 @param text 至少21字节输出。 @return 缓冲内首字符指针。 */
static const char *Decimal(uint64_t value,char text[21])
{ char *p=text+20;*p=0;do { *--p=(char)('0'+value%10U);value/=10U; }while(value!=0U);return p; }
/** @brief 限速输出传输累计计数。 @param now HAL毫秒计数。 @return 无返回值。 */
static void LogStats(uint32_t now)
{
    if(emit==NULL||(uint32_t)(now-last_log_ms)<1000U) { return; }last_log_ms=now;char line[256];
    int n=snprintf(line,sizeof(line),"$PTP_SYNC_STATS,OK=%" PRIu32 ",BAD=%" PRIu32 ",FOREIGN=%" PRIu32 ",CORRECTION=%" PRIu32 ",SEQ=%" PRIu32 ",EXPIRED=%" PRIu32 ",MISSING=%" PRIu32 ",TX_ERR=%" PRIu32 ",SEND_ERR=%" PRIu32 ",BIND_ERR=%" PRIu32 ",EPOCH=%" PRIu32 "\r\n",
        stats.completed,stats.invalid,stats.foreign,stats.correction,stats.sequence,stats.expired,stats.missing,stats.tx_error,stats.send_error,stats.bind_error,clock_epoch);
    if(n>0&&(size_t)n<sizeof(line)) { emit(line,n); }
}
void PtpBoardSync_Process(uint32_t now)
{
    if(!installed||active!=0U) { return; }Expire(now);
    if(!netif_is_up(&gnetif)||!netif_is_link_up(&gnetif)) { ClearPending();LogStats(now);return; }
    if((event_pcb==NULL||general_pcb==NULL)&&(uint32_t)(now-last_bind_ms)>=RETRY_MS)
    {
        last_bind_ms=now;if(event_pcb==NULL) { event_pcb=Bind(319); }if(general_pcb==NULL) { general_pcb=Bind(320); }
        if(event_pcb==NULL||general_pcb==NULL) { ++stats.bind_error; }
    }
    if(have_peer_sequence&&(uint32_t)(now-last_peer_ms)>=GUARD_MS) { ResetPeer(now); }
    if(event_pcb==NULL||general_pcb==NULL||!GuardDone(now)) { LogStats(now);return; }
    if(board.role==BOARD_ROLE_A)
    {
        if(response.ready)
        {
            PtpWireMessage m=Message(PTP_WIRE_DELAY_RESP,response.sequence);m.timestamp_ns=response.ns;memcpy(m.requester,peer_identity,10);response.ready=false;(void)Send(&m);
        }
        if(!sync_tx.waiting&&!sync_tx.ready&&(uint32_t)(now-last_send_ms)>=PERIOD_MS)
        {
            last_send_ms=now;sync_tx.sequence=++tx_sequence;sync_tx.created_ms=now;sync_tx.waiting=true;
            PtpWireMessage m=Message(PTP_WIRE_SYNC,sync_tx.sequence);
            if(Send(&m)!=ERR_OK) { sync_tx.waiting=sync_tx.ready=false; }
        }
        if(sync_tx.ready)
        {
            PtpWireMessage m=Message(PTP_WIRE_FOLLOW_UP,sync_tx.sequence);m.timestamp_ns=sync_tx.ns;sync_tx.ready=false;(void)Send(&m);
        }
    }
    else if(candidate.state==SEND_REQ)
    {
        candidate.delay_sequence=++tx_sequence;candidate.state=WAIT_TX;PtpWireMessage m=Message(PTP_WIRE_DELAY_REQ,candidate.delay_sequence);
        if(Send(&m)!=ERR_OK) { candidate.state=IDLE; }
    }
    LogStats(now);
}
bool PtpBoardSync_TakeExchange(SyncExchange *exchange,uint32_t *captured_ms)
{
    if(!installed||active!=0U||exchange==NULL||captured_ms==NULL) { return false; }Expire(read_tick());if(!have_output) { return false; }
    *exchange=output_exchange;*captured_ms=output_ms;have_output=false;
    if(emit!=NULL)
    {
        char a[21],b[21],c[21],d[21],line[192];int n=snprintf(line,sizeof(line),"$PTP_SYNC_RAW,T1_NS=%s,T2_NS=%s,T3_NS=%s,T4_NS=%s,EPOCH=%" PRIu32 "\r\n",
            Decimal(exchange->t1_a_tx_ns,a),Decimal(exchange->t2_b_rx_ns,b),Decimal(exchange->t3_b_tx_ns,c),Decimal(exchange->t4_a_rx_ns,d),clock_epoch);
        if(n>0&&(size_t)n<sizeof(line)) { emit(line,n); }
    }
    return true;
}
void PtpBoardSync_NotifyClockChange(uint32_t epoch)
{ if(active!=0U) { return; }clock_epoch=epoch;ClearPending(); }
bool PtpBoardSync_TakePeerReset(void)
{ bool result=peer_reset;peer_reset=false;return result; }
