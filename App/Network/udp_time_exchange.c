/**
 * @file        udp_time_exchange.c
 * @brief       关联一次UDP时间交换的板端接收、发送硬件时间与PC提交时刻。
 * @details     外包gnetif输入输出函数，记录type=4请求和type=5应答的t2/t3，前台发送type=6报告，再将type=7提交的t1/t4送入观测队列。
 * @note        仅支持NO_SYS同步链；必须在旧PtpRx/PtpTx包装之后安装，重新安装旧包装前先卸载本外层。仅观测，不校准时钟。
 */

#include "udp_time_exchange.h"
#include "udp_config.h"
#include "BSP/bsp_ptp_tx.h"
#include "Sync/sync_observation.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#if NO_SYS != 1
#error "Time exchange requires synchronous NO_SYS input/output delivery"
#endif

#define KEY_BYTES 36U
#define REPORT_BYTES UDP_TIME_EXCHANGE_REPORT_BYTES
#define SUBMIT_BYTES UDP_TIME_EXCHANGE_SUBMIT_BYTES
#define RECORD_COUNT 4U
#define RECORD_TTL_MS SYNC_OBSERVATION_TTL_MS
#define SERVICE_PERIOD_MS 1000U
#define LOG_CAPACITY 256U
#define NSEC_PER_SEC 1000000000U

typedef enum { EMPTY, WAIT_ACK, READY, REPORTED, SUBMITTED, DONE } RecordState;
typedef struct
{
    RecordState state;
    uint8_t key[KEY_BYTES];
    BSP_PtpTime rx, tx;
    uint32_t created_ms;
    uint64_t submitted_t1_ns, submitted_t4_ns;
} Record;
typedef struct
{
    uint32_t req_ok, req_bad, foreign, duplicate, conflict, ack_ok, ack_err, init_err;
} IoStats;
typedef struct
{
    uint32_t rx_ok, rx_invalid, tx_ok, tx_err, unsupported;
    uint32_t report_ok, report_err, expired, overflow;
} PairStats;

extern struct netif gnetif;
static struct udp_pcb *service_pcb;
static netif_input_fn original_input;
static netif_linkoutput_fn original_output;
static UDPTimeExchange_Tick read_tick;
static UDPTimeExchange_Log emit;
static bool installed, configured;
static unsigned int active_callbacks;
static uint8_t uid_wire[12];
static ip_addr_t peer;
static Record records[RECORD_COUNT];
typedef struct
{
    uint32_t accepted, bad, foreign, no_pair, early, duplicate, conflict, queue_full;
} ObservationStats;
static ObservationStats observation;
static IoStats io;
static PairStats pair;
static uint32_t last_attempt_ms, last_log_ms;
static struct
{
    bool available, valid;
    uint8_t key[KEY_BYTES];
    BSP_PtpTime rx;
} context;

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
 * @brief   读取网络大端序64位时间字段。
 * @param   p 至少8字节可读区域，不可为NULL；仅本次借用。
 * @return  解码后的无符号64位值。
 */
static uint64_t Get64(const uint8_t *p)
{ return (uint64_t)Get32(p)<<32U | Get32(p+4); }
/**
 * @brief   将32位协议字段编码为网络大端序。
 * @param   p 至少4字节可写区域，不可为NULL；仅本次写入。
 * @param   n 待编码的无符号协议字段。
 * @return  无返回值。
 */
static void Put32(uint8_t *p,uint32_t n)
{ p[0]=(uint8_t)(n>>24U); p[1]=(uint8_t)(n>>16U); p[2]=(uint8_t)(n>>8U); p[3]=(uint8_t)n; }
/**
 * @brief   检查PTP纳秒子字段是否落在十进制一秒范围内。
 * @param   value 按值传入的秒/纳秒快照。
 * @return  true表示nanoseconds小于1000000000。
 */
static bool TimeValid(BSP_PtpTime value) { return value.nanoseconds<NSEC_PER_SEC; }
/**
 * @brief   按秒及纳秒顺序判断后一个本地快照是否不早于前一个。
 * @param   later 较晚候选的本地PTP快照。
 * @param   earlier 比较基准的同一本地时钟快照。
 * @return  true表示later大于或等于earlier。
 * @note    不处理秒计数回绕；两快照应已分别通过TimeValid。
 */
