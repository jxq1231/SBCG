/**
 * @file        ptp_tx.c
 * @brief       观测v1 UDP探测应答的MAC发送描述符时间。
 * @details     包装gnetif.linkoutput，只对UID匹配的type=3单段帧申请TX时间戳，调用原输出后取回快照，4项CPU队列交前台日志。
 * @note        NO_SYS=1且同步发送；在PTP时钟与RX观察器之后、UDPTimeExchange外层之前初始化；不修改DMA配置。
 */

#include "ptp_tx.h"
#include "BSP/bsp_ptp_tx.h"
#include "Network/udp_config.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if NO_SYS != 1
#error "TX timestamp observation requires synchronous NO_SYS output"
#endif
_Static_assert(UDP_HEARTBEAT_PACKET_BYTES == 28U, "TX observer requires the v1 envelope");
#define TX_QUEUE_CAPACITY 4U
#define TX_LOG_PERIOD_MS 1000U

typedef struct
{
    uint32_t request_sequence, client_tick_ms;
    BSP_PtpTime time;
} TxRecord;

extern struct netif gnetif;
static netif_linkoutput_fn original_output;
static PtpTx_Log emit;
static uint8_t uid_wire[12];
static bool configured;
static TxRecord queue[TX_QUEUE_CAPACITY];
static uint32_t head, count, last_log_ms;
static uint32_t valid, arm_errors, snapshot_errors, unsupported, overflow, init_errors;

/**
 * @brief   读取网络大端序16位整数。
 * @param   p 至少2字节可读区域，不可为NULL；借用且不保存。
 * @return  解码后的无符号整数。
 */
static uint16_t Get16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0]<<8U | p[1]); }
/**
 * @brief   读取网络大端序32位整数。
 * @param   p 至少4字节可读区域，不可为NULL；借用且不保存。
 * @return  解码后的无符号整数。
 */
static uint32_t Get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24U | (uint32_t)p[1]<<16U | (uint32_t)p[2]<<8U | p[3]; }

/**
 * @brief   筛选UID和地址匹配的未分片type=3 UDP应答。
 * @param   netif 有效输出网卡，不可为NULL，只读本机IPv4地址。
 * @param   packet 借用以太网pbuf，可为NULL；不释放或修改。
 * @param   key 至少28字节可写输出缓冲，不可为NULL；复制匹配的UDP负载。
 * @return  true表示匹配并写入key；false时调用方不得使用key内容。
 * @note    仅决定是否请求TX快照，不替代协议栈校验。
 */
static bool ReplyKey(const struct netif *netif,const struct pbuf *packet,uint8_t key[28])
{
    uint8_t header[34],udp[36];
    if (!configured || packet == NULL || packet->tot_len < 70U ||
        pbuf_copy_partial(packet,header,sizeof(header),0) != sizeof(header)) { return false; }
    const uint8_t *ip=header+14;
    const uint32_t ip_header_bytes=(ip[0]&15U)*4U;
    const uint8_t destination[4]={UDP_HEARTBEAT_DEST_A,UDP_HEARTBEAT_DEST_B,
                                UDP_HEARTBEAT_DEST_C,UDP_HEARTBEAT_DEST_D};
    ip4_addr_t source;
    memcpy(&source.addr,ip+12,sizeof(source.addr));
    if (Get16(header+12) != 0x0800U || (ip[0]>>4U) != 4U || ip_header_bytes < 20U ||
        ip[9] != 17U || (Get16(ip+6)&0xBFFFU) != 0U ||
        Get16(ip+2) != ip_header_bytes+sizeof(udp) ||
        packet->tot_len < 14U+ip_header_bytes+sizeof(udp) ||
        !ip4_addr_cmp(&source,netif_ip4_addr(netif)) || memcmp(ip+16,destination,4) != 0)
    { return false; }
    if (pbuf_copy_partial(packet,udp,sizeof(udp),(u16_t)(14U+ip_header_bytes)) != sizeof(udp) ||
        Get16(udp) != UDP_PROBE_BOARD_PORT || Get16(udp+2) != UDP_PROBE_PC_PORT ||
        Get16(udp+4) != sizeof(udp) || memcmp(udp+8,"SBCG\1\3\0\34",8) != 0 ||
        memcmp(udp+24,uid_wire,sizeof(uid_wire)) != 0) { return false; }
    memcpy(key,udp+8,28);
    return true;
}

/**
 * @brief   围绕原链路输出申请并获取匹配应答的TX硬件时间。
 * @param   netif 有效输出网卡，不可为NULL，借用并传给原函数。
 * @param   packet 借用的发送pbuf；本函数不释放，不支持为链式或非连续帧取时间。
 * @return  原链路输出函数的LwIP错误码。
 * @note    NO_SYS前台同步输出；先清理已完成请求，再Begin→原输出→End，快照进入CPU队列。
 */
