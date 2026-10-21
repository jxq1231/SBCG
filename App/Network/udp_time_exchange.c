#include "udp_time_exchange.h"
#include "udp_config.h"
#include "BSP/bsp_ptp_tx.h"
#include "Sync/sync_observation.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#if NO_SYS != 1
#error "Time exchange requires synchronous NO_SYS input/output delivery"
#endif

#define KEY_BYTES 36U
#define REPORT_BYTES UDP_TIME_EXCHANGE_REPORT_BYTES
#define SUBMIT_BYTES UDP_TIME_EXCHANGE_SUBMIT_BYTES
#define RECORD_COUNT 4U
#define RECORD_TTL_MS SYNC_OBSERVATION_TTL_MS
#define SERVICE_PERIOD_MS 1000U
#define LOG_CAPACITY 256U
#define NSEC_PER_SEC 1000000000U

typedef enum { EMPTY, WAIT_ACK, READY, REPORTED, SUBMITTED, DONE } RecordState;
typedef struct
{
    RecordState state;
    uint8_t key[KEY_BYTES];
    BSP_PtpTime rx, tx;
    uint32_t created_ms;
    uint64_t submitted_t1_ns, submitted_t4_ns;
} Record;
typedef struct
{
    uint32_t req_ok, req_bad, foreign, duplicate, conflict, ack_ok, ack_err, init_err;
} IoStats;
typedef struct
{
    uint32_t rx_ok, rx_invalid, tx_ok, tx_err, unsupported;
    uint32_t report_ok, report_err, expired, overflow;
} PairStats;

extern struct netif gnetif;
static struct udp_pcb *service_pcb;
static netif_input_fn original_input;
static netif_linkoutput_fn original_output;
static UDPTimeExchange_Tick read_tick;
static UDPTimeExchange_Log emit;
static bool installed, configured;
static unsigned int active_callbacks;
static uint8_t uid_wire[12];
static ip_addr_t peer;
static Record records[RECORD_COUNT];
typedef struct
{
    uint32_t accepted, bad, foreign, no_pair, early, duplicate, conflict, queue_full;
} ObservationStats;
static ObservationStats observation;
static IoStats io;
static PairStats pair;
static uint32_t last_attempt_ms, last_log_ms;
static struct
{
    bool available, valid;
    uint8_t key[KEY_BYTES];
    BSP_PtpTime rx;
} context;

static uint16_t Get16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0]<<8U | p[1]); }
static uint32_t Get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24U | (uint32_t)p[1]<<16U | (uint32_t)p[2]<<8U | p[3]; }
static uint64_t Get64(const uint8_t *p)
{ return (uint64_t)Get32(p)<<32U | Get32(p+4); }
static void Put32(uint8_t *p,uint32_t n)
{ p[0]=(uint8_t)(n>>24U); p[1]=(uint8_t)(n>>16U); p[2]=(uint8_t)(n>>8U); p[3]=(uint8_t)n; }
static bool TimeValid(BSP_PtpTime value) { return value.nanoseconds<NSEC_PER_SEC; }
static bool TimeAtLeast(BSP_PtpTime later,BSP_PtpTime earlier)
{ return later.seconds>earlier.seconds || (later.seconds==earlier.seconds && later.nanoseconds>=earlier.nanoseconds); }
static bool RequestValid(const uint8_t key[KEY_BYTES])
{
    return memcmp(key,"SBCG\1\4\0\44",8)==0 && Get32(key+8)!=0U &&
           memcmp(key+16,uid_wire,sizeof(uid_wire))==0 && (Get32(key+28)!=0U || Get32(key+32)!=0U);
}

/* This selector only qualifies observation; the original LwIP input still
   validates/checksums and owns normal delivery. Ethernet padding is permitted. */
