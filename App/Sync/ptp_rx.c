/**
 * @file        ptp_rx.c
 * @brief       为已校验的v1 UDP探测请求关联MAC接收描述符时间。
 * @details     包装gnetif.input，在协议栈同步分发前保存匹配帧的RX快照，UDP回调仅可认领一次，记录进入4项CPU队列后前台输出。
 * @note        NO_SYS=1；需先初始化LwIP及PTP时钟；在UDPTimeExchange外层安装前初始化，快照窗口不能跨帧保存。
 */

#include "ptp_rx.h"
#include "BSP/bsp_ptp.h"
#include "Network/udp_config.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if NO_SYS != 1
#error "RX timestamp context requires synchronous NO_SYS input delivery"
#endif

#define RX_QUEUE_CAPACITY 4U
#define RX_LOG_PERIOD_MS 1000U
#define RX_LOG_CAPACITY 160U

typedef struct
{
    uint32_t request_sequence, client_tick_ms;
    BSP_PtpTime time;
} RxRecord;

extern struct netif gnetif;
static netif_input_fn original_input;
static PtpRx_Log emit;
static uint8_t frame_key[UDP_HEARTBEAT_PACKET_BYTES];
static BSP_PtpTime frame_time;
static BSP_PtpStatus frame_status;
static bool available;
static RxRecord queue[RX_QUEUE_CAPACITY];
static uint32_t head, count, last_log_ms;
static uint32_t valid, missing, invalid, overflow, init_errors;

/**
 * @brief   读取网络大端序16位整数。
 * @param   p 至少2字节可读区域，不可为NULL；借用且不保存。
 * @return  解码后的无符号整数。
 */
static uint16_t Get16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] << 8U | p[1]); }
/**
 * @brief   读取网络大端序32位整数。
 * @param   p 至少4字节可读区域，不可为NULL；借用且不保存。
 * @return  解码后的无符号整数。
 */
static uint32_t Get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24U | (uint32_t)p[1]<<16U | (uint32_t)p[2]<<8U | p[3]; }

/* Parse only observation eligibility. The original stack still owns parsing,
   checksums and pbuf lifetime. IP fragments never inherit the last fragment's time. */
/**
 * @brief   筛选可观测的未分片IPv4 UDP v1请求并保存其28字节键。
 * @param   packet 借用的以太网pbuf，可为NULL；不释放或修改报文。
 * @param   netif 有效输入网卡，不可为NULL；只读本机IPv4地址。
 * @return  true表示候选帧满足选择条件，false不建立快照关联。
 * @note    仅做观测资格筛选；协议栈仍负责校验和、完整解析与正常交付。
 */
static bool RequestKey(const struct pbuf *packet, const struct netif *netif)
{
    uint8_t header[34], udp[8U + UDP_HEARTBEAT_PACKET_BYTES];
    if (packet == NULL || packet->tot_len < 70U ||
        pbuf_copy_partial(packet, header, sizeof(header), 0) != sizeof(header)) { return false; }
    const uint8_t *ip = header + 14;
    const uint32_t ip_header_bytes = (ip[0] & 15U) * 4U;
    const uint8_t source[4] = {UDP_HEARTBEAT_DEST_A, UDP_HEARTBEAT_DEST_B,
                              UDP_HEARTBEAT_DEST_C, UDP_HEARTBEAT_DEST_D};
    ip4_addr_t destination;
    memcpy(&destination.addr, ip + 16, sizeof(destination.addr));
    if (Get16(header + 12) != 0x0800U || (ip[0] >> 4U) != 4U || ip_header_bytes < 20U ||
        ip[9] != 17U || (Get16(ip + 6) & 0xBFFFU) != 0U ||
        Get16(ip + 2) != ip_header_bytes + sizeof(udp) ||
        packet->tot_len < 14U + ip_header_bytes + sizeof(udp) ||
        memcmp(ip + 12, source, sizeof(source)) != 0 ||
        !ip4_addr_cmp(&destination, netif_ip4_addr(netif))) { return false; }
    if (pbuf_copy_partial(packet, udp, sizeof(udp), (u16_t)(14U + ip_header_bytes)) != sizeof(udp) ||
        Get16(udp) != UDP_PROBE_PC_PORT || Get16(udp + 2) != UDP_PROBE_BOARD_PORT ||
        Get16(udp + 4) != sizeof(udp) || memcmp(udp + 8, "SBCG\1\2\0\34", 8) != 0)
    {
        return false;
    }
    memcpy(frame_key, udp + 8, sizeof(frame_key));
    return true;
}

