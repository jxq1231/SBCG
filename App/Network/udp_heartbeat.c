/**
 * @file        udp_heartbeat.c
 * @brief       通过LwIP RAW UDP周期发送板端心跳。
 * @details     将序号、毫秒计数与三个UID字序列化为28字节大端报文，统计协议栈提交、链路跳过和遗漏时隙。
 * @note        MX_LWIP_Init后在裸机前台调用；发送成功只表示协议栈接受，不证明对端收到。
 */

#include "udp_heartbeat.h"
#include "udp_config.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Generated interface owns ETH configuration, link state and frame DMA. */
extern struct netif gnetif;

static struct udp_pcb *pcb;
static uint32_t last_slot_ms, sequence, board_uid[3];
static uint32_t stack_ok, errors, link_skips, missed_slots;
static UDPHeartbeat_Log write_log;

/**
 * @brief   将32位整数写成四字节网络大端序。
 * @param   dest 至少4字节可写区域，不可为NULL；仅本次写入，不保存指针。
 * @param   value 待编码的无符号整数。
 * @return  无返回值。
 */
static void Put32(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)(value >> 24);
    dest[1] = (uint8_t)(value >> 16);
    dest[2] = (uint8_t)(value >> 8);
    dest[3] = (uint8_t)value;
}

/**
 * @brief   懒创建并绑定UDP控制块，序列化和提交一次心跳。
 * @param   now_ms 写入心跳的板端毫秒计数。
 * @param   result 不可为NULL的输出指针，接收静态结果字符串；调用方不得释放字符串。
 * @return  LwIP错误码；ERR_OK表示提交成功。
 * @note    前台调用；发送pbuf始终由本函数释放自身引用，ARP可能另持引用。
 */
static err_t Send(uint32_t now_ms, const char **result)
{
    ip_addr_t destination;
    IP_ADDR4(&destination, UDP_HEARTBEAT_DEST_A, UDP_HEARTBEAT_DEST_B,
             UDP_HEARTBEAT_DEST_C, UDP_HEARTBEAT_DEST_D);
    if (pcb == NULL)
    {
        pcb = udp_new();
        if (pcb == NULL)
        {
            *result = "PCB_ERROR";
            return ERR_MEM;
        }
        /* Bind to this generated interface, without changing its settings. */
        const err_t bind_err = udp_bind(pcb, netif_ip_addr4(&gnetif),
                                       UDP_HEARTBEAT_LOCAL_PORT);
        if (bind_err != ERR_OK)
        {
            udp_remove(pcb);
            pcb = NULL;
            *result = "BIND_ERROR";
            return bind_err;
        }
        udp_bind_netif(pcb, &gnetif);
    }

    uint8_t data[UDP_HEARTBEAT_PACKET_BYTES] = {'S', 'B', 'C', 'G', 1, 1, 0, 28};
    Put32(data + 8, sequence);
    Put32(data + 12, now_ms);
    for (uint32_t i = 0; i < 3U; ++i)
    {
        Put32(data + 16U + i * 4U, board_uid[i]);
    }
    struct pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, sizeof(data), PBUF_RAM);
    if (packet == NULL)
    {
        *result = "NO_PBUF";
        return ERR_MEM;
    }
    err_t err = pbuf_take(packet, data, sizeof(data));
    if (err == ERR_OK)
    {
        err = udp_sendto(pcb, packet, &destination, UDP_HEARTBEAT_DEST_PORT);
        *result = (err == ERR_OK) ? "OK" : "SEND_ERROR";
    }
    else
    {
        *result = "COPY_ERROR";
    }
    /* udp_sendto does not consume our reference. ARP may retain its own copy
       or reference; normal TX uses the existing synchronous polling path. */
    pbuf_free(packet);
    return err;
}

/**
 * @brief   删除旧UDP控制块并复制UID、复位心跳时隙与计数。
 * @param   now_ms 初始化毫秒计数，作为后续时隙起点。
 * @param   uid_words 不可为NULL，指向至少3个UID字；内容复制到模块，不保存指针。
 * @param   log 日志回调，可为NULL；后续处理期间须有效。
 * @return  无返回值。
 * @note    MX_LWIP_Init之后前台调用；不会立即发送。
 */
void UDPHeartbeat_Init(uint32_t now_ms, const uint32_t uid_words[3], UDPHeartbeat_Log log)
{
    if (pcb != NULL)
    {
        udp_remove(pcb);
        pcb = NULL;
    }
    last_slot_ms = now_ms;
    sequence = stack_ok = errors = link_skips = missed_slots = 0U;
    memcpy(board_uid, uid_words, sizeof(board_uid));
    write_log = log;
}

/**
 * @brief   经过心跳时隙时至多发送一包并输出统计。
 * @param   now_ms 前台当前毫秒计数，按无符号差值处理回绕。
 * @return  无返回值。
 * @note    错过多时隙时序号递增相应数量且不补发；链路断开仅增加跳过计数。
 */
void UDPHeartbeat_Process(uint32_t now_ms)
{
    const uint32_t slots = (uint32_t)(now_ms - last_slot_ms) / UDP_HEARTBEAT_PERIOD_MS;
    if (slots == 0U)
    {
        return;
    }
    last_slot_ms += slots * UDP_HEARTBEAT_PERIOD_MS;
    sequence += slots;
    missed_slots += slots - 1U;
    const char *result = "LINK_DOWN";
    err_t err = ERR_OK;
    if (!netif_is_up(&gnetif) || !netif_is_link_up(&gnetif))
    {
        ++link_skips;
    }
    else
    {
        err = Send(now_ms, &result);
        if (err == ERR_OK) { ++stack_ok; }
        else { ++errors; }
    }
    if (write_log != NULL)
    {
        char line[160];
        const int length = snprintf(line, sizeof(line),
            "$UDP,SEQ=%" PRIu32 ",TICK_MS=%" PRIu32 ",RESULT=%s,ERR=%d,"
            "STACK_OK=%" PRIu32 ",ERRORS=%" PRIu32 ",LINK_SKIPS=%" PRIu32 ",MISSED=%" PRIu32 "\r\n",
            sequence, now_ms, result, (int)err, stack_ok, errors, link_skips, missed_slots);
        write_log(line, length);
    }
}