static bool FrameKey(const struct pbuf *packet,const struct netif *netif,
                     bool outgoing,uint8_t key[KEY_BYTES])
{
    uint8_t header[34],udp[8U+KEY_BYTES];
    if(netif!=&gnetif || packet==NULL || packet->tot_len<78U ||
       pbuf_copy_partial(packet,header,sizeof(header),0)!=sizeof(header)) { return false; }
    const uint8_t *ip=header+14;
    const uint32_t ip_header_bytes=(ip[0]&15U)*4U;
    const uint8_t pc[4]={UDP_HEARTBEAT_DEST_A,UDP_HEARTBEAT_DEST_B,
                         UDP_HEARTBEAT_DEST_C,UDP_HEARTBEAT_DEST_D};
    ip4_addr_t board;
    memcpy(&board.addr,ip+(outgoing ? 12:16),sizeof(board.addr));
    if(Get16(header+12)!=0x0800U || (ip[0]>>4U)!=4U || ip_header_bytes<20U || ip[9]!=17U ||
       (Get16(ip+6)&0xBFFFU)!=0U || Get16(ip+2)!=ip_header_bytes+sizeof(udp) ||
       packet->tot_len<14U+ip_header_bytes+sizeof(udp) ||
       !ip4_addr_cmp(&board,netif_ip4_addr(netif)) || memcmp(ip+(outgoing ? 16:12),pc,4)!=0 ||
       pbuf_copy_partial(packet,udp,sizeof(udp),(u16_t)(14U+ip_header_bytes))!=sizeof(udp) ||
       Get16(udp)!=(outgoing ? UDP_TIME_EXCHANGE_BOARD_PORT:UDP_TIME_EXCHANGE_PC_PORT) ||
       Get16(udp+2)!=(outgoing ? UDP_TIME_EXCHANGE_PC_PORT:UDP_TIME_EXCHANGE_BOARD_PORT) ||
       Get16(udp+4)!=sizeof(udp)) { return false; }
    memcpy(key,udp+8,KEY_BYTES);
    if(outgoing)
    {
        if(key[5]!=5U) { return false; }
        key[5]=4U; /* All records store the canonical REQUEST key. */
    }
    return RequestValid(key);
}

static void Expire(uint32_t now_ms)
{
    for(unsigned int i=0;i<RECORD_COUNT;++i)
    {
        Record *record=&records[i];
        if(record->state!=EMPTY && (uint32_t)(now_ms-record->created_ms)>=RECORD_TTL_MS)
        {
            if(record->state==WAIT_ACK || record->state==READY) { ++pair.expired; }
            record->state=EMPTY;
        }
    }
}

static err_t Input(struct pbuf *packet,struct netif *netif)
{
    ++active_callbacks;
    context.available=false;
    if(FrameKey(packet,netif,false,context.key))
    {
        BSP_PtpTime after;
        context.valid=BSP_PTP_ReadRx(&context.rx)==BSP_PTP_OK && TimeValid(context.rx);
        if(context.valid)
        {
            context.valid=BSP_PTP_Read(&after)==BSP_PTP_OK && TimeValid(after) && TimeAtLeast(after,context.rx);
        }
        context.available=true;
    }
    const err_t result=original_input(packet,netif);
    /* Never allow the next callback/frame to claim the previous DMA snapshot. */
    context.available=false;
    --active_callbacks;
    return result;
}

static err_t Output(struct netif *netif,struct pbuf *packet)
{
    ++active_callbacks;
    uint8_t key[KEY_BYTES];
    Record *record=NULL;
    Expire(read_tick());
    if(FrameKey(packet,netif,true,key))
    {
        for(unsigned int i=0;i<RECORD_COUNT;++i)
        {
            if(records[i].state==WAIT_ACK && memcmp(records[i].key,key,KEY_BYTES)==0)
            { record=&records[i]; break; }
        }
    }
    bool armed=false;
    if(record!=NULL)
    {
        /* Failure always leaves a short-lived tombstone, never a retry job. */
        record->state=DONE;
        if(packet->next!=NULL || packet->len!=packet->tot_len) { ++pair.unsupported; }
        else
        {
            armed=BSP_PTP_TxBegin(packet->payload,packet->len)==BSP_PTP_OK;
            if(!armed) { ++pair.tx_err; }
        }
    }
    /* The legacy wrapper's TxClean may return NOT_READY while our token is
       armed. Its type=3 selector ignores this type=5 frame and does not Begin. */
    const err_t result=original_output(netif,packet);
    if(armed)
    {
        BSP_PtpTime tx;
        const BSP_PtpStatus status=BSP_PTP_TxEnd(&tx);
        if(result==ERR_OK && status==BSP_PTP_OK && TimeValid(tx) && TimeAtLeast(tx,record->rx))
        { record->tx=tx; record->state=READY; ++pair.tx_ok; }
        else { ++pair.tx_err; }
    }
    --active_callbacks;
    return result;
}