static bool TimeAtLeast(BSP_PtpTime later,BSP_PtpTime earlier)
{ return later.seconds>earlier.seconds || (later.seconds==earlier.seconds && later.nanoseconds>=earlier.nanoseconds); }
/**
 * @brief   检查36字节规范请求键的封装、序号、UID和非零会话。
 * @param   key 至少36字节只读请求键，不可为NULL，使用type=4规范形式。
 * @return  true表示满足键约束；不校验时间交换结果。
 */
static bool RequestValid(const uint8_t key[KEY_BYTES])
{
    return memcmp(key,"SBCG\1\4\0\44",8)==0 && Get32(key+8)!=0U &&
           memcmp(key+16,uid_wire,sizeof(uid_wire))==0 && (Get32(key+28)!=0U || Get32(key+32)!=0U);
}

/* This selector only qualifies observation; the original LwIP input still
   validates/checksums and owns normal delivery. Ethernet padding is permitted. */
/**
 * @brief   筛选本机与指定PC间的时间请求或应答以建立时间观测关联。
 * @param   packet 借用的以太网pbuf，可为NULL；不释放或改动。
 * @param   netif 网卡指针，仅接受gnetif；不保存指针。
 * @param   outgoing true筛选type=5发送帧，false筛选type=4接收帧。
 * @param   key 至少36字节可写输出，不可为NULL；应答类型会规范化为请求类型。
 * @return  true表示已写入规范type=4键；false时key内容不可用。
 * @note    仅做观测筛选，允许帧尾填充；校验和及正常协议交付仍由原LwIP输入负责。
 */
static bool FrameKey(const struct pbuf *packet,const struct netif *netif,
                     bool outgoing,uint8_t key[KEY_BYTES])
{
    uint8_t header[34],udp[8U+KEY_BYTES];
    if(netif!=&gnetif || packet==NULL || packet->tot_len<78U ||
       pbuf_copy_partial(packet,header,sizeof(header),0)!=sizeof(header)) { return false; }
    const uint8_t *ip=header+14;
    const uint32_t ip_header_bytes=(ip[0]&15U)*4U;
    const uint8_t pc[4]={UDP_HEARTBEAT_DEST_A,UDP_HEARTBEAT_DEST_B,
                         UDP_HEARTBEAT_DEST_C,UDP_HEARTBEAT_DEST_D};
    ip4_addr_t board;
    memcpy(&board.addr,ip+(outgoing ? 12:16),sizeof(board.addr));
    if(Get16(header+12)!=0x0800U || (ip[0]>>4U)!=4U || ip_header_bytes<20U || ip[9]!=17U ||
       (Get16(ip+6)&0xBFFFU)!=0U || Get16(ip+2)!=ip_header_bytes+sizeof(udp) ||
       packet->tot_len<14U+ip_header_bytes+sizeof(udp) ||
       !ip4_addr_cmp(&board,netif_ip4_addr(netif)) || memcmp(ip+(outgoing ? 16:12),pc,4)!=0 ||
       pbuf_copy_partial(packet,udp,sizeof(udp),(u16_t)(14U+ip_header_bytes))!=sizeof(udp) ||
       Get16(udp)!=(outgoing ? UDP_TIME_EXCHANGE_BOARD_PORT:UDP_TIME_EXCHANGE_PC_PORT) ||
       Get16(udp+2)!=(outgoing ? UDP_TIME_EXCHANGE_PC_PORT:UDP_TIME_EXCHANGE_BOARD_PORT) ||
       Get16(udp+4)!=sizeof(udp)) { return false; }
    memcpy(key,udp+8,KEY_BYTES);
    if(outgoing)
    {
        if(key[5]!=5U) { return false; }
        key[5]=4U; /* All records store the canonical REQUEST key. */
    }
    return RequestValid(key);
}

