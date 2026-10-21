#include "Network/udp_time_exchange.h"
#include "Sync/ptp_rx.h"
#include "Sync/ptp_tx.h"
#include "BSP/bsp_ptp_tx.h"
#include "lwip/udp.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Link the actual old observers. Only sockets, pbuf ownership and MAC IO are
   replaced; every delivery crosses their real installed function pointers. */
struct netif gnetif;
static const uint32_t uid[3] = {0x001A002D,0x30365105,0x36363631};
static struct udp_pcb pcb;
static bool pcb_live, no_pcb, no_memory, short_copy, deferred, chain_ack;
static bool deliver_callback, double_claim, mutate_claim, token_active;
static bool deinit_in_input, deinit_in_output, init_in_output;
static bool reentrant_result;
static err_t bind_error, take_error, send_error, input_result, output_result;
static BSP_PtpStatus rx_status, read_status, begin_status, end_status;
static BSP_PtpTime rx_time, local_time, tx_time;
static uint32_t now_ms;
static unsigned int attempts, removes, allocations, releases, references;
static unsigned int inputs, outputs, rx_reads, local_reads, begins, ends, clean_armed;
static unsigned int ack_sends, report_sends, logs, old_rx_claims;
static uint8_t last_ack[36], last_report[52], expected_begin[128];
static uint16_t expected_begin_length;
static const uint8_t *armed_pointer;
static uint16_t armed_length;
static struct pbuf *queued;
static char lines[64][256];

static void Put16(uint8_t *p,uint32_t n) { p[0]=(uint8_t)(n>>8); p[1]=(uint8_t)n; }
static void Put32(uint8_t *p,uint32_t n)
{ p[0]=(uint8_t)(n>>24); p[1]=(uint8_t)(n>>16); p[2]=(uint8_t)(n>>8); p[3]=(uint8_t)n; }
static uint32_t Get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static uint32_t Tick(void) { return now_ms; }
static void Log(const char *text,int length)
{
    assert(logs<64 && length>0 && length<256);
    memcpy(lines[logs],text,(size_t)length); lines[logs++][length]=0;
}
static bool Contains(const char *text)
{ for(unsigned int i=0;i<logs;++i) { if(strstr(lines[i],text)) { return true; } } return false; }
static void Stats(void) { UDPTimeExchange_Process(now_ms+1000U); }

u16_t pbuf_copy_partial(const struct pbuf *p,void *dest,u16_t length,u16_t offset)
{
    unsigned int copied=0;
    while(p && offset>=p->len) { offset=(u16_t)(offset-p->len); p=p->next; }
    while(p && copied<length)
    {
        unsigned int n=p->len-offset; if(n>length-copied) { n=length-copied; }
        memcpy((uint8_t *)dest+copied,(uint8_t *)p->payload+offset,n);
        copied+=n; offset=0; p=p->next;
    }
    return (u16_t)(short_copy && copied ? copied-1 : copied);
}
struct pbuf *pbuf_alloc(pbuf_layer layer,u16_t length,pbuf_type type)
{
    assert(layer==PBUF_TRANSPORT && type==PBUF_RAM && (length==36 || length==52));
    if(no_memory) { return NULL; }
    struct pbuf *p=calloc(1,sizeof(*p)+length); assert(p);
    p->payload=p+1; p->len=p->tot_len=length; p->ref=1; ++allocations; ++references;
    return p;
}
void pbuf_ref(struct pbuf *p) { assert(p && p->ref); ++p->ref; ++references; }
u8_t pbuf_free(struct pbuf *p)
{
    assert(p && p->ref && references); --references;
    if(--p->ref) { return 0; }
    if(p->next) { pbuf_free(p->next); }
    ++releases; free(p); return 1;
}
err_t pbuf_take(struct pbuf *p,const void *data,u16_t length)
{ assert(p && p->len==length && p->ref==1); memcpy(p->payload,data,length); return take_error; }
struct udp_pcb *udp_new(void)
{ ++attempts; if(no_pcb) { return NULL; } assert(!pcb_live); pcb_live=true; memset(&pcb,0,sizeof(pcb)); return &pcb; }
void udp_remove(struct udp_pcb *p) { assert(p==&pcb && pcb_live); ++removes; pcb_live=false; }
err_t udp_bind(struct udp_pcb *p,const ip_addr_t *address,u16_t port)
{ assert(p==&pcb && address && port==5007); return bind_error; }
void udp_bind_netif(struct udp_pcb *p,const struct netif *n) { assert(p==&pcb && n==&gnetif); }
void udp_recv(struct udp_pcb *p,udp_recv_fn cb,void *arg) { p->recv=cb; p->recv_arg=arg; }
BSP_PtpStatus BSP_PTP_ReadRx(BSP_PtpTime *time) { ++rx_reads; *time=rx_time; return rx_status; }
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *time) { ++local_reads; *time=local_time; return read_status; }
BSP_PtpStatus BSP_PTP_TxClean(void)
{ if(token_active) { ++clean_armed; return BSP_PTP_NOT_READY; } return BSP_PTP_OK; }
BSP_PtpStatus BSP_PTP_TxBegin(const uint8_t *buffer,uint16_t length)
{
    assert(!token_active); ++begins;
    if(expected_begin_length) { assert(length==expected_begin_length && memcmp(buffer,expected_begin,length)==0); }
    if(begin_status==BSP_PTP_OK) { token_active=true; armed_pointer=buffer; armed_length=length; }
    return begin_status;
}
BSP_PtpStatus BSP_PTP_TxEnd(BSP_PtpTime *time)
{ assert(token_active); token_active=false; ++ends; *time=tx_time; return end_status; }