static err_t Send(const uint8_t *data,uint16_t length)
{
    struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,length,PBUF_RAM);
    if(packet==NULL) { return ERR_MEM; }
    err_t result=pbuf_take(packet,data,length);
    if(result==ERR_OK) { result=udp_sendto(service_pcb,packet,&peer,UDP_TIME_EXCHANGE_PC_PORT); }
    /* Raw UDP keeps our reference with us; ARP owns any additional queued ref. */
    pbuf_free(packet);
    return result;
}

/* A submit has no DMA timestamp of its own. Only the completed local record
   can supply t2/t3; copying it into the CPU queue transfers no pbuf ownership. */
static void ReceiveObservation(uint8_t data[SUBMIT_BYTES],uint32_t now_ms)
{
    const uint64_t t1_ns=Get64(data+36),t4_ns=Get64(data+44);
    if(data[6]!=0U || data[7]!=SUBMIT_BYTES)
    { ++observation.bad; return; }
    data[5]=4U; data[7]=KEY_BYTES;
    if(!RequestValid(data) || (uint32_t)(t1_ns/UINT64_C(1000000))!=Get32(data+12))
    { ++observation.bad; return; }
    Expire(now_ms);
    for(unsigned int i=0;i<RECORD_COUNT;++i)
    {
        Record *record=&records[i];
        if(record->state==EMPTY || memcmp(record->key,data,KEY_BYTES)!=0) { continue; }
        if(record->state==SUBMITTED)
        {
            if(record->submitted_t1_ns==t1_ns && record->submitted_t4_ns==t4_ns)
            { ++observation.duplicate; }
            else { ++observation.conflict; }
            return;
        }
        if(record->state==WAIT_ACK || record->state==READY)
        { ++observation.early; return; }
        if(record->state!=REPORTED) { break; }
        SyncObservationInput input={0};
        input.sequence=Get32(data+8); input.client_tick_ms=Get32(data+12);
        for(unsigned int j=0;j<3U;++j) { input.uid_words[j]=Get32(data+16+j*4U); }
        input.session_hi=Get32(data+28); input.session_lo=Get32(data+32);
        input.created_ms=record->created_ms;
        input.exchange=(SyncExchange){t1_ns,
            (uint64_t)record->rx.seconds*NSEC_PER_SEC+record->rx.nanoseconds,
            (uint64_t)record->tx.seconds*NSEC_PER_SEC+record->tx.nanoseconds,t4_ns};
        if(!SyncObservation_Submit(&input)) { ++observation.queue_full; return; }
        record->submitted_t1_ns=t1_ns; record->submitted_t4_ns=t4_ns;
        record->state=SUBMITTED; ++observation.accepted;
        return;
    }
    ++observation.no_pair;
}

static void ReceivePacket(struct pbuf *packet,const ip_addr_t *source,u16_t source_port)
{
    if(packet==NULL) { return; }
    uint8_t data[SUBMIT_BYTES];
    const u16_t length=packet->tot_len<SUBMIT_BYTES ? packet->tot_len:SUBMIT_BYTES;
    const u16_t copied=pbuf_copy_partial(packet,data,length,0);
    const bool is_submit=copied>=6U && data[5]==7U;
    if(source==NULL || !ip_addr_cmp(source,&peer) || source_port!=UDP_TIME_EXCHANGE_PC_PORT)
    {
        if(is_submit) { ++observation.foreign; } else { ++io.foreign; }
        pbuf_free(packet); return;
    }
    if(is_submit)
    {
        const bool complete=packet->tot_len==SUBMIT_BYTES && copied==SUBMIT_BYTES;
        pbuf_free(packet); context.available=false;
        if(complete) { ReceiveObservation(data,read_tick()); } else { ++observation.bad; }
        return;
    }
    if(packet->tot_len!=KEY_BYTES || copied!=KEY_BYTES || !RequestValid(data))
    { ++io.req_bad; pbuf_free(packet); return; }
    const bool rx_valid=context.available && context.valid && memcmp(context.key,data,KEY_BYTES)==0;
    context.available=false; /* Consume even a mismatched claim. */
    pbuf_free(packet);
    const uint32_t now_ms=read_tick();
    Expire(now_ms);
    Record *slot=NULL;
    for(unsigned int i=0;i<RECORD_COUNT;++i)
    {
        Record *record=&records[i];
        if(record->state==EMPTY) { if(slot==NULL) { slot=record; } continue; }
        /* SESSION + UID + SEQ is identity; a differing tick is a conflict. */
        if(memcmp(record->key+8,data+8,4)==0 && memcmp(record->key+16,data+16,20)==0)
        {
            if(memcmp(record->key,data,KEY_BYTES)==0) { ++io.duplicate; }
            else { ++io.conflict; }
            return;
        }
    }
    ++io.req_ok;
    if(rx_valid) { ++pair.rx_ok; } else { ++pair.rx_invalid; }
    if(slot!=NULL)
    {
        memcpy(slot->key,data,KEY_BYTES); slot->created_ms=now_ms;
        slot->state=rx_valid ? WAIT_ACK:DONE;
        if(rx_valid) { slot->rx=context.rx; }
    }
    else { ++pair.overflow; }
    data[5]=5U;
    if(Send(data,KEY_BYTES)==ERR_OK) { ++io.ack_ok; }
    else { ++io.ack_err; if(slot!=NULL) { slot->state=DONE; } }
}

