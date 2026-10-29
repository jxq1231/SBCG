#include "Sync/ptp_rx.h"
#include "BSP/bsp_ptp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct netif gnetif;
static unsigned char frame[128], request[28];
static struct pbuf first, tail;
static BSP_PtpTime captured, current;
static BSP_PtpStatus rx_status, now_status;
static unsigned int input_calls, rx_reads, logs;
static bool notify, twice, wrong_key, short_copy, alter_cache;
static char lines[32][160];

BSP_PtpStatus BSP_PTP_ReadRx(BSP_PtpTime *time)
{ ++rx_reads; *time=captured; return rx_status; }
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *time)
{ *time=current; return now_status; }
u16_t pbuf_copy_partial(const struct pbuf *p, void *output, u16_t length, u16_t offset)
{
    unsigned int copied=0;
    while (p && offset>=p->len) { offset=(u16_t)(offset-p->len); p=p->next; }
    while (p && copied<length)
    {
        unsigned int n=p->len-offset;
        if (n>length-copied) { n=length-copied; }
        memcpy((unsigned char *)output+copied,(unsigned char *)p->payload+offset,n);
        copied+=n; offset=0; p=p->next;
    }
    return (u16_t)(short_copy && copied ? copied-1 : copied);
}
static void Log(const char *text, int length)
{
    assert(logs<32 && length>0 && length<160);
    memcpy(lines[logs],text,(size_t)length); lines[logs++][length]=0;
}
static bool Contains(const char *text)
{
    for (unsigned int i=0;i<logs;++i) { if (strstr(lines[i],text)) { return true; } }
    return false;
}
static err_t OriginalInput(struct pbuf *p, struct netif *n)
{
    assert(p==&first && n==&gnetif); ++input_calls;
    if (alter_cache) { captured=(BSP_PtpTime){9,999}; }
    if (notify)
    {
        if (wrong_key) { request[8]^=1; }
        PtpRx_Observe(request);
        if (wrong_key) { request[8]^=1; }
        if (twice) { PtpRx_Observe(request); }
    }
    return ERR_IF; /* Wrapper must preserve caller ownership and this error. */
}
static void Put16(unsigned char *p, unsigned int n)
{ p[0]=(unsigned char)(n>>8); p[1]=(unsigned char)n; }
static void MakeFrame(unsigned int options, bool chained)
{
    memset(frame,0,sizeof(frame));
    const unsigned char data[28]={'S','B','C','G',1,2,0,28,0,0,0,7,0,0,3,232,
        0,0x1A,0,0x2D,0x30,0x36,0x51,5,0x36,0x36,0x36,0x31};
    memcpy(request,data,28);
    frame[12]=8; frame[14]=(unsigned char)(0x45+options/4); frame[23]=17;
    Put16(frame+16,56+options); frame[20]=0x40;
    const unsigned char source[4]={192,168,10,100}, dest[4]={192,168,10,1};
    memcpy(frame+26,source,4); memcpy(frame+30,dest,4);
    unsigned int udp=34+options;
    Put16(frame+udp,6006); Put16(frame+udp+2,5006); Put16(frame+udp+4,36);
    memcpy(frame+udp+8,request,28);
    memset(&first,0,sizeof(first)); memset(&tail,0,sizeof(tail));
    first.payload=frame; first.tot_len=(u16_t)(70+options); first.len=first.tot_len;
    if (chained)
    {
        first.len=20; first.next=&tail; tail.payload=frame+20;
        tail.len=tail.tot_len=(u16_t)(first.tot_len-20);
    }
}
static void Reset(uint32_t now_ms)
{
    memset(&gnetif,0,sizeof(gnetif));
    IP_ADDR4(&gnetif.ip_addr,192,168,10,1);
    gnetif.input=OriginalInput;
    captured=(BSP_PtpTime){2,500}; current=(BSP_PtpTime){2,1000};
    rx_status=now_status=BSP_PTP_OK;
    input_calls=rx_reads=logs=0;
    notify=true; twice=wrong_key=short_copy=alter_cache=false;
    MakeFrame(0,false); PtpRx_Init(now_ms,Log);
}
static void Deliver(void)
{ assert(gnetif.input(&first,&gnetif)==ERR_IF); }

