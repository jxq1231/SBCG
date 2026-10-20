#include "Sync/ptp_tx.h"
#include "BSP/bsp_ptp_tx.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct netif gnetif;
static const uint32_t uid[3]={0x001A002D,0x30365105,0x36363631};
static uint8_t frame[128];
static struct pbuf first,tail;
static unsigned int begins,ends,outputs,logs,cleans;
static BSP_PtpStatus begin_status,end_status,clean_status;
static BSP_PtpTime captured;
static err_t output_result;
static bool short_copy,active;
static char lines[32][160];
BSP_PtpStatus BSP_PTP_TxClean(void) { ++cleans; return clean_status; }
BSP_PtpStatus BSP_PTP_TxBegin(const uint8_t *buffer,uint16_t length)
{
    assert(buffer==first.payload && length==first.len && !active); ++begins;
    active=begin_status==BSP_PTP_OK; return begin_status;
}
BSP_PtpStatus BSP_PTP_TxEnd(BSP_PtpTime *time)
{ assert(active); active=false; ++ends; *time=captured; return end_status; }
u16_t pbuf_copy_partial(const struct pbuf *p,void *output,u16_t length,u16_t offset)
{
    unsigned int copied=0;
    while(p && offset>=p->len) { offset=(u16_t)(offset-p->len);p=p->next; }
    while(p && copied<length)
    {
        unsigned int n=p->len-offset;if(n>length-copied) { n=length-copied; }
        memcpy((uint8_t *)output+copied,(uint8_t *)p->payload+offset,n);
        copied+=n;offset=0;p=p->next;
    }
    return (u16_t)(short_copy && copied ? copied-1:copied);
}
static err_t Original(struct netif *n,struct pbuf *p)
{
    assert(n==&gnetif && p==&first && first.ref==2);
    ++outputs;return output_result;
}
static void Log(const char *text,int length)
{
    assert(logs<32 && length>0 && length<160);
    memcpy(lines[logs],text,(size_t)length);lines[logs++][length]=0;
}
static bool Contains(const char *text)
{
    for(unsigned int i=0;i<logs;++i) { if(strstr(lines[i],text)) { return true; } }
    return false;
}
static void Put16(uint8_t *p,unsigned int value)
{ p[0]=(uint8_t)(value>>8);p[1]=(uint8_t)value; }
static void MakeFrame(unsigned int options,bool chained)
{
    memset(frame,0,sizeof(frame));frame[12]=8;frame[14]=(uint8_t)(0x45+options/4);
    frame[23]=17;frame[20]=0x40;Put16(frame+16,56+options);
    const uint8_t source[4]={192,168,10,1},dest[4]={192,168,10,100};
    memcpy(frame+26,source,4);memcpy(frame+30,dest,4);
    unsigned int udp=34+options;Put16(frame+udp,5006);Put16(frame+udp+2,6006);Put16(frame+udp+4,36);
    const uint8_t ack[28]={'S','B','C','G',1,3,0,28,0,0,0,7,0,0,3,232,
        0,0x1A,0,0x2D,0x30,0x36,0x51,5,0x36,0x36,0x36,0x31};
    memcpy(frame+udp+8,ack,28);
    memset(&first,0,sizeof(first));memset(&tail,0,sizeof(tail));
    first.payload=frame;first.len=first.tot_len=(u16_t)(70+options);first.ref=2;
    if(chained) { first.len=20;first.next=&tail;tail.payload=frame+20;tail.len=tail.tot_len=(u16_t)(first.tot_len-20); }
}
static void Reset(uint32_t now_ms)
{
    memset(&gnetif,0,sizeof(gnetif));IP_ADDR4(&gnetif.ip_addr,192,168,10,1);
    gnetif.linkoutput=Original;begins=ends=outputs=logs=cleans=0;
    begin_status=end_status=clean_status=BSP_PTP_OK;captured=(BSP_PtpTime){2,500};
    output_result=ERR_OK;short_copy=active=false;MakeFrame(0,false);PtpTx_Init(now_ms,uid,Log);
}
static void Deliver(void)
{ assert(gnetif.linkoutput(&gnetif,&first)==output_result && first.ref==2); }

