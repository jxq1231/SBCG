/**
 * @file        udp_chirp_capture.c
 * @brief       将候选附近 End 验证 PCM 快照按二进制头分片经 UDP 下传并输出一行元数据。
 * @details     每片 56 字节大端头（PCM1 魔数、版本/标志、片序号/总数/样本数、
 *              capture_id/session、首样本绝对帧、hint/raw_first 帧），后随最多 512 个 int16；
 *              PC 侧按键名 (session,capture_id) 重组。发送成功只证明协议栈接受。
 * @note        前台串行、懒绑定 5008 端口，不修改生成网卡；发送失败即停并计数，不重试。
 */
#include "udp_chirp_capture.h"
#include "udp_config.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern struct netif gnetif;

static struct udp_pcb *pcb;
static UDPChirpCaptureSnapshot state;
static void (*logger)(const char *, int);

_Static_assert(UDP_CHIRP_CAPTURE_HEADER_BYTES==56U,
    "Wire header size must stay 56 bytes; host reassembler must match");
_Static_assert((UDP_CHIRP_CAPTURE_CHUNK_FRAMES*2U+UDP_CHIRP_CAPTURE_HEADER_BYTES)<=1400U,
    "Datagram must stay below the link MTU to avoid IP fragmentation");

/** @brief 将value转为至少21字节十进制缓冲，避免nano的64位printf；无返回值。 */
static void Decimal(uint64_t value,char output[21])
{
    char reversed[20];unsigned n=0U;
    do{reversed[n++]=(char)('0'+value%10U);value/=10U;}while(value!=0U);
    for(unsigned i=0U;i<n;++i)output[i]=reversed[n-i-1U];
    output[n]='\0';
}
/** @brief 饱和增加计数value与amount，仅前台，无返回值。 */
static void Add(uint32_t *value,uint32_t amount)
{*value=UINT32_MAX-*value<amount ? UINT32_MAX:*value+amount;}
/** @brief 将32位整数写成四字节网络大端序，dest至少4字节，无返回值。 */
static void Put32(uint8_t *dest,uint32_t value)
{
    dest[0]=(uint8_t)(value>>24);dest[1]=(uint8_t)(value>>16);
    dest[2]=(uint8_t)(value>>8);dest[3]=(uint8_t)value;
}
/** @brief 将64位整数写成八字节网络大端序，dest至少8字节，无返回值。 */
static void Put64(uint8_t *dest,uint64_t value)
{
    for(unsigned i=0U;i<8U;++i)dest[i]=(uint8_t)(value>>(56U-8U*i));
}
/**
 * @brief 重置计数并懒删除旧UDP控制块；不创建pcb、不发送。
 * @param now_ms 当前HAL毫秒，保留签名仅作接口一致性；本模块不使用。
 * @param log 可为NULL的日志回调，返回前须消费文本。
 * @return 无返回值。
 * @note 前台、MX_LWIP_Init之后调用；该模块没有周期状态行。
 */
void UDPChirpCapture_Init(uint32_t now_ms,void (*log)(const char *,int))
{
    (void)now_ms;
    if(pcb!=NULL){udp_remove(pcb);pcb=NULL;}
    logger=log;
    state=(UDPChirpCaptureSnapshot){0};
}
/**
 * @brief 懒创建并绑定5008板端UDP控制块。
 * @param result 非NULL输出字符串，与心跳模块同风格，指向静态文本。
 * @return LwIP错误码，ERR_OK表示可用。
 * @note 与心跳相同模式：绑定生成接口的本机IP，不修改其配置；首次发送才创建。
 */
static err_t EnsurePcb(const char **result)
{
    if(pcb!=NULL){*result="OK";return ERR_OK;}
    pcb=udp_new();
    if(pcb==NULL){*result="PCB_ERROR";Add(&state.pcb_errors,1U);return ERR_MEM;}
    const err_t bind_err=udp_bind(pcb,netif_ip_addr4(&gnetif),UDP_CHIRP_CAPTURE_BOARD_PORT);
    if(bind_err!=ERR_OK){
        udp_remove(pcb);pcb=NULL;*result="BIND_ERROR";Add(&state.pcb_errors,1U);return bind_err;
    }
    udp_bind_netif(pcb,&gnetif);
    *result="OK";
    return ERR_OK;
}
/**
 * @brief 发送一个候选切片的全部分片，完成后（log非NULL时）输出一行元数据。
 * @param slice 非NULL只读切片，仅本次调用借用，不保存指针。
 * @return true为全部分片提交成功；false见快照/日志分类。
 * @note 前台串行；任一数据报失败即停止，不重试；链路未就绪仅计数不发送。
 */