/**
 * @brief   按记录创建年龄回收超时交换槽。
 * @param   now_ms 当前毫秒计数，年龄以uint32_t差值计算。
 * @return  无返回值。
 * @note    WAIT_ACK或READY超时增加配对过期计数；其余非空状态也释放，但不增加该计数。
 */
static void Expire(uint32_t now_ms)
{
    for(unsigned int i=0;i<RECORD_COUNT;++i)
    {
        Record *record=&records[i];
        if(record->state!=EMPTY && (uint32_t)(now_ms-record->created_ms)>=RECORD_TTL_MS)
        {
            if(record->state==WAIT_ACK || record->state==READY) { ++pair.expired; }
            record->state=EMPTY;
        }
    }
}

/**
 * @brief   在同步输入链内临时保存时间请求的RX硬件快照。
 * @param   packet 交给原输入链的pbuf；本包装不释放，所有权遵循原链。
 * @param   netif 输入网卡，沿原调用链借用。
 * @return  原输入函数的LwIP错误码。
 * @note    NO_SYS前台；进入时清除旧上下文，读取RX及当前PTP验证先后，原输入返回后失效。
 */
static err_t Input(struct pbuf *packet,struct netif *netif)
{
    ++active_callbacks;
    context.available=false;
    if(FrameKey(packet,netif,false,context.key))
    {
        BSP_PtpTime after;
        context.valid=BSP_PTP_ReadRx(&context.rx)==BSP_PTP_OK && TimeValid(context.rx);
        if(context.valid)
        {
            context.valid=BSP_PTP_Read(&after)==BSP_PTP_OK && TimeValid(after) && TimeAtLeast(after,context.rx);
        }
        context.available=true;
    }
    const err_t result=original_input(packet,netif);
    /* Never allow the next callback/frame to claim the previous DMA snapshot. */
    context.available=false;
    --active_callbacks;
    return result;
}

/**
 * @brief   在同步链路输出周围获取匹配时间应答的TX硬件快照。
 * @param   netif 有效输出网卡，借用并传给原链。
 * @param   packet 发送pbuf，借用且不释放；只对单段连续帧尝试时间戳。
 * @return  原输出函数的LwIP错误码。
 * @note    匹配WAIT_ACK记录先置DONE；Begin→原输出→End成功且tx不早于rx才置READY，不自动重试。
 */
static err_t Output(struct netif *netif,struct pbuf *packet)
{
    ++active_callbacks;
    uint8_t key[KEY_BYTES];
    Record *record=NULL;
    Expire(read_tick());
    if(FrameKey(packet,netif,true,key))
    {
        for(unsigned int i=0;i<RECORD_COUNT;++i)
        {
            if(records[i].state==WAIT_ACK && memcmp(records[i].key,key,KEY_BYTES)==0)
            { record=&records[i]; break; }
        }
    }
    bool armed=false;
    if(record!=NULL)
    {
        /* Failure always leaves a short-lived tombstone, never a retry job. */
        record->state=DONE;
        if(packet->next!=NULL || packet->len!=packet->tot_len) { ++pair.unsupported; }
        else
        {
            armed=BSP_PTP_TxBegin(packet->payload,packet->len)==BSP_PTP_OK;
            if(!armed) { ++pair.tx_err; }
        }
    }
    /* The legacy wrapper's TxClean may return NOT_READY while our token is
       armed. Its type=3 selector ignores this type=5 frame and does not Begin. */
    const err_t result=original_output(netif,packet);
    if(armed)
    {
        BSP_PtpTime tx;
        const BSP_PtpStatus status=BSP_PTP_TxEnd(&tx);
        if(result==ERR_OK && status==BSP_PTP_OK && TimeValid(tx) && TimeAtLeast(tx,record->rx))
        { record->tx=tx; record->state=READY; ++pair.tx_ok; }
        else { ++pair.tx_err; }
    }
    --active_callbacks;
    return result;
}

/**
 * @brief   复制应用负载到临时pbuf并向配置PC提交UDP包。
 * @param   data 至少length字节的只读输入，不可为NULL；内容复制，不保存指针。
 * @param   length UDP负载字节数。
 * @return  LwIP错误码；ERR_OK仅表示协议栈接受。
 * @note    要求service_pcb已绑定；本函数释放自身pbuf引用，ARP可另持排队引用。
 */