static uint16_t MakeFrame(uint8_t frame[128],const uint8_t *payload,uint16_t length,
                          bool outgoing,unsigned int options,bool old)
{
    memset(frame,0,128); frame[12]=8; frame[14]=(uint8_t)(0x45+options/4);
    frame[23]=17; frame[20]=0x40; Put16(frame+16,20U+options+8U+length);
    const uint8_t board[4]={192,168,10,1},peer[4]={192,168,10,100};
    memcpy(frame+26,outgoing ? board : peer,4); memcpy(frame+30,outgoing ? peer : board,4);
    uint8_t *udp=frame+34+options;
    Put16(udp,outgoing ? (old?5006:5007) : (old?6006:6007));
    Put16(udp+2,outgoing ? (old?6006:6007) : (old?5006:5007));
    Put16(udp+4,length+8U); memcpy(udp+8,payload,length);
    return (uint16_t)(42U+options+length);
}
static err_t OriginalOutput(struct netif *n,struct pbuf *p)
{
    assert(n==&gnetif && p && p->ref==1); ++outputs;
    if(token_active) { assert(p->payload==armed_pointer && p->len==armed_length); }
    if(deinit_in_output) { reentrant_result=UDPTimeExchange_Deinit(); }
    if(init_in_output) { reentrant_result=UDPTimeExchange_Init(now_ms,uid,Tick,Log); }
    return output_result;
}
static err_t SendFrame(struct pbuf *payload)
{
    uint8_t frame[128];
    uint16_t length=MakeFrame(frame,payload->payload,payload->tot_len,true,0,false);
    struct pbuf p={0},tail={0}; p.payload=frame; p.len=p.tot_len=length; p.ref=1;
    if(chain_ack && ((uint8_t *)payload->payload)[5]==5)
    { p.len=30; p.next=&tail; tail.payload=frame+30; tail.len=tail.tot_len=length-30; tail.ref=1; }
    return gnetif.linkoutput(&gnetif,&p);
}
err_t udp_sendto(struct udp_pcb *p,struct pbuf *buf,const ip_addr_t *address,u16_t port)
{
    assert(p==&pcb && pcb_live && buf && buf->ref==1 && address && port==6007);
    const uint8_t *data=buf->payload;
    if(data[5]==5) { assert(buf->tot_len==36); memcpy(last_ack,data,36); ++ack_sends; }
    else { assert(data[5]==6 && buf->tot_len==52); memcpy(last_report,data,52); ++report_sends; }
    if(send_error!=ERR_OK) { return send_error; }
    if(deferred && data[5]==5) { assert(!queued); pbuf_ref(buf); queued=buf; return ERR_OK; }
    return SendFrame(buf);
}
static void Flush(void) { assert(queued); struct pbuf *p=queued; queued=NULL; SendFrame(p); pbuf_free(p); }
static void OutputFrame(uint8_t frame[128],uint16_t length)
{
    struct pbuf p={0}; p.payload=frame; p.len=p.tot_len=length; p.ref=1;
    assert(gnetif.linkoutput(&gnetif,&p)==output_result && p.ref==1);
}

