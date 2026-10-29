#include "Network/udp_probe.h"
#include "Sync/ptp_rx.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct netif gnetif;
static struct udp_pcb pcb;
static struct pbuf rx, rx_tail, tx;
static udp_recv_fn callback;
static void *callback_arg;
static uint32_t uid[3] = {0x001A002D, 0x30365105, 0x36363631};
static unsigned int attempts, removes, rx_frees, tx_frees, sends, logs;
static bool no_pcb, no_memory, short_copy;
static err_t bind_err, copy_err, send_err;
static unsigned char request[28], reply[28];
static char line[160];
static unsigned int observations;
static bool rx_released;

void PtpRx_Observe(const uint8_t data[28])
{
    assert(!rx_released && memcmp(data,request,28)==0);
    ++observations;
}

static void Log(const char *data, int length)
{
    assert(length > 0 && (size_t)length < sizeof(line));
    memcpy(line, data, (size_t)length); line[length] = 0; ++logs;
}
struct udp_pcb *udp_new(void) { ++attempts; return no_pcb ? NULL : &pcb; }
void udp_remove(struct udp_pcb *p) { assert(p == &pcb); ++removes; }
err_t udp_bind(struct udp_pcb *p, const ip_addr_t *address, u16_t port)
{ assert(p == &pcb && address && port == 5006); return bind_err; }
void udp_bind_netif(struct udp_pcb *p, const struct netif *n)
{ assert(p == &pcb && n == &gnetif); }
void udp_recv(struct udp_pcb *p, udp_recv_fn cb, void *arg)
{ assert(p == &pcb); callback = cb; callback_arg = arg; }
struct pbuf *pbuf_alloc(pbuf_layer layer, u16_t size, pbuf_type type)
{ assert(layer == PBUF_TRANSPORT && size == 28 && type == PBUF_RAM); return no_memory ? NULL : &tx; }
u16_t pbuf_copy_partial(const struct pbuf *p, void *dest, u16_t size, u16_t offset)
{ assert(p == &rx && size == 28 && offset == 0); memcpy(dest, request, size); return short_copy ? size-1 : size; }
err_t pbuf_take(struct pbuf *p, const void *data, u16_t size)
{ assert(p == &tx && size == 28); memcpy(reply, data, size); return copy_err; }
u8_t pbuf_free(struct pbuf *p)
{ if (p == &rx) { ++rx_frees; rx_released=true; } else { assert(p == &tx); ++tx_frees; } return 1; }
err_t udp_sendto(struct udp_pcb *p, struct pbuf *buf, const ip_addr_t *address, u16_t port)
{ assert(p == &pcb && buf == &tx && address && port == 6006); ++sends; return send_err; }