static err_t Output(struct netif *netif,struct pbuf *packet)
{
    /* Reclaim only completed CPU-owned requests before any frame reuses them. */
    (void)BSP_PTP_TxClean();
    uint8_t key[28];
    if (!ReplyKey(netif,packet,key)) { return original_output(netif,packet); }
    if (packet->next != NULL || packet->len != packet->tot_len)
    {
        ++unsupported;
        return original_output(netif,packet);
    }
    if (BSP_PTP_TxBegin(packet->payload,packet->len) != BSP_PTP_OK)
    {
        ++arm_errors;
        return original_output(netif,packet);
    }
    const err_t result=original_output(netif,packet);
    BSP_PtpTime time;
    const BSP_PtpStatus status=BSP_PTP_TxEnd(&time);
    if (result != ERR_OK || status != BSP_PTP_OK) { ++snapshot_errors; return result; }
    ++valid;
    if (count == TX_QUEUE_CAPACITY) { ++overflow; return result; }
    queue[(head+count)%TX_QUEUE_CAPACITY]=(TxRecord){Get32(key+8),Get32(key+12),time};
    ++count;
    return result;
}

/**
 * @brief   复制UID、复位TX队列统计并安装链路输出包装。
 * @param   now_ms 初始化毫秒计数，作为日志节流起点。
 * @param   uid_words 至少3个UID字，允许NULL以禁用匹配并记录错误；内容复制，不保存指针。
 * @param   log 日志回调，可为NULL；后续调用期间须有效。
 * @return  无返回值；缺失UID或原输出时增加初始化错误。
 * @note    PTP时钟及RX观察器之后前台初始化；重新初始化前须先卸载UDPTimeExchange外层。
 */
void PtpTx_Init(uint32_t now_ms, const uint32_t uid_words[3], PtpTx_Log log)
{
    emit=log; last_log_ms=now_ms;
    head=count=valid=arm_errors=snapshot_errors=unsupported=overflow=init_errors=0U;
    configured=uid_words != NULL;
    if (configured)
    {
        for (uint32_t i=0U;i<3U;++i)
        {
            uid_wire[i*4U]=(uint8_t)(uid_words[i]>>24U);
            uid_wire[i*4U+1U]=(uint8_t)(uid_words[i]>>16U);
            uid_wire[i*4U+2U]=(uint8_t)(uid_words[i]>>8U);
            uid_wire[i*4U+3U]=(uint8_t)uid_words[i];
        }
    }
    else { ++init_errors; }
    if (gnetif.linkoutput != Output) { original_output=gnetif.linkoutput; }
    if (original_output == NULL) { ++init_errors; return; }
    gnetif.linkoutput=Output;
}

/**
 * @brief   回收已完成TX请求，并限速输出队列记录和统计。
 * @param   now_ms 前台当前毫秒计数，日志按无符号差值每秒节流。
 * @return  无返回值。
 * @note    每次调用先尝试清理；DMA仍持有OWN时不强行回收，日志固定SYNC=0。
 */
void PtpTx_Process(uint32_t now_ms)
{
    const BSP_PtpStatus cleanup=BSP_PTP_TxClean();
    if (emit == NULL || (uint32_t)(now_ms-last_log_ms) < TX_LOG_PERIOD_MS) { return; }
    last_log_ms=now_ms;
    /* Pending OWN is already counted by the failed capture; mapping loss is
       a separate visible fault, counted at most once per reporting period. */
    if (cleanup != BSP_PTP_OK && cleanup != BSP_PTP_NOT_READY) { ++snapshot_errors; }
    char line[160];
    if (count != 0U)
    {
        const TxRecord record=queue[head];head=(head+1U)%TX_QUEUE_CAPACITY;--count;
        const int length=snprintf(line,sizeof(line),
            "$PTP_TX,REQ_SEQ=%" PRIu32 ",CLIENT_TICK_MS=%" PRIu32
            ",SEC=%" PRIu32 ",NSEC=%" PRIu32 ",SYNC=0,SOURCE=DMA\r\n",
            record.request_sequence,record.client_tick_ms,record.time.seconds,record.time.nanoseconds);
        emit(line,length);
    }
    const int length=snprintf(line,sizeof(line),
        "$PTP_TX_STATS,VALID=%" PRIu32 ",ARM_ERR=%" PRIu32 ",SNAP_ERR=%" PRIu32
        ",UNSUPPORTED=%" PRIu32 ",OVERFLOW=%" PRIu32 ",INIT_ERR=%" PRIu32 "\r\n",
        valid,arm_errors,snapshot_errors,unsupported,overflow,init_errors);
    emit(line,length);
}