static err_t Send(const uint8_t *data,uint16_t length)
{
    struct pbuf *packet=pbuf_alloc(PBUF_TRANSPORT,length,PBUF_RAM);
    if(packet==NULL) { return ERR_MEM; }
    err_t result=pbuf_take(packet,data,length);
    if(result==ERR_OK) { result=udp_sendto(service_pcb,packet,&peer,UDP_TIME_EXCHANGE_PC_PORT); }
    /* Raw UDP keeps our reference with us; ARP owns any additional queued ref. */
    pbuf_free(packet);
    return result;
}

/* A submit has no DMA timestamp of its own. Only the completed local record
   can supply t2/t3; copying it into the CPU queue transfers no pbuf ownership. */
/**
 * @brief   将type=7提交中的PC四时刻部分与已报告本地记录配对入队。
 * @param   data 至少52字节可写提交缓冲，不可为NULL；原地将报文头规范化为请求键。
 * @param   now_ms 前台当前毫秒计数，用于先清理过期配对。
 * @return  无返回值；各拒绝原因记录在观测计数中。
 * @note    t1/t4为PC软件纳秒时刻，t2/t3仅从已REPORTED板端记录取得；成功复制到CPU队列，无pbuf转移。
 */
static void ReceiveObservation(uint8_t data[SUBMIT_BYTES],uint32_t now_ms)
{
    const uint64_t t1_ns=Get64(data+36),t4_ns=Get64(data+44);
    if(data[6]!=0U || data[7]!=SUBMIT_BYTES)
    { ++observation.bad; return; }
    data[5]=4U; data[7]=KEY_BYTES;
    if(!RequestValid(data) || (uint32_t)(t1_ns/UINT64_C(1000000))!=Get32(data+12))
    { ++observation.bad; return; }
    Expire(now_ms);
    for(unsigned int i=0;i<RECORD_COUNT;++i)
    {
        Record *record=&records[i];
        if(record->state==EMPTY || memcmp(record->key,data,KEY_BYTES)!=0) { continue; }
        if(record->state==SUBMITTED)
        {
            if(record->submitted_t1_ns==t1_ns && record->submitted_t4_ns==t4_ns)
            { ++observation.duplicate; }
            else { ++observation.conflict; }
            return;
        }
        if(record->state==WAIT_ACK || record->state==READY)
        { ++observation.early; return; }
        if(record->state!=REPORTED) { break; }
        SyncObservationInput input={0};
        input.sequence=Get32(data+8); input.client_tick_ms=Get32(data+12);
        for(unsigned int j=0;j<3U;++j) { input.uid_words[j]=Get32(data+16+j*4U); }
        input.session_hi=Get32(data+28); input.session_lo=Get32(data+32);
        input.created_ms=record->created_ms;
        input.exchange=(SyncExchange){t1_ns,
            (uint64_t)record->rx.seconds*NSEC_PER_SEC+record->rx.nanoseconds,
            (uint64_t)record->tx.seconds*NSEC_PER_SEC+record->tx.nanoseconds,t4_ns};
        if(!SyncObservation_Submit(&input)) { ++observation.queue_full; return; }
        record->submitted_t1_ns=t1_ns; record->submitted_t4_ns=t4_ns;
        record->state=SUBMITTED; ++observation.accepted;
        return;
    }
    ++observation.no_pair;
}

/**
 * @brief   识别请求或观测提交，维护配对记录并应答有效请求。
 * @param   packet RAW接收pbuf，可为NULL；非NULL时所有路径释放自身引用。
 * @param   source 源IP，可为NULL并拒收；仅回调期间借用。
 * @param   source_port 源UDP端口，须匹配配置PC端口。
 * @return  无返回值。
 * @note    前台同步输入链调用；请求仅可消费本帧上下文一次，重复和冲突键不会再次应答。
 */
