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

static uint16_t Get16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0]<<8U | p[1]); }
static uint32_t Get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24U | (uint32_t)p[1]<<16U | (uint32_t)p[2]<<8U | p[3]; }

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