static void Receive(void *arg,struct udp_pcb *pcb,struct pbuf *packet,
                    const ip_addr_t *source,u16_t source_port)
{
    (void)arg; (void)pcb;
    ++active_callbacks;
    ReceivePacket(packet,source,source_port);
    --active_callbacks;
}

bool UDPTimeExchange_Deinit(void)
{
    if(active_callbacks!=0U || (installed && (gnetif.input!=Input || gnetif.linkoutput!=Output)))
    { ++io.init_err; return false; }
    if(installed)
    { gnetif.input=original_input; gnetif.linkoutput=original_output; installed=false; }
    if(service_pcb!=NULL) { udp_remove(service_pcb); service_pcb=NULL; }
    memset(records,0,sizeof(records)); context.available=false; configured=false;
    return true;
}

bool UDPTimeExchange_Init(uint32_t now_ms,const uint32_t uid_words[3],
                          UDPTimeExchange_Tick tick,UDPTimeExchange_Log log)
{
    if(!UDPTimeExchange_Deinit()) { return false; }
    emit=log; read_tick=tick; last_log_ms=now_ms; last_attempt_ms=now_ms-SERVICE_PERIOD_MS;
    memset(&io,0,sizeof(io)); memset(&pair,0,sizeof(pair));
    memset(&observation,0,sizeof(observation));
    if(uid_words==NULL || (uid_words[0]==0U && uid_words[1]==0U && uid_words[2]==0U) ||
       tick==NULL || gnetif.input==NULL || gnetif.linkoutput==NULL ||
       gnetif.input==Input || gnetif.linkoutput==Output)
    { ++io.init_err; return false; }
    for(unsigned int i=0;i<3U;++i) { Put32(uid_wire+i*4U,uid_words[i]); }
    IP_ADDR4(&peer,UDP_HEARTBEAT_DEST_A,UDP_HEARTBEAT_DEST_B,UDP_HEARTBEAT_DEST_C,UDP_HEARTBEAT_DEST_D);
    original_input=gnetif.input; original_output=gnetif.linkoutput;
    gnetif.input=Input; gnetif.linkoutput=Output; installed=configured=true;
    return true;
}