static void ReceivePacket(struct pbuf *packet,const ip_addr_t *source,u16_t source_port)
{
    if(packet==NULL) { return; }
    uint8_t data[SUBMIT_BYTES];
    const u16_t length=packet->tot_len<SUBMIT_BYTES ? packet->tot_len:SUBMIT_BYTES;
    const u16_t copied=pbuf_copy_partial(packet,data,length,0);
    const bool is_submit=copied>=6U && data[5]==7U;
    if(source==NULL || !ip_addr_cmp(source,&peer) || source_port!=UDP_TIME_EXCHANGE_PC_PORT)
    {
        if(is_submit) { ++observation.foreign; } else { ++io.foreign; }
        pbuf_free(packet); return;
    }
    if(is_submit)
    {
        const bool complete=packet->tot_len==SUBMIT_BYTES && copied==SUBMIT_BYTES;
        pbuf_free(packet); context.available=false;
        if(complete) { ReceiveObservation(data,read_tick()); } else { ++observation.bad; }
        return;
    }
    if(packet->tot_len!=KEY_BYTES || copied!=KEY_BYTES || !RequestValid(data))
    { ++io.req_bad; pbuf_free(packet); return; }
    const bool rx_valid=context.available && context.valid && memcmp(context.key,data,KEY_BYTES)==0;
    context.available=false; /* Consume even a mismatched claim. */
    pbuf_free(packet);
    const uint32_t now_ms=read_tick();
    Expire(now_ms);
    Record *slot=NULL;
    for(unsigned int i=0;i<RECORD_COUNT;++i)
    {
        Record *record=&records[i];
        if(record->state==EMPTY) { if(slot==NULL) { slot=record; } continue; }
        /* SESSION + UID + SEQ is identity; a differing tick is a conflict. */
        if(memcmp(record->key+8,data+8,4)==0 && memcmp(record->key+16,data+16,20)==0)
        {
            if(memcmp(record->key,data,KEY_BYTES)==0) { ++io.duplicate; }
            else { ++io.conflict; }
            return;
        }
    }
    ++io.req_ok;
    if(rx_valid) { ++pair.rx_ok; } else { ++pair.rx_invalid; }
    if(slot!=NULL)
    {
        memcpy(slot->key,data,KEY_BYTES); slot->created_ms=now_ms;
        slot->state=rx_valid ? WAIT_ACK:DONE;
        if(rx_valid) { slot->rx=context.rx; }
    }
    else { ++pair.overflow; }
    data[5]=5U;
    if(Send(data,KEY_BYTES)==ERR_OK) { ++io.ack_ok; }
    else { ++io.ack_err; if(slot!=NULL) { slot->state=DONE; } }
}

/**
 * @brief   记录RAW接收回调活动层数并委托报文处理。
 * @param   arg 注册的用户上下文，本实现未使用。
 * @param   pcb 协议栈传入的控制块，本包装未使用且不释放。
 * @param   packet 接收pbuf，所有权交给ReceivePacket处理。
 * @param   source 源IP，仅本回调期间借用，可为NULL。
 * @param   source_port 源UDP端口。
 * @return  无返回值。
 * @note    NO_SYS主循环内同步调用，非ISR；活动计数阻止回调中卸载或前台处理重入。
 */
static void Receive(void *arg,struct udp_pcb *pcb,struct pbuf *packet,
                    const ip_addr_t *source,u16_t source_port)
{
    (void)arg; (void)pcb;
    ++active_callbacks;
    ReceivePacket(packet,source,source_port);
    --active_callbacks;
}

/**
 * @brief   确认外层所有权后卸载包装并清空服务状态。
 * @return  true表示已卸载或本来未安装；false表示回调活动或包装链所有权不符，链保持原样。
 * @note    仅在串行前台调用；成功恢复原input/linkoutput、移除PCB并清空记录，旧PtpRx/PtpTx包装保留。
 */
bool UDPTimeExchange_Deinit(void)
{
    if(active_callbacks!=0U || (installed && (gnetif.input!=Input || gnetif.linkoutput!=Output)))
    { ++io.init_err; return false; }
    if(installed)
    { gnetif.input=original_input; gnetif.linkoutput=original_output; installed=false; }
    if(service_pcb!=NULL) { udp_remove(service_pcb); service_pcb=NULL; }
    memset(records,0,sizeof(records)); context.available=false; configured=false;
    return true;
}