bool UDPChirpCapture_Send(const ChirpCaptureSlice *slice)
{
    static uint8_t datagram[UDP_CHIRP_CAPTURE_HEADER_BYTES+UDP_CHIRP_CAPTURE_CHUNK_FRAMES*2U];
    const char *result="LINK_DOWN";
    if(slice==NULL || slice->samples==NULL || slice->frames==0U){
        Add(&state.captures_skipped,1U);return false;
    }
    if(logger==NULL){Add(&state.captures_skipped,1U);return false;}
    if(!netif_is_up(&gnetif) || !netif_is_link_up(&gnetif)){
        Add(&state.link_skips,1U);Add(&state.captures_skipped,1U);return false;
    }
    if(EnsurePcb(&result)!=ERR_OK){Add(&state.captures_skipped,1U);return false;}
    ip_addr_t destination;
    IP_ADDR4(&destination,UDP_HEARTBEAT_DEST_A,UDP_HEARTBEAT_DEST_B,
        UDP_HEARTBEAT_DEST_C,UDP_HEARTBEAT_DEST_D);
    const uint32_t total=(slice->frames+UDP_CHIRP_CAPTURE_CHUNK_FRAMES-1U)/
        UDP_CHIRP_CAPTURE_CHUNK_FRAMES;
    const uint8_t flags=(slice->left_clamped?UDP_CHIRP_CAPTURE_FLAG_LEFT_CLAMP:0U)|
        (slice->right_clamped?UDP_CHIRP_CAPTURE_FLAG_RIGHT_CLAMP:0U)|
        (slice->tail_topup?UDP_CHIRP_CAPTURE_FLAG_TAIL_TOPUP:0U);
    Add(&state.chunks_total,total);
    uint32_t sent=0U;bool ok=true;
    for(uint32_t chunk=0U;chunk<total;++chunk){
        const uint32_t offset=chunk*UDP_CHIRP_CAPTURE_CHUNK_FRAMES;
        const uint32_t present=slice->frames-offset<UDP_CHIRP_CAPTURE_CHUNK_FRAMES ?
            slice->frames-offset:UDP_CHIRP_CAPTURE_CHUNK_FRAMES;
        uint8_t *p=datagram;
        memcpy(p,"PCM1",4);p+=4;
        *p++=1U;                       /* version */
        *p++=1U;                       /* kind=raw capture slice */
        *p++=flags;
        *p++=0U;                       /* reserved */
        Put32(p,chunk);p+=4;
        Put32(p,total);p+=4;
        Put32(p,present);p+=4;
        Put32(p,0U);p+=4;              /* pad */
        Put32(p,slice->id);p+=4;
        Put32(p,slice->session);p+=4;
        Put64(p,slice->first_frame+(uint64_t)offset);p+=8;
        Put64(p,slice->hint_frame);p+=8;
        Put64(p,slice->raw_first_frame);p+=8;
        const uint16_t bytes=(uint16_t)(present*2U);
        memcpy(p,slice->samples+offset,bytes);
        const uint16_t packet_bytes=(uint16_t)((p-datagram)+bytes);
        struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,packet_bytes,PBUF_RAM);
        if(packet==NULL){
            Add(&state.packets_error,1U);Add(&state.chunk_errors,1U);ok=false;break;
        }
        if(pbuf_take(packet,datagram,packet_bytes)!=ERR_OK){
            pbuf_free(packet);Add(&state.packets_error,1U);Add(&state.chunk_errors,1U);ok=false;break;
        }
        const err_t err=udp_sendto(pcb,packet,&destination,UDP_CHIRP_CAPTURE_PC_PORT);
        pbuf_free(packet);
        if(err!=ERR_OK){
            Add(&state.packets_error,1U);Add(&state.chunk_errors,1U);ok=false;break;
        }
        Add(&state.packets_ok,1U);Add(&state.chunks_sent,1U);++sent;
    }
    char first[21];Decimal(slice->first_frame,first);
    char line[192];
    const int length=snprintf(line,sizeof line,
        "$CHIRP_CAPTURE,ID=%" PRIu32 ",SESSION=%" PRIu32 ",FIRST=%s,FRAMES=%u,"
        "CLAMP=%u,TAIL=%u,PKTS=%" PRIu32 ",TOTAL=%u,RESULT=%s\r\n",
        slice->id,slice->session,first,(unsigned)slice->frames,
        (slice->left_clamped||slice->right_clamped)?1U:0U,
        slice->tail_topup?1U:0U,sent,total,result);
    if(length>0 && length<(int)sizeof line)logger(line,length);
    if(ok){Add(&state.captures_ok,1U);return true;}
    Add(&state.captures_skipped,1U);return false;
}
/**
 * @brief 复制发送诊断计数。
 * @param out 非NULL独占输出；NULL不操作。
 * @return 无返回值。
 */
void UDPChirpCapture_GetSnapshot(UDPChirpCaptureSnapshot *out)
{if(out!=NULL){*out=state;}}