static void Reset(void)
{
    no_pcb = no_memory = short_copy = false;
    bind_err = copy_err = send_err = ERR_OK;
    memset(&gnetif, 0, sizeof(gnetif));
    gnetif.flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP;
    UDPProbe_Init(0, uid, Log);
    attempts = removes = rx_frees = tx_frees = sends = logs = 0;
    observations=0; rx_released=false;
    callback = NULL;
    const unsigned char wire[28] = {
        'S','B','C','G',1,2,0,28,0,0,0,7,0,0,3,232,
        0,0x1A,0,0x2D,0x30,0x36,0x51,5,0x36,0x36,0x36,0x31};
    memcpy(request, wire, sizeof(request));
    memset(&rx, 0, sizeof(rx)); rx.tot_len = 28;
}
static void Receive(u16_t port, bool foreign)
{
    ip_addr_t address;
    IP_ADDR4(&address, 192,168,10,foreign ? 101 : 100);
    assert(callback != NULL);
    rx_released=false;
    callback(callback_arg, &pcb, &rx, &address, port);
}
int main(void)
{
    Reset(); UDPProbe_Process(0);
    assert(callback && attempts == 1);
    Receive(6006, false);
    assert(observations==1);
    assert(rx_frees == 1 && tx_frees == 1 && sends == 1);
    assert(memcmp(reply, "SBCG\1\3\0\34\0\0\0\7", 12) == 0);
    assert(memcmp(reply+12,request+12,4) == 0);
    assert(memcmp(reply+16, request+16, 12) == 0);
    FILE *wire = fopen("Logs/M1_3_UDP/test-ack.bin", "wb");
    assert(wire != NULL && fwrite(reply,1,28,wire) == 28);
    assert(fclose(wire) == 0);
    assert(logs == 0); UDPProbe_Process(1000);
    assert(strstr(line,"VALID=1") && strstr(line,"ACK_OK=1") && logs == 1);
    puts("PASS: valid request echoes sequence/UID/tick and releases both pbuf references");

    Reset(); UDPProbe_Process(0);
    for (unsigned int i = 0; i < 5; ++i)
    {
        unsigned int offset[] = {0,4,5,7,16};
        request[offset[i]] ^= 1;
        Receive(6006,false); request[offset[i]] ^= 1;
    }
    rx.tot_len = 27; Receive(6006,false);
    assert(rx_frees == 6 && sends == 0 && tx_frees == 0);
    assert(observations==0);
    UDPProbe_Process(1000); assert(strstr(line,"BAD=6"));
    puts("PASS: invalid magic/version/type/length/UID are rejected without reply");

    Reset(); UDPProbe_Process(0);
    rx.len = 8; rx.next = &rx_tail; rx_tail.len = rx_tail.tot_len = 20;
    Receive(6006,false); assert(sends == 1 && rx_frees == 1);
    short_copy = true; Receive(6006,false); UDPProbe_Process(1000);
    assert(sends == 1 && rx_frees == 2 && strstr(line,"BAD=1"));
    puts("PASS: chained input uses pbuf copy boundary and short copy is rejected with RX release");

    Reset(); UDPProbe_Process(0); Receive(6006,true); Receive(6007,false);
    assert(rx_frees == 2 && sends == 0);
    assert(observations==0);
    UDPProbe_Process(1000); assert(strstr(line,"FOREIGN=2"));
    puts("PASS: unexpected source IP/port are ignored and RX buffers released");

    Reset(); UDPProbe_Process(0); no_memory = true; Receive(6006,false);
    no_memory = false; copy_err = ERR_MEM; Receive(6006,false);
    copy_err = ERR_OK; send_err = ERR_IF; Receive(6006,false);
    send_err = ERR_OK; Receive(6006,false); UDPProbe_Process(1000);
    assert(rx_frees == 4 && tx_frees == 3 && sends == 2);
    assert(strstr(line,"ACK_OK=1") && strstr(line,"ACK_ERR=3"));
    puts("PASS: allocation/copy/send failures release ownership and next request recovers");

    Reset(); no_pcb = true; UDPProbe_Process(0); UDPProbe_Process(999);
    assert(attempts == 1); no_pcb = false; bind_err = ERR_USE; UDPProbe_Process(1000);
    assert(attempts == 2 && removes == 1);
    bind_err = ERR_OK; UDPProbe_Process(2000);
    assert(attempts == 3 && callback); Receive(6006,false); assert(sends == 1);
    puts("PASS: PCB creation and bind failures retry once per second without leaking PCB");

    Reset(); gnetif.flags = NETIF_FLAG_UP; UDPProbe_Process(0); assert(attempts == 0);
    gnetif.flags |= NETIF_FLAG_LINK_UP; UDPProbe_Process(1); assert(attempts == 1);
    gnetif.flags = NETIF_FLAG_UP; UDPProbe_Process(1000);
    gnetif.flags |= NETIF_FLAG_LINK_UP; UDPProbe_Process(2000); Receive(6006,false);
    assert(attempts == 1 && sends == 1);
    puts("PASS: interface-down initialization is deferred and existing PCB survives link recovery");
    return 0;
}