int main(void)
{
    Reset(0); assert(gnetif.input!=OriginalInput);
    alter_cache=true; Deliver(); assert(input_calls==1 && rx_reads==1 && logs==0);
    PtpRx_Process(999); assert(logs==0);
    memset(frame,0,sizeof(frame)); PtpRx_Process(1000);
    assert(Contains("REQ_SEQ=7,CLIENT_TICK_MS=1000,SEC=2,NSEC=500,SYNC=0,SOURCE=DMA"));
    assert(Contains("VALID=1,MISSING=0,INVALID=0,OVERFLOW=0,INIT_ERR=0"));
    puts("PASS: input wrapper snapshots DMA time before synchronous delivery and owns no pbuf");

    Reset(0); MakeFrame(4,true); Deliver(); PtpRx_Process(1000);
    assert(Contains("REQ_SEQ=7") && Contains("VALID=1,MISSING=0"));
    puts("PASS: chained pbuf and IPv4 options locate the complete request key");

    Reset(0); twice=true; Deliver(); PtpRx_Observe(request); PtpRx_Process(1000);
    assert(Contains("VALID=1,MISSING=2"));
    Reset(0); wrong_key=true; Deliver(); PtpRx_Process(1000);
    assert(Contains("VALID=0,MISSING=1"));
    Reset(0); notify=false; Deliver(); MakeFrame(0,false); frame[23]=1;
    notify=true; Deliver(); PtpRx_Process(1000);
    assert(Contains("VALID=0,MISSING=1"));
    puts("PASS: complete-key mismatch, repeat claims and old-frame contexts never reuse a timestamp");

    const unsigned int offsets[]={12,14,16,20,21,23,26,30,34,36,38,42};
    const unsigned char values[]={0x81,0x44,0xFF,0x20,1,6,10,10,0,0,0xFF,'X'};
    for (unsigned int i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i)
    {
        Reset(0); frame[offsets[i]]=values[i]; Deliver(); PtpRx_Process(1000);
        assert(Contains("VALID=0,MISSING=1") && rx_reads==0 && input_calls==1);
    }
    Reset(0); first.tot_len=33; first.len=33; Deliver(); PtpRx_Process(1000);
    assert(Contains("VALID=0,MISSING=1"));
    Reset(0); short_copy=true; Deliver(); PtpRx_Process(1000);
    assert(Contains("VALID=0,MISSING=1"));
    puts("PASS: fragments, VLAN, foreign endpoints and malformed/short headers pass through without false metadata");

    Reset(0); rx_status=BSP_PTP_BAD_SNAPSHOT; Deliver(); PtpRx_Process(1000);
    assert(Contains("VALID=0,MISSING=0,INVALID=1"));
    Reset(0); current.nanoseconds=499; Deliver(); PtpRx_Process(1000);
    assert(Contains("INVALID=1"));
    Reset(0); now_status=BSP_PTP_HAL_ERROR; Deliver(); PtpRx_Process(1000);
    assert(Contains("INVALID=1"));
    puts("PASS: absent/corrupt/future hardware times are explicit failures without a software substitute");

    Reset(0); for (unsigned int i=0;i<5;++i) { Deliver(); }
    for (uint32_t ms=1000;ms<=5000;ms+=1000) { PtpRx_Process(ms); }
    unsigned int records=0;
    for (unsigned int i=0;i<logs;++i) { if (strstr(lines[i],"$PTP_RX,")) { ++records; } }
    assert(records==4 && Contains("VALID=5,MISSING=0,INVALID=0,OVERFLOW=1"));
    puts("PASS: fixed queue keeps four records, reports overflow and drains without holding packets");

    Reset(UINT32_MAX-499); Deliver(); PtpRx_Process(499); assert(logs==0);
    PtpRx_Process(500); assert(logs==2); PtpRx_Process(501); assert(logs==2);
    Reset(0); Deliver(); Deliver(); PtpRx_Process(10000); assert(logs==2);
    PtpRx_Process(10001); assert(logs==2);
    puts("PASS: observation rate limit survives tick wrap and delayed loops do not burst");

    Reset(0); PtpRx_Init(0,Log); Deliver(); assert(input_calls==1);
    PtpRx_Process(1000); assert(Contains("VALID=1,MISSING=0"));
    Reset(0); gnetif.input=NULL; PtpRx_Init(0,Log); PtpRx_Process(1000);
    assert(Contains("INIT_ERR=1"));
    Reset(0); PtpRx_Init(0,NULL); Deliver(); PtpRx_Process(1000); assert(logs==0);
    puts("PASS: repeated installation avoids recursion and missing input/disabled logging are safe");
    return 0;
}