static void LogStats(uint32_t now_ms)
{
    if(emit==NULL || (uint32_t)(now_ms-last_log_ms)<SERVICE_PERIOD_MS) { return; }
    last_log_ms=now_ms;
    /* Maximal 32-bit counters: literals plus eight/nine 10-digit fields. */
    _Static_assert(sizeof("$TS_IO,REQ_OK=,REQ_BAD=,FOREIGN=,DUPLICATE=,CONFLICT=,ACK_OK=,ACK_ERR=,INIT_ERR=\r\n")+8U*10U<=LOG_CAPACITY,
                   "IO statistics exceed log buffer");
    _Static_assert(sizeof("$TS_PAIR,RX_OK=,RX_INVALID=,TX_OK=,TX_ERR=,UNSUPPORTED=,REPORT_OK=,REPORT_ERR=,EXPIRED=,OVERFLOW=\r\n")+9U*10U<=LOG_CAPACITY,
                   "Pair statistics exceed log buffer");
    char line[LOG_CAPACITY];
    int length=snprintf(line,sizeof(line),
        "$TS_IO,REQ_OK=%" PRIu32 ",REQ_BAD=%" PRIu32 ",FOREIGN=%" PRIu32
        ",DUPLICATE=%" PRIu32 ",CONFLICT=%" PRIu32 ",ACK_OK=%" PRIu32 ",ACK_ERR=%" PRIu32 ",INIT_ERR=%" PRIu32 "\r\n",
        io.req_ok,io.req_bad,io.foreign,io.duplicate,io.conflict,io.ack_ok,io.ack_err,io.init_err);
    if(length>0 && (size_t)length<sizeof(line)) { emit(line,length); }
    length=snprintf(line,sizeof(line),
        "$TS_PAIR,RX_OK=%" PRIu32 ",RX_INVALID=%" PRIu32 ",TX_OK=%" PRIu32 ",TX_ERR=%" PRIu32
        ",UNSUPPORTED=%" PRIu32 ",REPORT_OK=%" PRIu32 ",REPORT_ERR=%" PRIu32 ",EXPIRED=%" PRIu32 ",OVERFLOW=%" PRIu32 "\r\n",
        pair.rx_ok,pair.rx_invalid,pair.tx_ok,pair.tx_err,pair.unsupported,pair.report_ok,pair.report_err,pair.expired,pair.overflow);
    if(length>0 && (size_t)length<sizeof(line)) { emit(line,length); }
    _Static_assert(sizeof("$OBS_RX,ACCEPTED=,BAD=,FOREIGN=,NO_PAIR=,EARLY=,DUPLICATE=,CONFLICT=,QUEUE_FULL=\r\n")+8U*10U<=LOG_CAPACITY,
                   "Observation statistics exceed log buffer");
    length=snprintf(line,sizeof(line),
        "$OBS_RX,ACCEPTED=%" PRIu32 ",BAD=%" PRIu32 ",FOREIGN=%" PRIu32 ",NO_PAIR=%" PRIu32
        ",EARLY=%" PRIu32 ",DUPLICATE=%" PRIu32 ",CONFLICT=%" PRIu32 ",QUEUE_FULL=%" PRIu32 "\r\n",
        observation.accepted,observation.bad,observation.foreign,observation.no_pair,observation.early,
        observation.duplicate,observation.conflict,observation.queue_full);
    if(length>0 && (size_t)length<sizeof(line)) { emit(line,length); }
}

void UDPTimeExchange_Process(uint32_t now_ms)
{
    if(active_callbacks!=0U) { return; }
    if(configured)
    {
        if(service_pcb==NULL && netif_is_up(&gnetif) && netif_is_link_up(&gnetif) &&
           (uint32_t)(now_ms-last_attempt_ms)>=SERVICE_PERIOD_MS)
        {
            last_attempt_ms=now_ms; service_pcb=udp_new();
            if(service_pcb!=NULL)
            {
                if(udp_bind(service_pcb,netif_ip_addr4(&gnetif),UDP_TIME_EXCHANGE_BOARD_PORT)==ERR_OK)
                { udp_bind_netif(service_pcb,&gnetif); udp_recv(service_pcb,Receive,NULL); }
                else { udp_remove(service_pcb); service_pcb=NULL; }
            }
            if(service_pcb==NULL) { ++io.init_err; }
        }
        Expire(now_ms);
        for(unsigned int i=0;service_pcb!=NULL && netif_is_up(&gnetif) &&
            netif_is_link_up(&gnetif) && i<RECORD_COUNT;++i)
        {
            Record *record=&records[i];
            if(record->state!=READY) { continue; }
            uint8_t data[REPORT_BYTES]; memcpy(data,record->key,KEY_BYTES);
            data[5]=6U; data[7]=REPORT_BYTES;
            Put32(data+36,record->rx.seconds); Put32(data+40,record->rx.nanoseconds);
            Put32(data+44,record->tx.seconds); Put32(data+48,record->tx.nanoseconds);
            record->state=DONE;
            if(Send(data,REPORT_BYTES)==ERR_OK) { record->state=REPORTED; ++pair.report_ok; }
            else { ++pair.report_err; }
            break; /* At most one attempt, never an automatic retransmission. */
        }
    }
    LogStats(now_ms);
}