/**
 * @brief   在原输入链同步执行期间提供当前请求的RX描述符时间。
 * @param   packet 传给原输入链的pbuf；本包装不释放，所有权沿原输入约定流转。
 * @param   netif 有效输入网卡，不可为NULL；传递给原函数。
 * @return  原netif输入函数返回的LwIP错误码。
 * @note    NO_SYS前台同步回调；调用原输入之前读取快照，返回后清除可认领状态。
 */
static err_t Input(struct pbuf *packet, struct netif *netif)
{
    available = false;
    if (RequestKey(packet, netif))
    {
        frame_status = BSP_PTP_ReadRx(&frame_time);
        if (frame_status == BSP_PTP_OK)
        {
            BSP_PtpTime after;
            frame_status = BSP_PTP_Read(&after);
            if (frame_status == BSP_PTP_OK &&
                (frame_time.seconds > after.seconds ||
                 (frame_time.seconds == after.seconds && frame_time.nanoseconds > after.nanoseconds)))
            {
                frame_status = BSP_PTP_BAD_SNAPSHOT;
            }
        }
        available = true;
    }
    /* NO_SYS ethernet_input invokes the UDP callback before returning. */
    const err_t result = original_input(packet, netif);
    available = false;
    return result;
}

/**
 * @brief   复位RX观测队列与统计，并安装输入包装。
 * @param   now_ms 初始化毫秒计数，用于日志节流。
 * @param   log 日志回调，可为NULL；后续调用期间须有效。
 * @return  无返回值；原输入为空时累计初始化错误且不安装。
 * @note    先完成MX_LWIP_Init及PtpClock_Init；如已装UDPTimeExchange，须先卸载该外层以免形成包装环。
 */
void PtpRx_Init(uint32_t now_ms, PtpRx_Log log)
{
    emit = log;
    head = count = valid = missing = invalid = overflow = init_errors = 0U;
    last_log_ms = now_ms;
    available = false;
    if (gnetif.input != Input) { original_input = gnetif.input; }
    if (original_input == NULL) { ++init_errors; return; }
    gnetif.input = Input;
}

/**
 * @brief   为一个已验证探测请求一次性认领当前RX快照并排队。
 * @param   request 至少28字节的已校验v1请求，可为NULL并按缺失计数；只复制序号、毫秒字段和快照。
 * @return  无返回值；未匹配、无效快照和队列满分别累计统计。
 * @note    由UDPProbe RAW回调在Input返回前调用；无论认领成功与否都会消耗本帧可用标志，不保存请求指针。
 */
void PtpRx_Observe(const uint8_t request[28])
{
    const bool matches = available && request != NULL &&
                         memcmp(frame_key, request, sizeof(frame_key)) == 0;
    available = false; /* A frame can be claimed once, including failed claims. */
    if (!matches) { ++missing; return; }
    if (frame_status != BSP_PTP_OK) { ++invalid; return; }
    ++valid;
    if (count == RX_QUEUE_CAPACITY) { ++overflow; return; }
    queue[(head + count) % RX_QUEUE_CAPACITY] =
        (RxRecord){Get32(request + 8), Get32(request + 12), frame_time};
    ++count;
}

/**
 * @brief   按一秒节流输出至多一条RX队列记录及累计统计。
 * @param   now_ms 前台当前毫秒计数，按无符号差值处理回绕。
 * @return  无返回值。
 * @note    主循环调用；日志SYNC=0，仅表示本地DMA时间戳观测。
 */
void PtpRx_Process(uint32_t now_ms)
{
    if (emit == NULL || (uint32_t)(now_ms - last_log_ms) < RX_LOG_PERIOD_MS) { return; }
    last_log_ms = now_ms;
    char line[RX_LOG_CAPACITY];
    if (count != 0U)
    {
        const RxRecord record = queue[head];
        head = (head + 1U) % RX_QUEUE_CAPACITY;
        --count;
        const int length = snprintf(line, sizeof(line),
            "$PTP_RX,REQ_SEQ=%" PRIu32 ",CLIENT_TICK_MS=%" PRIu32
            ",SEC=%" PRIu32 ",NSEC=%" PRIu32 ",SYNC=0,SOURCE=DMA\r\n",
            record.request_sequence, record.client_tick_ms, record.time.seconds, record.time.nanoseconds);
        emit(line, length);
    }
    const int length = snprintf(line, sizeof(line),
        "$PTP_RX_STATS,VALID=%" PRIu32 ",MISSING=%" PRIu32 ",INVALID=%" PRIu32
        ",OVERFLOW=%" PRIu32 ",INIT_ERR=%" PRIu32 "\r\n",
        valid, missing, invalid, overflow, init_errors);
    emit(line, length);
}
