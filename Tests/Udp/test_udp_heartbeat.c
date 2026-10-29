#include "Network/udp_heartbeat.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct netif gnetif;
static struct udp_pcb fake_pcb;
static struct pbuf fake_pbuf;
static unsigned char payload[28];
static unsigned char sent[28];
static uint32_t uid[3] = {0x001A002D, 0x30365105, 0x36363631};
static unsigned int sends, allocs, frees, removes, binds;
static bool fail_new, fail_alloc;
static err_t bind_result, take_result, send_result;
static char log_line[160];
static unsigned int logs;

struct udp_pcb *udp_new(void) { return fail_new ? NULL : &fake_pcb; }
err_t udp_bind(struct udp_pcb *p, const ip_addr_t *ip, u16_t port)
{
    assert(p == &fake_pcb && ip != NULL && port == 5005);
    ++binds;
    return bind_result;
}
void udp_bind_netif(struct udp_pcb *p, const struct netif *n)
{ assert(p == &fake_pcb && n == &gnetif); }
void udp_remove(struct udp_pcb *p) { assert(p == &fake_pcb); ++removes; }
struct pbuf *pbuf_alloc(pbuf_layer layer, u16_t len, pbuf_type type)
{
    assert(layer == PBUF_TRANSPORT && len == 28 && type == PBUF_RAM);
    ++allocs;
    fake_pbuf.payload = payload;
    return fail_alloc ? NULL : &fake_pbuf;
}
err_t pbuf_take(struct pbuf *p, const void *data, u16_t len)
{
    assert(p == &fake_pbuf && len == sizeof(payload));
    memcpy(payload, data, len);
    return take_result;
}
u8_t pbuf_free(struct pbuf *p) { assert(p == &fake_pbuf); ++frees; return 1; }
err_t udp_sendto(struct udp_pcb *p, struct pbuf *buf, const ip_addr_t *dest, u16_t port)
{
    assert(p == &fake_pcb && buf == &fake_pbuf && port == 5005);
    const unsigned char *ip = (const unsigned char *)&dest->addr;
    assert(ip[0] == 192 && ip[1] == 168 && ip[2] == 10 && ip[3] == 100);
    ++sends;
    memcpy(sent, payload, sizeof(sent));
    return send_result;
}
static void Log(const char *line, int len)
{
    assert(len > 0 && (size_t)len < sizeof(log_line));
    memcpy(log_line, line, (size_t)len);
    log_line[len] = '\0';
    ++logs;
}
static uint32_t Read32(const unsigned char *p)
{ return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | p[3]; }
static void Reset(uint32_t now_ms)
{
    fail_new = fail_alloc = false;
    bind_result = take_result = send_result = ERR_OK;
    memset(&gnetif, 0, sizeof(gnetif));
    gnetif.flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP;
    UDPHeartbeat_Init(now_ms, uid, Log);
    sends = allocs = frees = removes = binds = logs = 0;
}
int main(void)
{
    Reset(0);
    UDPHeartbeat_Process(999);
    assert(sends == 0 && logs == 0);
    UDPHeartbeat_Process(1000);
    assert(sends == 1 && frees == 1 && logs == 1);
    assert(memcmp(sent, "SBCG\1\1\0\34", 8) == 0);
    assert(Read32(sent+8) == 1 && Read32(sent+12) == 1000);
    assert(Read32(sent+16) == uid[0] && Read32(sent+20) == uid[1] && Read32(sent+24) == uid[2]);
    FILE *wire = fopen("Logs/M1_2_UDP/test-packet.bin", "wb");
    assert(wire != NULL && fwrite(sent, 1, 28, wire) == 28);
    assert(fclose(wire) == 0);
    UDPHeartbeat_Process(1000);
    assert(sends == 1);
    puts("PASS: one packet per second with exact network-order envelope and UID");

    Reset(UINT32_MAX - 499U);
    UDPHeartbeat_Process(499);
    assert(sends == 0);
    UDPHeartbeat_Process(500);
    assert(sends == 1 && Read32(sent+8) == 1 && Read32(sent+12) == 500);
    puts("PASS: millisecond scheduling survives uint32 wrap");

    Reset(0);
    UDPHeartbeat_Process(5500);
    assert(sends == 1 && Read32(sent+8) == 5 && strstr(log_line,"MISSED=4") != NULL);
    UDPHeartbeat_Process(5501);
    assert(sends == 1);
    UDPHeartbeat_Process(6000);
    assert(sends == 2 && Read32(sent+8) == 6);
    puts("PASS: delayed loop skips slots without burst or phase drift");

    Reset(0);
    gnetif.flags = NETIF_FLAG_UP;
    UDPHeartbeat_Process(1000);
    assert(allocs == 0 && sends == 0 && strstr(log_line,"RESULT=LINK_DOWN"));
    gnetif.flags |= NETIF_FLAG_LINK_UP;
    UDPHeartbeat_Process(2000);
    assert(sends == 1 && Read32(sent+8) == 2);
    puts("PASS: link-down slots create visible sequence gaps and recover");

    Reset(0);
    fail_new = true;
    UDPHeartbeat_Process(1000);
    assert(sends == 0 && strstr(log_line,"RESULT=PCB_ERROR"));
    fail_new = false;
    bind_result = ERR_USE;
    UDPHeartbeat_Process(2000);
    assert(removes == 1 && strstr(log_line,"RESULT=BIND_ERROR"));
    bind_result = ERR_OK;
    UDPHeartbeat_Process(3000);
    assert(sends == 1 && Read32(sent+8) == 3);
    puts("PASS: PCB/bind failures retry once per slot and free failed PCB");

    Reset(0);
    fail_alloc = true;
    UDPHeartbeat_Process(1000);
    assert(allocs == 1 && frees == 0 && sends == 0 && strstr(log_line,"RESULT=NO_PBUF"));
    fail_alloc = false;
    take_result = ERR_MEM;
    UDPHeartbeat_Process(2000);
    assert(frees == 1 && sends == 0 && strstr(log_line,"RESULT=COPY_ERROR"));
    take_result = ERR_OK;
    send_result = ERR_IF;
    UDPHeartbeat_Process(3000);
    assert(frees == 2 && sends == 1 && strstr(log_line,"RESULT=SEND_ERROR"));
    send_result = ERR_OK;
    UDPHeartbeat_Process(4000);
    assert(frees == 3 && sends == 2 && Read32(sent+8) == 4);
    assert(strstr(log_line,"STACK_OK=1,ERRORS=3") != NULL);
    puts("PASS: allocation/copy/send failures release ownership and recover");
    return 0;
}