int main(void)
{
    Reset(0);assert(gnetif.linkoutput!=Original && begins==0);
    Deliver();assert(begins==1 && ends==1 && outputs==1 && logs==0 && !active);
    memset(frame,0,sizeof(frame));PtpTx_Process(999);assert(logs==0);
    PtpTx_Process(1000);
    assert(Contains("REQ_SEQ=7,CLIENT_TICK_MS=1000,SEC=2,NSEC=500,SYNC=0,SOURCE=DMA"));
    assert(Contains("VALID=1,ARM_ERR=0,SNAP_ERR=0,UNSUPPORTED=0,OVERFLOW=0,INIT_ERR=0"));
    puts("PASS: TX observation occurs at linkoutput and queues hardware time without retaining pbuf ownership");

    const unsigned int offsets[]={12,14,16,20,21,23,26,30,34,36,38,42,47,58};
    const uint8_t values[]={0x81,0x44,0xFF,0x20,1,6,10,10,0,0,0xFF,'X',2,99};
    for(unsigned int i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i)
    {
        Reset(0);frame[offsets[i]]=values[i];Deliver();PtpTx_Process(1000);
        assert(begins==0 && ends==0 && outputs==1 && Contains("VALID=0,ARM_ERR=0,SNAP_ERR=0,UNSUPPORTED=0"));
    }
    Reset(0);short_copy=true;Deliver();assert(begins==0 && outputs==1);
    Reset(0);first.len=first.tot_len=33;Deliver();assert(begins==0 && outputs==1);
    puts("PASS: only complete unfragmented replies with this board UID and expected endpoints are observed");

    Reset(0);MakeFrame(0,true);output_result=ERR_IF;Deliver();PtpTx_Process(1000);
    assert(begins==0 && ends==0 && outputs==1 && Contains("UNSUPPORTED=1"));
    Reset(0);MakeFrame(4,false);PtpTx_Process(1000);assert(begins==0);
    captured=(BSP_PtpTime){5,777};Deliver();PtpTx_Process(2000);
    assert(Contains("SEC=5,NSEC=777") && begins==1);
    puts("PASS: chained replies are explicitly unsupported and delayed actual output uses its own frame and time");

    Reset(0);begin_status=BSP_PTP_NOT_READY;Deliver();PtpTx_Process(1000);
    assert(ends==0 && outputs==1 && Contains("VALID=0,ARM_ERR=1,SNAP_ERR=0"));
    Reset(0);end_status=BSP_PTP_BAD_SNAPSHOT;Deliver();PtpTx_Process(1000);
    assert(ends==1 && outputs==1 && Contains("VALID=0,ARM_ERR=0,SNAP_ERR=1"));
    Reset(0);output_result=ERR_IF;Deliver();PtpTx_Process(1000);
    assert(ends==1 && !active && Contains("VALID=0,ARM_ERR=0,SNAP_ERR=1"));
    puts("PASS: arm, snapshot and original output failures preserve the send result without fabricated times");

    Reset(0);for(unsigned int i=0;i<5;++i) { frame[53]=(uint8_t)(i+1);Deliver(); }
    for(uint32_t ms=1000;ms<=5000;ms+=1000) { PtpTx_Process(ms); }
    unsigned int records=0;
    for(unsigned int i=0;i<logs;++i) { if(strstr(lines[i],"$PTP_TX,")) { ++records; } }
    assert(records==4 && Contains("REQ_SEQ=4") && !Contains("REQ_SEQ=5"));
    assert(Contains("VALID=5,ARM_ERR=0,SNAP_ERR=0,UNSUPPORTED=0,OVERFLOW=1"));
    puts("PASS: bounded TX queue preserves four keys in order and reports the discarded observation");

    Reset(UINT32_MAX-499);Deliver();PtpTx_Process(499);assert(logs==0);
    PtpTx_Process(500);assert(logs==2);PtpTx_Process(501);assert(logs==2);
    Reset(0);Deliver();Deliver();PtpTx_Process(10000);assert(logs==2);
    PtpTx_Process(10001);assert(logs==2);
    puts("PASS: TX logs survive tick wrap and delayed processing never bursts queued records");

    Reset(0);PtpTx_Init(0,uid,Log);Deliver();assert(outputs==1 && begins==1 && ends==1);
    Reset(0);gnetif.linkoutput=NULL;PtpTx_Init(0,uid,Log);PtpTx_Process(1000);
    assert(Contains("INIT_ERR=1"));
    Reset(0);PtpTx_Init(0,NULL,Log);Deliver();PtpTx_Process(1000);
    assert(begins==0 && Contains("INIT_ERR=1"));
    Reset(0);PtpTx_Init(0,uid,NULL);Deliver();PtpTx_Process(1000);assert(logs==0);
    puts("PASS: repeated installation avoids recursion and absent input identity or logging is safe");

    Reset(0);clean_status=BSP_PTP_BAD_CONFIG;PtpTx_Process(1000);
    assert(Contains("SNAP_ERR=1"));
    Reset(0);clean_status=BSP_PTP_NOT_READY;PtpTx_Process(1000);
    assert(Contains("SNAP_ERR=0"));
    puts("PASS: invalid cleanup mappings are visible while deferred DMA ownership is not counted repeatedly");
    return 0;
}
