/**
 * @file        udp_probe.c
 * @brief       接收并校验PC探测请求，返回对应UDP应答。
 * @details     限制对端IP和端口，检查28字节v1封装及UID，交由PtpRx关联接收时间，再发送type=3应答。
 * @note        RAW接收回调在NO_SYS主循环输入链中同步执行；每个接收pbuf由回调释放。
 */

#include "udp_probe.h"
#include "udp_config.h"
#include "Sync/ptp_rx.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

_Static_assert(UDP_HEARTBEAT_PACKET_BYTES == 28U, "Probe v1 requires the reviewed 28-byte wire format");

extern struct netif gnetif;
static struct udp_pcb *probe_pcb;
static UDPProbe_Log emit;
static uint8_t uid_wire[12];
static ip_addr_t peer;
static uint32_t last_attempt_ms, last_log_ms;
static uint32_t valid, bad, foreign, ack_ok, ack_errors, init_errors;

/**
 * @brief   将一个UID字按网络大端序写入缓冲区。
 * @param   p 至少4字节可写区域，不可为NULL；指针不保存。
 * @param   n 待编码的32位无符号UID字。
 * @return  无返回值。
 */
static void Put32(uint8_t *p, uint32_t n)
{ p[0]=(uint8_t)(n>>24); p[1]=(uint8_t)(n>>16); p[2]=(uint8_t)(n>>8); p[3]=(uint8_t)n; }

/* Raw API callback is invoked by MX_LWIP_Process in the main loop, not ISR. */
/**
 * @brief   校验UDP探测包、认领RX快照并发送type=3应答。
 * @param   arg 注册的用户上下文，本实现未使用。
 * @param   pcb LwIP传入的有效UDP控制块，本函数用于发送应答但不释放。
 * @param   packet 接收pbuf，可为NULL；非NULL时所有路径释放该引用。
 * @param   source 源IP，允许NULL但将被拒收；仅回调期间借用。
 * @param   source_port 源UDP端口，须匹配配置的PC端口。
 * @return  无返回值；拒收及发送失败分别累计统计。
 * @note    在MX_LWIP_Process的RAW输入调用链中同步执行，非ISR；本地数据仅供本次复制和应答。
 */
static void Receive(void *arg, struct udp_pcb *pcb, struct pbuf *packet,
                    const ip_addr_t *source, u16_t source_port)
{
    (void)arg;
    if (packet == NULL) { return; }
    if (source == NULL || !ip_addr_cmp(source, &peer) || source_port != UDP_PROBE_PC_PORT)
    {
        ++foreign;
        pbuf_free(packet);
        return;
    }
    uint8_t data[UDP_HEARTBEAT_PACKET_BYTES];
    const int copied = packet->tot_len == sizeof(data) &&
        pbuf_copy_partial(packet, data, sizeof(data), 0) == sizeof(data);
    if (!copied || memcmp(data, "SBCG\1\2\0\34", 8) != 0 ||
        memcmp(data + 16, uid_wire, sizeof(uid_wire)) != 0)
    {
        ++bad;
        pbuf_free(packet);
        return;
    }
    ++valid;
    PtpRx_Observe(data);
    pbuf_free(packet);
    data[5] = 3;
    struct pbuf *reply = pbuf_alloc(PBUF_TRANSPORT, sizeof(data), PBUF_RAM);
    if (reply == NULL) { ++ack_errors; return; }
    err_t err = pbuf_take(reply, data, sizeof(data));
    if (err == ERR_OK) { err = udp_sendto(pcb, reply, &peer, UDP_PROBE_PC_PORT); }
    /* Raw API does not consume our reference; ARP manages any queued copy. */
    pbuf_free(reply);
    if (err == ERR_OK) { ++ack_ok; }
    else { ++ack_errors; }
}

/**
 * @brief   清理旧接收控制块，复制UID并复位探测统计。
 * @param   now_ms 初始化毫秒计数；允许首次Process立即尝试绑定。
 * @param   uid_words 至少3个只读UID字，不可为NULL；本函数复制内容。
 * @param   log 统计日志回调，可为NULL；后续使用期间须有效。
 * @return  无返回值。
 * @note    MX_LWIP_Init后由前台调用；实际udp_recv注册在Process进行。
 */
void UDPProbe_Init(uint32_t now_ms, const uint32_t uid_words[3], UDPProbe_Log log)
{
    if (probe_pcb != NULL) { udp_remove(probe_pcb); probe_pcb = NULL; }
    emit = log;
    for (unsigned int i = 0; i < 3; ++i) { Put32(uid_wire + i*4U, uid_words[i]); }
    IP_ADDR4(&peer, UDP_HEARTBEAT_DEST_A, UDP_HEARTBEAT_DEST_B,
             UDP_HEARTBEAT_DEST_C, UDP_HEARTBEAT_DEST_D);
    last_attempt_ms = now_ms - 1000U;
    last_log_ms = now_ms;
    valid = bad = foreign = ack_ok = ack_errors = init_errors = 0U;
}

/**
 * @brief   链路可用时重试注册UDP接收，每秒最多输出一次统计。
 * @param   now_ms 前台当前毫秒计数，重试与日志按无符号差值节流。
 * @return  无返回值。
 * @note    主循环调度；UDP请求本身在后续LwIP输入链的Receive中处理。
 */
void UDPProbe_Process(uint32_t now_ms)
{
    if (probe_pcb == NULL && netif_is_up(&gnetif) &&
        netif_is_link_up(&gnetif) && (uint32_t)(now_ms-last_attempt_ms) >= 1000U)
    {
        last_attempt_ms = now_ms;
        probe_pcb = udp_new();
        if (probe_pcb != NULL)
        {
            if (udp_bind(probe_pcb, netif_ip_addr4(&gnetif), UDP_PROBE_BOARD_PORT) == ERR_OK)
            {
                udp_bind_netif(probe_pcb, &gnetif);
                udp_recv(probe_pcb, Receive, NULL);
            }
            else { udp_remove(probe_pcb); probe_pcb = NULL; }
        }
        if (probe_pcb == NULL) { ++init_errors; }
    }
    if (emit != NULL && (uint32_t)(now_ms-last_log_ms) >= 1000U)
    {
        last_log_ms = now_ms;
        char line[160];
        const int length = snprintf(line, sizeof(line),
            "$UDP_RX,VALID=%" PRIu32 ",BAD=%" PRIu32 ",FOREIGN=%" PRIu32
            ",ACK_OK=%" PRIu32 ",ACK_ERR=%" PRIu32 ",INIT_ERR=%" PRIu32 "\r\n",
            valid,bad,foreign,ack_ok,ack_errors,init_errors);
        emit(line,length);
    }
}