/**
 * @brief   安全卸载旧服务后校验依赖并安装时间交换外层包装。
 * @param   now_ms 初始化毫秒计数，允许首次Process立即尝试绑定。
 * @param   uid_words 至少3个UID字，不可为NULL且不能全零；内容复制到模块。
 * @param   tick 不可为NULL的毫秒读取回调，后续包装执行期间须有效。
 * @param   log 日志回调，可为NULL；非NULL时后续使用期间须有效。
 * @return  true表示包装已安装并配置；false表示卸载失败或参数/原输入输出无效。
 * @note    必须先安装PtpRx/PtpTx；实际UDP PCB由Process懒创建，初始化失败不表示已有包装仍在。
 */
bool UDPTimeExchange_Init(uint32_t now_ms,const uint32_t uid_words[3],
                          UDPTimeExchange_Tick tick,UDPTimeExchange_Log log)
{
    if(!UDPTimeExchange_Deinit()) { return false; }
    emit=log; read_tick=tick; last_log_ms=now_ms; last_attempt_ms=now_ms-SERVICE_PERIOD_MS;
    memset(&io,0,sizeof(io)); memset(&pair,0,sizeof(pair));
    memset(&observation,0,sizeof(observation));
    if(uid_words==NULL || (uid_words[0]==0U && uid_words[1]==0U && uid_words[2]==0U) ||
       tick==NULL || gnetif.input==NULL || gnetif.linkoutput==NULL ||
       gnetif.input==Input || gnetif.linkoutput==Output)
    { ++io.init_err; return false; }
    for(unsigned int i=0;i<3U;++i) { Put32(uid_wire+i*4U,uid_words[i]); }
    IP_ADDR4(&peer,UDP_HEARTBEAT_DEST_A,UDP_HEARTBEAT_DEST_B,UDP_HEARTBEAT_DEST_C,UDP_HEARTBEAT_DEST_D);
    original_input=gnetif.input; original_output=gnetif.linkoutput;
    gnetif.input=Input; gnetif.linkoutput=Output; installed=configured=true;
    return true;
}

/**
 * @brief   按一秒间隔格式化并输出IO、配对和观测提交统计。
 * @param   now_ms 当前毫秒计数，按无符号差值节流。
 * @return  无返回值。
 * @note    回调须同步消费或复制局部日志缓冲；格式化失败或截断时该行不提交。
 */
