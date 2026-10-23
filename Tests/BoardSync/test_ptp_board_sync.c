/**
 * @file test_ptp_board_sync.c
 * @brief 用原生主机替代LwIP IO和MAC硬件，验证真实PTP传输与旧观察器包装链。
 * @details 覆盖A/B四消息、所有权、硬件戳失败、延后输出、非法端点和重新获取。
 * @note 此处pbuf/PCB与硬件接口均为测试替身，不代表实物验证。
 */
#include "Network/ptp_board_sync.h"
#include "Network/ptp_wire.h"
#include "Sync/ptp_rx.h"
#include "Sync/ptp_tx.h"
#include "BSP/bsp_ptp_tx.h"
#include "lwip/udp.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct netif gnetif;
static struct udp_pcb pcbs[2];static bool live[2];
static BoardIdentityProfile profile;
static uint32_t now,exchange_count;static unsigned int refs,begins,ends,sends,inputs,outputs;
static BSP_PtpTime rx_stamp,tx_stamp;
static BSP_PtpStatus rx_result,begin_result,end_result;
static bool token,defer,chain,fail_send,fail_alloc,call_deinit;
static bool deinit_result;
static uint8_t sent[32][54];static uint16_t sizes[32],ports[32];
static uint8_t delayed[54];static uint16_t delayed_length,delayed_port;
static unsigned int delayed_count,logs;
static uint8_t *armed_buffer;static uint16_t armed_length;
static char lines[64][256];
/** @brief 读取测试HAL毫秒计数。 @return 可由场景推进和回绕的计数。 */
static uint32_t Tick(void) { return now; }
/** @brief 复制被测模块日志。 @param s 只读n字节输入。 @param n 正文字节数。 @return 无返回值。 */
static void Log(const char *s,int n) { assert(n>0&&n<256);if(logs<64) { memcpy(lines[logs],s,(size_t)n);lines[logs++][n]=0; } }
/** @brief 搜索已复制日志。 @param s 非NULL目标文本。 @return 是否找到。 */
static bool Contains(const char *s) { for(unsigned int i=0;i<logs;++i) { if(strstr(lines[i],s)) { return true; } }return false; }
/** @brief 写测试帧16位大端字段。 @param p 非NULL两字节输出。 @param v 整数值。 @return 无返回值。 */
static void Put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }
/** @brief 复制测试pbuf链。 @param p 借用链首。 @param d 可写目标。 @param len 请求字节数。 @param offset 链内偏移。 @return 实际复制字节数。 */
u16_t pbuf_copy_partial(const struct pbuf *p,void *d,u16_t len,u16_t offset)
{ unsigned int done=0;while(p&&offset>=p->len) { offset-=p->len;p=p->next; }while(p&&done<len) { unsigned int n=p->len-offset;if(n>len-done) { n=len-done; }memcpy((uint8_t *)d+done,(uint8_t *)p->payload+offset,n);done+=n;offset=0;p=p->next; }return (u16_t)done; }
/** @brief 分配测试pbuf并计引用。 @param layer 仅支持TRANSPORT。 @param len 负载字节数。 @param type 仅支持RAM。 @return 拥有引用或注入失败时NULL。 */
struct pbuf *pbuf_alloc(pbuf_layer layer,u16_t len,pbuf_type type)
{ assert(layer==PBUF_TRANSPORT&&type==PBUF_RAM);if(fail_alloc) { return NULL; }struct pbuf *p=calloc(1,sizeof(*p)+len);assert(p);p->payload=p+1;p->len=p->tot_len=len;p->ref=1;++refs;return p; }
/** @brief 释放测试链引用。 @param p 非NULL持有引用。 @return 本节点是否释放。 */
u8_t pbuf_free(struct pbuf *p) { assert(p&&p->ref&&refs);--refs;if(--p->ref) { return 0; }if(p->next) { pbuf_free(p->next); }free(p);return 1; }
/** @brief 复制测试负载。 @param p 可写pbuf。 @param s 只读输入。 @param n 字节数。 @return ERR_OK。 */
err_t pbuf_take(struct pbuf *p,const void *s,u16_t n) { memcpy(p->payload,s,n);return ERR_OK; }
/** @brief 从两个静态测试PCB槽取得一项。 @return PCB或满槽时NULL。 */
struct udp_pcb *udp_new(void) { for(unsigned int i=0;i<2;++i) { if(!live[i]) { live[i]=true;memset(&pcbs[i],0,sizeof(pcbs[i]));return &pcbs[i]; } }return NULL; }
/** @brief 回收测试PCB。 @param p 本测试拥有的有效PCB。 @return 无返回值。 */
void udp_remove(struct udp_pcb *p) { unsigned int i=(unsigned int)(p-pcbs);assert(i<2&&live[i]);live[i]=false; }
/** @brief 记录测试本地端口。 @param p 测试PCB。 @param a 借用地址，本替身不使用。 @param port 绑定端口。 @return ERR_OK。 */
err_t udp_bind(struct udp_pcb *p,const ip_addr_t *a,u16_t port) { (void)a;p->local_port=port;return ERR_OK; }
/** @brief 核对测试网卡绑定。 @param p 未使用PCB。 @param n 须为gnetif。 @return 无返回值。 */
void udp_bind_netif(struct udp_pcb *p,const struct netif *n) { (void)p;assert(n==&gnetif); }
/** @brief 保存被测接收函数。 @param p PCB。 @param cb 回调指针。 @param arg 借用上下文。 @return 无返回值。 */
void udp_recv(struct udp_pcb *p,udp_recv_fn cb,void *arg) { p->recv=cb;p->recv_arg=arg; }
/** @brief 提供注入RX快照。 @param t 非NULL输出。 @return 注入状态。 */
BSP_PtpStatus BSP_PTP_ReadRx(BSP_PtpTime *t) { *t=rx_stamp;return rx_result; }
/** @brief 提供较晚的本地硬件时间。 @param t 非NULL输出。 @return OK。 */
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *t) { *t=(BSP_PtpTime){1000,0};return BSP_PTP_OK; }
/** @brief 模拟共享TX令牌清理。 @return 活动令牌时NOT_READY，否则OK。 */
BSP_PtpStatus BSP_PTP_TxClean(void) { return token?BSP_PTP_NOT_READY:BSP_PTP_OK; }
/** @brief 验证实际发送帧并模拟布防。 @param b 借用帧地址。 @param n 帧字节数。 @return 注入状态。 */
BSP_PtpStatus BSP_PTP_TxBegin(const uint8_t *b,uint16_t n) { assert(b&&n>=86&&!token);++begins;if(begin_result==BSP_PTP_OK) { token=true;armed_buffer=(uint8_t *)b;armed_length=n; }return begin_result; }
/** @brief 模拟收回唯一TX快照。 @param t 非NULL输出。 @return 注入状态。 */
BSP_PtpStatus BSP_PTP_TxEnd(BSP_PtpTime *t) { assert(token);token=false;++ends;*t=tx_stamp;return end_result; }
/** @brief 构建测试IPv4/UDP以太网帧。 @param f 至少128字节输出。 @param data 只读负载。 @param len 负载长度。 @param port 双端端口。 @param outgoing 本机输出方向。 @return 帧长度。 */
static uint16_t Frame(uint8_t *f,const uint8_t *data,uint16_t len,uint16_t port,bool outgoing)
{ memset(f,0,128);Put16(f+12,0x0800);f[14]=0x45;Put16(f+16,(uint16_t)(28+len));f[23]=17;memcpy(f+26,outgoing?profile.local_ip:profile.peer_ip,4);memcpy(f+30,outgoing?profile.peer_ip:profile.local_ip,4);Put16(f+34,port);Put16(f+36,port);Put16(f+38,(uint16_t)(8+len));memcpy(f+42,data,len);return (uint16_t)(42+len); }
/** @brief 模拟链路发送并检查共享令牌地址。 @param n 网卡。 @param p 借用帧。 @return ERR_OK。 */
static err_t OriginalOutput(struct netif *n,struct pbuf *p) { assert(n==&gnetif&&p);if(token) { assert(p->payload==armed_buffer&&p->len==armed_length&&p->len==p->tot_len&&!p->next); }++outputs;if(call_deinit) { deinit_result=PtpBoardSync_Deinit(); }return ERR_OK; }
/** @brief 同步分发测试UDP负载。 @param p 借用以太网帧。 @param n 未使用网卡。 @return ERR_OK。 */
static err_t OriginalInput(struct pbuf *p,struct netif *n)
{ (void)n;++inputs;uint8_t f[128];pbuf_copy_partial(p,f,p->tot_len,0);const uint16_t port=(uint16_t)((uint16_t)f[36]<<8|f[37]);const uint16_t len=(uint16_t)(p->tot_len-42);ip_addr_t src;memcpy(&src.addr,f+26,4);for(unsigned int i=0;i<2;++i) { if(live[i]&&pcbs[i].local_port==port) { struct pbuf *payload=pbuf_alloc(PBUF_TRANSPORT,len,PBUF_RAM);memcpy(payload->payload,f+42,len);pcbs[i].recv(pcbs[i].recv_arg,&pcbs[i],payload,&src,port); } }return ERR_OK; }
/** @brief 经真实已装钩子输出测试帧。 @param d 只读负载。 @param length 字节数。 @param port 双端端口。 @return 无返回值。 */
static void OutputData(const uint8_t *d,uint16_t length,uint16_t port)
{ uint8_t f[128];uint16_t n=Frame(f,d,length,port,true);struct pbuf p={0},tail={0};p.payload=f;p.len=p.tot_len=n;if(chain) { p.len=n-1;p.next=&tail;tail.payload=f+n-1;tail.len=tail.tot_len=1; }assert(gnetif.linkoutput(&gnetif,&p)==ERR_OK); }
/** @brief 复制提交并模拟即时或ARP延后输出。 @param pcb 本地PCB。 @param p 借用负载。 @param a 未使用对端IP。 @param port 对端端口。 @return 正常OK或注入ERR_IF。 */
err_t udp_sendto(struct udp_pcb *pcb,struct pbuf *p,const ip_addr_t *a,u16_t port)
{ (void)a;assert(pcb->local_port==port&&sends<32);sizes[sends]=p->tot_len;ports[sends]=port;pbuf_copy_partial(p,sent[sends],p->tot_len,0);++sends;if(fail_send) { return ERR_IF; }if(defer) { assert(!delayed_count);memcpy(delayed,p->payload,p->tot_len);delayed_length=p->tot_len;delayed_port=port;++delayed_count; }else { OutputData(p->payload,p->tot_len,port); }return ERR_OK; }
/** @brief 模拟ARP解析后实际输出。 @return 无返回值。 */
static void Flush(void) { assert(delayed_count);delayed_count=0;OutputData(delayed,delayed_length,delayed_port); }
/** @brief 生成测试对端端口身份。 @param id 至少10字节输出。 @return 无返回值。 */
static void PeerIdentity(uint8_t id[10]) { uint8_t mac[6]={2,0,0,0,0,profile.role==BOARD_ROLE_A?2:1};PtpWire_Identity(mac,id); }
/** @brief 经真实输入钩子交付消息。 @param d 负载。 @param len 字节数。 @param port 端口。 @param tamper 非零为帧内翻转偏移。 @return 无返回值。 */
static void DeliverBytes(const uint8_t *d,uint16_t len,uint16_t port,unsigned int tamper)
{ uint8_t f[128];uint16_t n=Frame(f,d,len,port,false);if(tamper) { f[tamper]^=1; }struct pbuf p={0};p.payload=f;p.len=p.tot_len=n;assert(gnetif.input(&p,&gnetif)==ERR_OK); }
/** @brief 构造并交付标准对端消息。 @param type 消息类型。 @param seq 序号。 @param ns 纳秒字段。 @return 无返回值。 */
static void Deliver(PtpWireType type,uint16_t seq,uint64_t ns)
{ PtpWireMessage m={0};m.type=type;m.sequence=seq;m.timestamp_ns=ns;PeerIdentity(m.source);PtpWire_Identity(profile.local_mac,m.requester);uint8_t d[54];uint16_t len=(uint16_t)PtpWire_Encode(&m,d);DeliverBytes(d,len,type==PTP_WIRE_SYNC||type==PTP_WIRE_DELAY_REQ?319:320,0); }
/** @brief 无硬件输入上下文直接调用已绑定回调。 @param d 负载。 @param len 字节数。 @param port 端口。 @param foreign 是否注入错误源IP。 @return 无返回值。 */
static void CallbackBytes(const uint8_t *d,uint16_t len,uint16_t port,bool foreign)
{ ip_addr_t source;memcpy(&source.addr,profile.peer_ip,4);if(foreign) { source.addr^=1; }for(unsigned int i=0;i<2;++i) { if(live[i]&&pcbs[i].local_port==port) { struct pbuf *p=pbuf_alloc(PBUF_TRANSPORT,len,PBUF_RAM);memcpy(p->payload,d,len);pcbs[i].recv(NULL,&pcbs[i],p,&source,port); } } }
/** @brief 安全拆装包装链并清测试状态。 @param role 本机场景角色。 @param start 起始HAL毫秒。 @return 无返回值。 */
static void Reset(BoardRole role,uint32_t start)
{ assert(PtpBoardSync_Deinit());assert(!token&&!refs);memset(&gnetif,0,sizeof(gnetif));gnetif.input=OriginalInput;gnetif.linkoutput=OriginalOutput;gnetif.flags=NETIF_FLAG_UP|NETIF_FLAG_LINK_UP;assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,role==BOARD_ROLE_A?BoardIdentity_DefaultMap.a_uid:BoardIdentity_DefaultMap.b_uid,&profile));memcpy(&gnetif.ip_addr.addr,profile.local_ip,4);now=start;exchange_count=0;begins=ends=sends=inputs=outputs=logs=delayed_count=0;defer=chain=fail_send=fail_alloc=call_deinit=false;rx_result=begin_result=end_result=BSP_PTP_OK;rx_stamp=(BSP_PtpTime){10,200};tx_stamp=(BSP_PtpTime){10,300};PtpRx_Init(now,NULL);PtpTx_Init(now,profile.uid_words,NULL);assert(PtpBoardSync_Init(now,&profile,Tick,Log));PtpBoardSync_Process(now); }
/** @brief 推进六秒启动静默。 @return 无返回值。 */
static void Ready(void) { now+=6000;PtpBoardSync_Process(now); }
/** @brief 完成一个B端标准四消息候选。 @param seq A端Sync序号。 @return 无返回值。 */
static void BComplete(uint16_t seq)
{ uint32_t delta=exchange_count++*1000U;rx_stamp.nanoseconds=200+delta;tx_stamp.nanoseconds=300+delta;Deliver(PTP_WIRE_SYNC,seq,0);Deliver(PTP_WIRE_FOLLOW_UP,seq,UINT64_C(10000000100)+delta);PtpBoardSync_Process(now);assert(sends);PtpWireMessage req;assert(PtpWire_Decode(sent[sends-1],sizes[sends-1],&req)==PTP_WIRE_OK&&req.type==PTP_WIRE_DELAY_REQ);Deliver(PTP_WIRE_DELAY_RESP,req.sequence,UINT64_C(10000000400)+delta); }
/** @brief 执行真实传输与包装链场景断言。 @return 全部通过返回0，失败由assert终止。 */
int main(void)
{
    Reset(BOARD_ROLE_A,0);assert(sends==0);now=5999;PtpBoardSync_Process(now);assert(!sends);now=6000;PtpBoardSync_Process(now);
    assert(sends==2&&sent[0][0]==0&&sent[1][0]==8&&begins==1&&ends==1&&ports[0]==319&&ports[1]==320);
    PtpWireMessage fu;assert(PtpWire_Decode(sent[1],sizes[1],&fu)==PTP_WIRE_OK&&fu.timestamp_ns==UINT64_C(10000000300));
    Deliver(PTP_WIRE_DELAY_REQ,42,0);PtpBoardSync_Process(now);assert(sends==3&&sent[2][0]==9&&begins==1);
    Deliver(PTP_WIRE_DELAY_REQ,42,0);PtpBoardSync_Process(now);assert(sends==3);
    puts("PASS A: six-second startup, standard Sync/FU DMA TX and DelayResp DMA RX, duplicate ignored");
    Reset(BOARD_ROLE_B,0);Ready();BComplete(5);SyncExchange e;uint32_t captured;
    assert(PtpBoardSync_TakeExchange(&e,&captured));assert(e.t1_a_tx_ns==UINT64_C(10000000100)&&e.t2_b_rx_ns==UINT64_C(10000000200)&&e.t3_b_tx_ns==UINT64_C(10000000300)&&e.t4_a_rx_ns==UINT64_C(10000000400));
    assert(!PtpBoardSync_TakeExchange(&e,&captured));Deliver(PTP_WIRE_DELAY_RESP,1,UINT64_C(10000000400));assert(!PtpBoardSync_TakeExchange(&e,&captured));
    puts("PASS B: natural A/B quartet copied once, independent DelayReq sequence, replay ignored");
    Reset(BOARD_ROLE_B,0);Ready();Deliver(PTP_WIRE_FOLLOW_UP,1,100);Deliver(PTP_WIRE_SYNC,1,0);Deliver(PTP_WIRE_FOLLOW_UP,2,100);assert(!sends);now+=1000;PtpBoardSync_Process(now);Deliver(PTP_WIRE_FOLLOW_UP,1,100);assert(!sends&&!PtpBoardSync_TakeExchange(&e,&captured));
    Reset(BOARD_ROLE_B,UINT32_MAX-3000U);Ready();BComplete(65535);assert(PtpBoardSync_TakeExchange(&e,&captured));BComplete(0);assert(PtpBoardSync_TakeExchange(&e,&captured));
    puts("PASS timeout/wrap: out-of-order FU and mismatched sequence rejected, HAL tick and PTP sequence wrap supported");
    Reset(BOARD_ROLE_B,0);Ready();rx_result=BSP_PTP_NOT_READY;Deliver(PTP_WIRE_SYNC,1,0);Deliver(PTP_WIRE_FOLLOW_UP,1,100);PtpBoardSync_Process(now);assert(!sends);
    Reset(BOARD_ROLE_B,0);Ready();Deliver(PTP_WIRE_SYNC,1,0);Deliver(PTP_WIRE_FOLLOW_UP,1,100);begin_result=BSP_PTP_NOT_READY;PtpBoardSync_Process(now);Deliver(PTP_WIRE_DELAY_RESP,1,200);assert(!PtpBoardSync_TakeExchange(&e,&captured));
    puts("PASS missing stamps: no valid quartet from absent RX or failed TX Begin");
    Reset(BOARD_ROLE_B,0);Ready();Deliver(PTP_WIRE_SYNC,1,0);Deliver(PTP_WIRE_FOLLOW_UP,1,100);defer=true;PtpBoardSync_Process(now);assert(!begins&&delayed_count);now+=1000;PtpBoardSync_Process(now);defer=false;Flush();assert(!begins&&!PtpBoardSync_TakeExchange(&e,&captured));
    Reset(BOARD_ROLE_B,0);Ready();Deliver(PTP_WIRE_SYNC,1,0);Deliver(PTP_WIRE_FOLLOW_UP,1,UINT64_C(10000000100));defer=true;PtpBoardSync_Process(now);assert(!begins);defer=false;Flush();assert(begins==1&&ends==1);Deliver(PTP_WIRE_DELAY_RESP,1,UINT64_C(10000000400));assert(PtpBoardSync_TakeExchange(&e,&captured));
    puts("PASS deferred ARP: only actual unexpired output receives one paired hardware token");
    Reset(BOARD_ROLE_B,0);Ready();BComplete(1);PtpBoardSync_NotifyClockChange(2);assert(!PtpBoardSync_TakeExchange(&e,&captured));BComplete(2);assert(PtpBoardSync_TakeExchange(&e,&captured));
    Reset(BOARD_ROLE_B,0);Ready();call_deinit=true;BComplete(1);assert(!deinit_result);call_deinit=false;
    netif_input_fn in=gnetif.input;netif_linkoutput_fn output=gnetif.linkoutput;gnetif.input=OriginalInput;assert(!PtpBoardSync_Deinit()&&gnetif.linkoutput==output);gnetif.input=in;assert(PtpBoardSync_Deinit());
    assert(!token&&!refs&&!live[0]&&!live[1]);
    puts("PASS lifecycle: phase/rate notification clears exchange without startup delay; active/lost-hook deinit fails closed");
    Reset(BOARD_ROLE_B,0);Ready();
    PtpWireMessage bad={0};bad.type=PTP_WIRE_SYNC;bad.sequence=1;PeerIdentity(bad.source);uint8_t bytes[54];assert(PtpWire_Encode(&bad,bytes)==44);
    CallbackBytes(bytes,44,319,false);Deliver(PTP_WIRE_FOLLOW_UP,1,100);PtpBoardSync_Process(now);assert(!sends);
    CallbackBytes(bytes,44,319,true);bytes[8]=1;DeliverBytes(bytes,44,319,0);bytes[8]=0;bytes[20]^=1;DeliverBytes(bytes,44,319,0);bytes[20]^=1;
    DeliverBytes(bytes,44,319,26);DeliverBytes(bytes,44,319,20);DeliverBytes(bytes,44,320,0);DeliverBytes(bytes,43,319,0);
    now+=1000;PtpBoardSync_Process(now);assert(Contains("CORRECTION=1")&&Contains("MISSING=2"));assert(!sends);
    BComplete(2);assert(PtpBoardSync_TakeExchange(&e,&captured));
    puts("PASS ingress: missing frame context, wrong IP/identity/port, fragments, short body and nonzero correction cannot fabricate stamps");
    Reset(BOARD_ROLE_B,0);Ready();Deliver(PTP_WIRE_SYNC,1,0);Deliver(PTP_WIRE_FOLLOW_UP,1,UINT64_C(10000000100));PtpBoardSync_Process(now);
    bad.type=PTP_WIRE_DELAY_RESP;bad.sequence=1;bad.timestamp_ns=UINT64_C(10000000400);PeerIdentity(bad.source);PtpWire_Identity(profile.local_mac,bad.requester);bad.requester[0]^=1;assert(PtpWire_Encode(&bad,bytes)==54);DeliverBytes(bytes,54,320,0);assert(!PtpBoardSync_TakeExchange(&e,&captured));
    Deliver(PTP_WIRE_DELAY_RESP,2,UINT64_C(10000000400));assert(!PtpBoardSync_TakeExchange(&e,&captured));Deliver(PTP_WIRE_DELAY_RESP,1,UINT64_C(10000000400));assert(PtpBoardSync_TakeExchange(&e,&captured));
    puts("PASS DelayResp: exact requestingPortIdentity and independent request sequence required");
    for(unsigned int mode=0;mode<5;++mode)
    {
        Reset(BOARD_ROLE_B,0);Ready();Deliver(PTP_WIRE_SYNC,1,0);Deliver(PTP_WIRE_FOLLOW_UP,1,UINT64_C(10000000100));
        if(mode==0) { chain=true; }if(mode==1) { end_result=BSP_PTP_BAD_SNAPSHOT; }if(mode==2) { tx_stamp.nanoseconds=1000000000U; }if(mode==3) { fail_alloc=true; }if(mode==4) { fail_send=true; }
        PtpBoardSync_Process(now);fail_alloc=false;Deliver(PTP_WIRE_DELAY_RESP,1,UINT64_C(10000000400));assert(!PtpBoardSync_TakeExchange(&e,&captured)&&!token&&!refs);
    }
    puts("PASS egress: chain, End error, invalid stamp, allocation and send failure terminate without false handoff or leaks");
    Reset(BOARD_ROLE_B,0);Ready();BComplete(10);assert(PtpBoardSync_TakeExchange(&e,&captured));
    Deliver(PTP_WIRE_SYNC,11,0);Deliver(PTP_WIRE_FOLLOW_UP,11,UINT64_C(9000000100));assert(PtpBoardSync_TakePeerReset()&&!PtpBoardSync_TakePeerReset());unsigned int before=sends;
    Deliver(PTP_WIRE_SYNC,1,0);Deliver(PTP_WIRE_FOLLOW_UP,1,100);PtpBoardSync_Process(now);assert(sends==before);now+=6000;PtpBoardSync_Process(now);BComplete(1);assert(PtpBoardSync_TakeExchange(&e,&captured));
    now+=6000;PtpBoardSync_Process(now);assert(PtpBoardSync_TakePeerReset());
    puts("PASS peer reset: observed clock rollback/inactivity clears history, signals foreground and quarantines without claiming unique boot identity");
    Reset(BOARD_ROLE_B,0);Ready();now+=1000;PtpBoardSync_Process(now);assert(Contains("$PTP_SYNC_STATS"));assert(PtpBoardSync_Deinit());
    puts("PASS transport integration through actual legacy PtpRx/PtpTx wrappers");return 0;
}