static struct pbuf *Packet(const uint8_t *data,uint16_t length,bool chain)
{
    struct pbuf *p=calloc(1,sizeof(*p)+length); assert(p); p->payload=p+1;
    memcpy(p->payload,data,length); p->len=p->tot_len=length; p->ref=1; ++allocations; ++references;
    if(chain)
    {
        struct pbuf *tail=calloc(1,sizeof(*tail)+length-10); assert(tail); tail->payload=tail+1;
        memcpy(tail->payload,data+10,length-10); tail->len=tail->tot_len=length-10; tail->ref=1;
        p->len=10; p->next=tail; ++allocations; ++references;
    }
    return p;
}
static void Callback(const uint8_t *data,uint16_t length,u16_t port,bool foreign,bool chain)
{
    ip_addr_t source; IP_ADDR4(&source,192,168,10,foreign ? 101:100);
    assert(pcb.recv); pcb.recv(pcb.recv_arg,&pcb,Packet(data,length,chain),&source,port);
}
static err_t OriginalInput(struct pbuf *p,struct netif *n)
{
    assert(n==&gnetif); ++inputs;
    if(deinit_in_input) { reentrant_result=UDPTimeExchange_Deinit(); }
    uint8_t frame[128]; assert(pbuf_copy_partial(p,frame,p->tot_len,0)==p->tot_len || short_copy);
    const unsigned int offset=14U+(frame[14]&15U)*4U+8U;
    if(deliver_callback && offset+36<=p->tot_len && frame[offset+5]==4)
    {
        uint8_t data[36]; memcpy(data,frame+offset,36); if(mutate_claim) { data[15]^=1; }
        Callback(data,36,6007,false,false);
        if(double_claim) { ++data[11]; Callback(data,36,6007,false,false); }
    }
    if(offset+28<=p->tot_len && frame[offset+5]==2) { PtpRx_Observe(frame+offset); ++old_rx_claims; }
    return input_result;
}
static void Request(uint8_t data[36],uint32_t sequence,uint32_t tick,uint32_t session)
{
    memset(data,0,36); memcpy(data,"SBCG\1\4\0\44",8); Put32(data+8,sequence); Put32(data+12,tick);
    for(unsigned int i=0;i<3;++i) { Put32(data+16+i*4,uid[i]); }
    Put32(data+28,0xABCDEF01); Put32(data+32,session);
}
static void Deliver(const uint8_t data[36],unsigned int options,bool chain)
{
    uint8_t frame[128]; uint16_t length=MakeFrame(frame,data,36,false,options,false);
    struct pbuf *p=Packet(frame,length,chain);
    assert(gnetif.input(p,&gnetif)==input_result); pbuf_free(p);
}
static void Reset(uint32_t tick)
{
    assert(UDPTimeExchange_Deinit()); assert(!queued && references==0 && !token_active);
    memset(&gnetif,0,sizeof(gnetif)); IP_ADDR4(&gnetif.ip_addr,192,168,10,1);
    gnetif.flags=NETIF_FLAG_UP|NETIF_FLAG_LINK_UP; gnetif.input=OriginalInput; gnetif.linkoutput=OriginalOutput;
    no_pcb=no_memory=short_copy=deferred=chain_ack=false; deliver_callback=true;
    double_claim=mutate_claim=deinit_in_input=deinit_in_output=init_in_output=false;
    bind_error=take_error=send_error=input_result=output_result=ERR_OK;
    rx_status=read_status=begin_status=end_status=BSP_PTP_OK;
    rx_time=(BSP_PtpTime){2,100}; local_time=(BSP_PtpTime){3,0}; tx_time=(BSP_PtpTime){2,500}; now_ms=tick;
    attempts=removes=allocations=releases=references=inputs=outputs=rx_reads=local_reads=0;
    begins=ends=clean_armed=ack_sends=report_sends=logs=old_rx_claims=0; expected_begin_length=0;
    PtpRx_Init(tick,NULL); PtpTx_Init(tick,uid,NULL);
    assert(UDPTimeExchange_Init(tick,uid,Tick,Log)); UDPTimeExchange_Process(tick); assert(pcb.recv);
}
static void Normal(void)
{
    uint8_t request[36]; Request(request,7,1000,0x12345678); Deliver(request,0,false);
    assert(ack_sends==1 && report_sends==0 && begins==1 && ends==1 && clean_armed==1 && !token_active);
    assert(memcmp(last_ack,"SBCG\1\5\0\44",8)==0 && memcmp(last_ack+8,request+8,28)==0);
    assert(references==0 && allocations==releases && logs==0);
    /* Overwrite the source: records must contain CPU copies, never pbuf pointers. */
    memset(request,0,sizeof(request)); UDPTimeExchange_Process(now_ms);
    assert(report_sends==1 && begins==1 && memcmp(last_report,"SBCG\1\6\0\64",8)==0);
    assert(Get32(last_report+8)==7 && Get32(last_report+12)==1000 && Get32(last_report+32)==0x12345678);
    assert(Get32(last_report+36)==2 && Get32(last_report+40)==100 && Get32(last_report+44)==2 && Get32(last_report+48)==500);
    Stats(); assert(logs==2 && Contains("REQ_OK=1,REQ_BAD=0,FOREIGN=0,DUPLICATE=0,CONFLICT=0,ACK_OK=1,ACK_ERR=0,INIT_ERR=0"));
    assert(Contains("RX_OK=1,RX_INVALID=0,TX_OK=1,TX_ERR=0,UNSUPPORTED=0,REPORT_OK=1,REPORT_ERR=0,EXPIRED=0,OVERFLOW=0"));
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    Reset(0); Normal(); puts("PASS 01: exact 36/52-byte wire, hardware pair, single token through real old wrappers, copied ownership");

    uint8_t request[36];
    Reset(0); Request(request,1,0,1); Deliver(request,12,true); UDPTimeExchange_Process(0);
    assert(report_sends==1 && begins==1 && rx_reads==1 && references==0);
    puts("PASS 02: IPv4 options and chained RX retain exact full key including zero client tick");

    const unsigned int bad_offsets[]={0,4,5,6,7,16,28,8};
    for(unsigned int i=0;i<sizeof(bad_offsets)/sizeof(bad_offsets[0]);++i)
    {
        Reset(0); Request(request,1,0,1);
        if(i==6) { memset(request+28,0,8); } else if(i==7) { memset(request+8,0,4); } else { request[bad_offsets[i]]^=1; }
        Callback(request,36,6007,false,false); Stats(); assert(ack_sends==0 && Contains("REQ_BAD=1") && references==0);
    }
    Reset(0); Request(request,1,0,1); Callback(request,35,6007,false,false);
    short_copy=true; Callback(request,36,6007,false,true); short_copy=false;
    Callback(request,36,6006,false,false); Callback(request,36,6007,true,false); Stats();
    assert(ack_sends==0 && Contains("REQ_BAD=2,FOREIGN=2") && references==0);
    puts("PASS 03: strict envelope, UID, nonzero sequence/session, source, length and short-copy rejection");

    Reset(0); Request(request,1,0,1); Deliver(request,0,false); Deliver(request,0,false);
    request[15]=1; Deliver(request,0,false); UDPTimeExchange_Process(0); request[15]=0; Deliver(request,0,false); Stats();
    assert(ack_sends==1 && report_sends==1 && Contains("DUPLICATE=2,CONFLICT=1"));
    puts("PASS 04: duplicates/conflicts ignore ACK and preserve original time with DONE tombstone");

    Reset(0); for(uint32_t i=1;i<=5;++i) { Request(request,i,0,1); Deliver(request,0,false); }
    assert(ack_sends==5 && begins==4); UDPTimeExchange_Process(0); assert(report_sends==1);
    UDPTimeExchange_Process(0); assert(report_sends==2); UDPTimeExchange_Process(0); UDPTimeExchange_Process(0);
    assert(report_sends==4); Stats(); assert(Contains("OVERFLOW=1"));
    puts("PASS 05: four fixed records, overflow ACK without report, one REPORT per Process");

    Reset(UINT32_MAX-1000U); deferred=true; Request(request,1,0,1); Deliver(request,0,false);
    now_ms=998; UDPTimeExchange_Process(now_ms); assert(report_sends==0); now_ms=999; UDPTimeExchange_Process(now_ms);
    Flush(); assert(begins==0); Stats(); assert(Contains("EXPIRED=1"));
    deferred=false; Deliver(request,0,false); UDPTimeExchange_Process(now_ms); assert(report_sends==1);
    puts("PASS 06: 2000ms unsigned TTL survives tick wrap and rejects delayed expired ACK");

    Reset(0); Request(request,1,0,1); Deliver(request,0,false); UDPTimeExchange_Process(0);
    now_ms=2000; UDPTimeExchange_Process(now_ms); assert(Contains("EXPIRED=0")); Deliver(request,0,false);
    assert(ack_sends==2); UDPTimeExchange_Process(now_ms); assert(report_sends==2);
    puts("PASS 07: completed tombstones expire quietly at original TTL and allow a fresh request");

    for(unsigned int scenario=0;scenario<6;++scenario)
    {
        Reset(0); Request(request,1,0,1);
        if(scenario==0) { rx_status=BSP_PTP_NOT_READY; }
        if(scenario==1) { rx_time.nanoseconds=1000000000U; }
        if(scenario==2) { read_status=BSP_PTP_HAL_ERROR; }
        if(scenario==3) { rx_time.seconds=4; }
        if(scenario==4) { local_time.nanoseconds=1000000000U; }
        if(scenario==5) { rx_time.seconds=3; rx_time.nanoseconds=1; }
        Deliver(request,0,false); UDPTimeExchange_Process(0); Stats();
        assert(ack_sends==1 && report_sends==0 && begins==0 && Contains("RX_INVALID=1"));
    }
    puts("PASS 08: bad/missing/future RX or invalid later local reading never fabricates a hardware pair");

    Reset(0); Request(request,1,0,1); Callback(request,36,6007,false,false); UDPTimeExchange_Process(0); Stats();
    assert(ack_sends==1 && report_sends==0 && Contains("RX_INVALID=1"));
    Reset(0); Request(request,1,0,1); double_claim=true; Deliver(request,0,false); UDPTimeExchange_Process(0); Stats();
    assert(ack_sends==2 && begins==1 && report_sends==1 && Contains("RX_OK=1,RX_INVALID=1"));
    Reset(0); Request(request,1,0,1); deliver_callback=false; Deliver(request,0,false);
    Callback(request,36,6007,false,false); Stats(); assert(begins==0 && Contains("RX_INVALID=1"));
    Reset(0); Request(request,1,0,1); mutate_claim=true; Deliver(request,0,false); Stats(); assert(begins==0 && Contains("RX_INVALID=1"));
    puts("PASS 09: context must match full key, can be claimed once, and disappears after original input");

    const unsigned int frame_offsets[]={12,14,16,20,21,23,26,30,34,36,38};
    for(unsigned int i=0;i<sizeof(frame_offsets)/sizeof(frame_offsets[0]);++i)
    {
        Reset(0); Request(request,1,0,1); uint8_t frame[128]; uint16_t length=MakeFrame(frame,request,36,false,0,false);
        frame[frame_offsets[i]]^=1; struct pbuf *p=Packet(frame,length,false);
        assert(gnetif.input(p,&gnetif)==ERR_OK); pbuf_free(p); Stats(); assert(rx_reads==0 && references==0);
    }
    puts("PASS 10: VLAN, fragments, IP/UDP lengths, protocol and wrong endpoint cannot inherit timestamps");

    Reset(0); deferred=true; Request(request,9,55,42); Deliver(request,0,false); assert(begins==0 && queued && references==1);
    expected_begin_length=MakeFrame(expected_begin,last_ack,36,true,0,false); tx_time=(BSP_PtpTime){7,999};
    Flush(); assert(begins==1 && ends==1 && references==0); UDPTimeExchange_Process(0);
    assert(Get32(last_report+44)==7 && Get32(last_report+48)==999);
    puts("PASS 11: deferred ARP observes actual output frame/pointer/length and later DMA TX time");

    Reset(0); chain_ack=true; Request(request,1,0,1); Deliver(request,0,false); Stats();
    assert(ack_sends==1 && begins==0 && report_sends==0 && Contains("UNSUPPORTED=1"));
    puts("PASS 12: chained ACK sends normally but cannot produce a hardware REPORT");

    for(unsigned int scenario=0;scenario<5;++scenario)
    {
        Reset(0); Request(request,1,0,1);
        if(scenario==0) { begin_status=BSP_PTP_NOT_READY; }
        if(scenario==1) { end_status=BSP_PTP_BAD_SNAPSHOT; }
        if(scenario==2) { tx_time.nanoseconds=1000000000U; }
        if(scenario==3) { tx_time.nanoseconds=99; }
        if(scenario==4) { output_result=ERR_IF; }
        Deliver(request,0,false); UDPTimeExchange_Process(0); Stats();
        assert(report_sends==0 && !token_active && Contains("TX_ERR=1"));
    }
    puts("PASS 13: Begin, End, invalid/backward TX and underlying output failures terminate without retry");

    for(unsigned int scenario=0;scenario<3;++scenario)
    {
        Reset(0); Request(request,1,0,1);
        if(scenario==0) { no_memory=true; } if(scenario==1) { take_error=ERR_MEM; } if(scenario==2) { send_error=ERR_IF; }
        Deliver(request,0,false); no_memory=false; take_error=send_error=ERR_OK; UDPTimeExchange_Process(0); Stats();
        assert(references==0 && report_sends==0 && Contains("ACK_ERR=1"));
        Request(request,2,0,1); Deliver(request,0,false); UDPTimeExchange_Process(0); assert(report_sends==1);
    }
    puts("PASS 14: ACK allocation/take/send failures free every owned reference and next request recovers");

    for(unsigned int scenario=0;scenario<3;++scenario)
    {
        Reset(0); Request(request,1,0,1); Deliver(request,0,false);
        if(scenario==0) { no_memory=true; } if(scenario==1) { take_error=ERR_MEM; } if(scenario==2) { send_error=ERR_IF; }
        UDPTimeExchange_Process(0); no_memory=false; take_error=send_error=ERR_OK;
        UDPTimeExchange_Process(0); Stats(); assert(references==0 && Contains("REPORT_ERR=1,EXPIRED=0"));
        assert(report_sends==(scenario==2 ? 1U:0U));
    }
    puts("PASS 15: REPORT allocation/take/send failures free ownership, retain tombstone and do not retry");

    Reset(0); assert(UDPTimeExchange_Deinit()); no_pcb=true;
    assert(UDPTimeExchange_Init(0,uid,Tick,Log)); UDPTimeExchange_Process(0); UDPTimeExchange_Process(999); assert(attempts==2);
    no_pcb=false; bind_error=ERR_USE; UDPTimeExchange_Process(1000); assert(attempts==3 && removes==2);
    bind_error=ERR_OK; UDPTimeExchange_Process(2000); assert(attempts==4 && pcb.recv);
    assert(UDPTimeExchange_Deinit()); gnetif.flags=NETIF_FLAG_UP;
    assert(UDPTimeExchange_Init(2000,uid,Tick,Log)); UDPTimeExchange_Process(2000); assert(attempts==4);
    gnetif.flags|=NETIF_FLAG_LINK_UP; UDPTimeExchange_Process(2001); assert(attempts==5);
    puts("PASS 16: PCB creation/bind retry at 1Hz, interface/link gating and exact removal");

    Reset(0); netif_input_fn input=gnetif.input; netif_linkoutput_fn output=gnetif.linkoutput;
    assert(UDPTimeExchange_Init(0,uid,Tick,Log)); assert(gnetif.input==input && gnetif.linkoutput==output);
    UDPTimeExchange_Process(0);
    Request(request,1,0,1); Deliver(request,0,false); assert(inputs==1 && outputs==1 && begins==1);
    assert(UDPTimeExchange_Deinit()); assert(gnetif.input!=input && gnetif.linkoutput!=output);
    PtpRx_Init(0,NULL); PtpTx_Init(0,uid,NULL); assert(UDPTimeExchange_Init(0,uid,Tick,Log)); UDPTimeExchange_Process(0);
    Request(request,2,0,1); Deliver(request,0,false); assert(inputs==2 && outputs==2 && begins==2);
    puts("PASS 17: repeated service init and full App-style old/new reinitialization remain acyclic");

    Reset(0); input=gnetif.input; output=gnetif.linkoutput; gnetif.input=OriginalInput;
    assert(!UDPTimeExchange_Deinit() && !UDPTimeExchange_Init(0,uid,Tick,Log)); assert(gnetif.input==OriginalInput && gnetif.linkoutput==output);
    gnetif.input=input; assert(UDPTimeExchange_Deinit());
    Reset(0); input=gnetif.input; output=gnetif.linkoutput; gnetif.linkoutput=OriginalOutput;
    assert(!UDPTimeExchange_Deinit()); assert(gnetif.input==input && gnetif.linkoutput==OriginalOutput);
    gnetif.linkoutput=output; assert(UDPTimeExchange_Deinit());
    puts("PASS 18: lost input OR output ownership fails closed without blind restore or reinstall");

    Reset(0); deinit_in_input=true; reentrant_result=true; Request(request,1,0,1); Deliver(request,0,false);
    assert(!reentrant_result); deinit_in_input=false; deinit_in_output=true; Request(request,2,0,1); Deliver(request,0,false); assert(!reentrant_result);
    deinit_in_output=false; init_in_output=true; Request(request,3,0,1); Deliver(request,0,false); assert(!reentrant_result && !token_active);
    puts("PASS 19: callback-active Deinit and Init fail closed while preserving active chain/token");

    Reset(0); assert(UDPTimeExchange_Deinit()); input=gnetif.input; output=gnetif.linkoutput;
    assert(!UDPTimeExchange_Init(0,NULL,Tick,Log)); assert(!UDPTimeExchange_Init(0,uid,NULL,Log));
    const uint32_t zero_uid[3]={0,0,0}; assert(!UDPTimeExchange_Init(0,zero_uid,Tick,Log));
    gnetif.input=NULL; assert(!UDPTimeExchange_Init(0,uid,Tick,Log)); gnetif.input=input;
    gnetif.linkoutput=NULL; assert(!UDPTimeExchange_Init(0,uid,Tick,Log)); gnetif.linkoutput=output;
    assert(UDPTimeExchange_Init(0,uid,Tick,NULL)); UDPTimeExchange_Process(0); Request(request,1,0,1); Deliver(request,0,false); Stats(); assert(logs==0);
    puts("PASS 20: absent/all-zero identity, clock function or original entry rejected; null logger is safe");

    Reset(0); uint8_t old[36]; Request(old,7,1000,1); old[5]=2; old[7]=28;
    uint8_t frame[128]; uint16_t length=MakeFrame(frame,old,28,false,0,true); struct pbuf *p=Packet(frame,length,false);
    assert(gnetif.input(p,&gnetif)==ERR_OK); pbuf_free(p); assert(old_rx_claims==1 && rx_reads==1);
    old[5]=3; length=MakeFrame(frame,old,28,true,0,true); struct pbuf old_frame={0}; old_frame.payload=frame; old_frame.len=old_frame.tot_len=length; old_frame.ref=1;
    assert(gnetif.linkoutput(&gnetif,&old_frame)==ERR_OK); assert(begins==1 && ends==1 && clean_armed==0);
    Request(request,8,1000,1); Deliver(request,0,false); assert(begins==2 && ends==2 && clean_armed==1 && rx_reads==2);
    puts("PASS 21: actual legacy RX/type2 and TX/type3 still work and selectors are mutually exclusive");

    Reset(UINT32_MAX-499U); Request(request,1,0,1); input_result=ERR_IF; output_result=ERR_IF; Deliver(request,0,false);
    UDPTimeExchange_Process(499); assert(logs==0); UDPTimeExchange_Process(500); assert(logs==2);
    UDPTimeExchange_Process(501); assert(logs==2 && inputs==1 && outputs==1 && references==0);
    assert(UDPTimeExchange_Deinit()); assert(allocations==releases);
    puts("PASS 22: original input/output errors are preserved, log rate survives wrap, no buffer/reference leak");

    Reset(0); Request(request,1,7,1); Deliver(request,0,false);
    Request(request,1,7,2); Deliver(request,0,false);
    Request(request,2,7,1); Deliver(request,0,false);
    Request(request,1,8,1); Deliver(request,0,false);
    assert(ack_sends==3 && begins==3); UDPTimeExchange_Process(0); UDPTimeExchange_Process(0); UDPTimeExchange_Process(0);
    Stats(); assert(report_sends==3 && Contains("DUPLICATE=0,CONFLICT=1"));
    puts("PASS 23: session and sequence independently distinguish identities while changed tick conflicts");

    Reset(0); deferred=true; Request(request,1,7,1); Deliver(request,0,false);
    const unsigned int output_offsets[]={12,14,16,20,21,23,26,30,34,36,38,42,46,47,49,50,54,58,70,74};
    for(unsigned int i=0;i<sizeof(output_offsets)/sizeof(output_offsets[0]);++i)
    {
        length=MakeFrame(frame,last_ack,36,true,0,false); frame[output_offsets[i]]^=1;
        OutputFrame(frame,length); assert(begins==0);
    }
    /* A real ACK with IPv4 options qualifies using its actual complete length. */
    expected_begin_length=MakeFrame(expected_begin,last_ack,36,true,8,false);
    memcpy(frame,expected_begin,sizeof(frame)); OutputFrame(frame,expected_begin_length);
    assert(begins==1 && ends==1); Flush(); assert(begins==1 && references==0);
    UDPTimeExchange_Process(0); assert(report_sends==1);
    puts("PASS 24: pending ACK requires full exact key/endpoints/envelope; actual IPv4-option frame length is armed once");

    Reset(0); Request(request,1,0,1); deliver_callback=false; Deliver(request,0,false);
    short_copy=true; Deliver(request,0,false); short_copy=false;
    Callback(request,36,6007,false,false); Stats(); assert(rx_reads==1 && begins==0 && Contains("RX_INVALID=1"));
    puts("PASS 25: a short/failed next selector clears previously captured context without retaining a pbuf");
    assert(UDPTimeExchange_Deinit()); assert(references==0 && allocations==releases && !token_active);
    Reset(0); Request(request,1,0,1); Deliver(request,0,false);
    gnetif.flags=NETIF_FLAG_UP; UDPTimeExchange_Process(0); assert(report_sends==0);
    gnetif.flags|=NETIF_FLAG_LINK_UP; UDPTimeExchange_Process(1); assert(report_sends==1);
    Request(request,2,0,1); Deliver(request,0,false); gnetif.flags=NETIF_FLAG_LINK_UP;
    UDPTimeExchange_Process(1); assert(report_sends==1);
    now_ms=2000; UDPTimeExchange_Process(now_ms); assert(report_sends==1);
    gnetif.flags|=NETIF_FLAG_UP; UDPTimeExchange_Process(2001); assert(report_sends==1 && Contains("EXPIRED=1"));
    puts("PASS 26: READY reports wait for link/interface recovery and expire normally without false send attempt");
    assert(UDPTimeExchange_Deinit()); assert(references==0 && allocations==releases && !token_active);
    puts("26 native time-exchange integration scenarios passed"); return 0;
}