static void LogStats(uint32_t now_ms)
{
    if(emit==NULL || (uint32_t)(now_ms-last_log_ms)<SERVICE_PERIOD_MS) { return; }
    last_log_ms=now_ms;
    /* Maximal 32-bit counters: literals plus eight/nine 10-digit fields. */
    _Static_assert(sizeof("$TS_IO,REQ_OK=,REQ_BAD=,FOREIGN=,DUPLICATE=,CONFLICT=,ACK_OK=,ACK_ERR=,INIT_ERR=\r\n")+8U*10U<=LOG_CAPACITY,
                   "IO statistics exceed log buffer");
    _Static_assert(sizeof("$TS_PAIR,RX_OK=,RX_INVALID=,TX_OK=,TX_ERR=,UNSUPPORTED=,REPORT_OK=,REPORT_ERR=,EXPIRED=,OVERFLOW=\r\n")+9U*10U<=LOG_CAPACITY,
                   "Pair statistics exceed log buffer");
    char line[LOG_CAPACITY];
    int length=snprintf(line,sizeof(line),
        "$TS_IO,REQ_OK=%" PRIu32 ",REQ_BAD=%" PRIu32 ",FOREIGN=%" PRIu32
        ",DUPLICATE=%" PRIu32 ",CONFLICT=%" PRIu32 ",ACK_OK=%" PRIu32 ",ACK_ERR=%" PRIu32 ",INIT_ERR=%" PRIu32 "\r\n",
        io.req_ok,io.req_bad,io.foreign,io.duplicate,io.conflict,io.ack_ok,io.ack_err,io.init_err);
    if(length>0 && (size_t)length<sizeof(line)) { emit(line,length); }
    length=snprintf(line,sizeof(line),
        "$TS_PAIR,RX_OK=%" PRIu32 ",RX_INVALID=%" PRIu32 ",TX_OK=%" PRIu32 ",TX_ERR=%" PRIu32
        ",UNSUPPORTED=%" PRIu32 ",REPORT_OK=%" PRIu32 ",REPORT_ERR=%" PRIu32 ",EXPIRED=%" PRIu32 ",OVERFLOW=%" PRIu32 "\r\n",
        pair.rx_ok,pair.rx_invalid,pair.tx_ok,pair.tx_err,pair.unsupported,pair.report_ok,pair.report_err,pair.expired,pair.overflow);
    if(length>0 && (size_t)length<sizeof(line)) { emit(line,length); }
    _Static_assert(sizeof("$OBS_RX,ACCEPTED=,BAD=,FOREIGN=,NO_PAIR=,EARLY=,DUPLICATE=,CONFLICT=,QUEUE_FULL=\r\n")+8U*10U<=LOG_CAPACITY,
                   "Observation statistics exceed log buffer");
    length=snprintf(line,sizeof(line),
        "$OBS_RX,ACCEPTED=%" PRIu32 ",BAD=%" PRIu32 ",FOREIGN=%" PRIu32 ",NO_PAIR=%" PRIu32
        ",EARLY=%" PRIu32 ",DUPLICATE=%" PRIu32 ",CONFLICT=%" PRIu32 ",QUEUE_FULL=%" PRIu32 "\r\n",
        observation.accepted,observation.bad,observation.foreign,observation.no_pair,observation.early,
        observation.duplicate,observation.conflict,observation.queue_full);
    if(length>0 && (size_t)length<sizeof(line)) { emit(line,length); }
}

/**
 * @brief   在前台重试绑定、回收过期记录并发送至多一个READY报告。
 * @param   now_ms 前台当前毫秒计数，重试、过期和日志均使用无符号差值。
 * @return  无返回值。
 * @note    回调活动时立即返回；每次至多尝试一条type=6报告且失败不自动重试，随后限速输出统计。
 */
void UDPTimeExchange_Process(uint32_t now_ms)
{
    if(active_callbacks!=0U) { return; }
    if(configured)
    {
        if(service_pcb==NULL && netif_is_up(&gnetif) && netif_is_link_up(&gnetif) &&
           (uint32_t)(now_ms-last_attempt_ms)>=SERVICE_PERIOD_MS)
        {
            last_attempt_ms=now_ms; service_pcb=udp_new();
            if(service_pcb!=NULL)
            {
                if(udp_bind(service_pcb,netif_ip_addr4(&gnetif),UDP_TIME_EXCHANGE_BOARD_PORT)==ERR_OK)
                { udp_bind_netif(service_pcb,&gnetif); udp_recv(service_pcb,Receive,NULL); }
                else { udp_remove(service_pcb); service_pcb=NULL; }
            }
            if(service_pcb==NULL) { ++io.init_err; }
        }
        Expire(now_ms);
        for(unsigned int i=0;service_pcb!=NULL && netif_is_up(&gnetif) &&
            netif_is_link_up(&gnetif) && i<RECORD_COUNT;++i)
        {
            Record *record=&records[i];
            if(record->state!=READY) { continue; }
            uint8_t data[REPORT_BYTES]; memcpy(data,record->key,KEY_BYTES);
            data[5]=6U; data[7]=REPORT_BYTES;
            Put32(data+36,record->rx.seconds); Put32(data+40,record->rx.nanoseconds);
            Put32(data+44,record->tx.seconds); Put32(data+48,record->tx.nanoseconds);
            record->state=DONE;
            if(Send(data,REPORT_BYTES)==ERR_OK) { record->state=REPORTED; ++pair.report_ok; }
            else { ++pair.report_err; }
            break; /* At most one attempt, never an automatic retransmission. */
        }
    }
    LogStats(now_ms);
}
