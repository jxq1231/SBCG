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

static void Put32(uint8_t *p, uint32_t n)
{ p[0]=(uint8_t)(n>>24); p[1]=(uint8_t)(n>>16); p[2]=(uint8_t)(n>>8); p[3]=(uint8_t)n; }

/* Raw API callback is invoked by MX_LWIP_Process in the main loop, not ISR. */
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
