/**
 * @file        test_app_udp_integration.c
 * @brief       双进程真实App/UDP/结果缓存/UI主机驱动。
 * @details     stdin提供受控时钟、检测器标量TOA及网络输入；stdout记录生产报文和只读状态。
 * @note        只替换硬件/检测器/LwIP收发边界，不注入pair或结果，不访问实物。
 */
#define main unused_app_main
#define BSP_Console_Write LegacyConsoleWrite
#define MX_LWIP_Process LegacyLwipProcess
#include "test_app_main.c"
#undef MX_LWIP_Process
#undef BSP_Console_Write
#undef main
#include "BSP/bsp_board_identity.h"
#include "BSP/bsp_ptp.h"
#include "Detector/event_classifier.h"
#include "Detector/sound_event_time.h"
#include "Detector/sound_detector_service.h"
#include "Network/udp_sound_event.h"
#include "Sync/time_domain.h"
#include "UI/status_snapshot.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include <inttypes.h>

#define HOST_QUEUE 64U
#define HOST_BYTES 128U
typedef struct { uint16_t length; uint8_t data[HOST_BYTES]; } HostPacket;
struct netif gnetif;
static struct udp_pcb fake_pcb;
static struct pbuf tx_pbuf;
static uint8_t tx_payload[HOST_BYTES];
static HostPacket packets[HOST_QUEUE];
static unsigned queue_head,queue_count;
static udp_recv_fn receive_callback;
static void *receive_context;
static ip_addr_t peer_ip;
static BoardIdentityProfile identity;
static uint64_t common_ns;
static uint32_t audio_session=1U,clock_epoch=3U;
static uint32_t audio_dropped, audio_processed;
static bool clock_ok=true,sync_faulted,send_ok=true,event_pending,process_blocks=true;
static SoundEventTimeEvent next_event;
static uint32_t event_takes,event_delivered,packet_attempts;
static TemperatureSnapshot host_temperature={false,true,1U,0U,20000,343420U};

/** @brief 输出字节十六进制。 @param data 非NULL输入。 @param length 字节数。 @return 无返回值。 */
static void Hex(const uint8_t *data,size_t length)
{for(size_t i=0U;i<length;++i)(void)printf("%02x",data[i]);}
/** @brief 保存实际串口投影并保留原门控断言。 @param data 非NULL行。 @param length 字节数。 @return 模拟写入结果。 */
bool BSP_Console_Write(const char *data,uint16_t length)
{
    const bool result=LegacyConsoleWrite(data,length);
    (void)printf("{\"type\":\"log\",\"tick_ms\":%" PRIu32 ",\"hex\":\"",tick_ms);
    Hex((const uint8_t*)data,length);puts("\"}");return result;
}
/** @brief 返回独立进程的板身份。 @return 静态只读副本。 */
const BoardIdentityProfile *BSP_BoardIdentity_Get(void){return &identity;}
/** @brief 模拟同步外设卸载。 @return true。 */
bool TimeSync_Deinit(void){return true;}
/** @brief 检查同步边界。 @param now 毫秒。 @param profile 身份。 @param tick 读时接口。 @param log 日志接口。 @return true。 */
bool TimeSync_Init(uint32_t now,const BoardIdentityProfile *profile,uint32_t(*tick)(void),void(*log)(const char*,int))
{assert(now==tick_ms && profile==&identity && tick && log);return true;}
/** @brief 模拟同步轮询。 @param now 毫秒。 @param allowed DHT放行。 @return 无返回值。 */
void TimeSync_Process(uint32_t now,bool allowed){assert(now==tick_ms && allowed==!temperature_low);}
/** @brief 复制受控同步健康。 @param now 毫秒。 @param out 可空输出。 @return 无返回值。 */
void TimeSync_GetSnapshot(uint32_t now,TimeSyncSnapshot *out)
{(void)now;if(out)*out=(TimeSyncSnapshot){.configured=true,.faulted=sync_faulted,
    .offset_valid=!sync_faulted,.internal_locked=!sync_faulted,.role=identity.role,
    .state=sync_faulted?CLOCK_SERVO_FAULT:CLOCK_SERVO_TRACKING};}
/** @brief 读取受控PTP。 @param out 输出时间。 @return OK或NOT_READY。 */
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *out)
{if(!clock_ok || out==NULL)return BSP_PTP_NOT_READY;out->seconds=(uint32_t)(common_ns/UINT64_C(1000000000));out->nanoseconds=(uint32_t)(common_ns%UINT64_C(1000000000));return BSP_PTP_OK;}
/** @brief 返回本机代际，初始3，独立于权威代际。 @return 时钟代际。 */
uint32_t BSP_PTP_GetEpoch(void){return clock_epoch;}
/** @brief 复制网口健康。 @param now 毫秒。 @param out 可空输出。 @return 无返回值。 */
void EthernetMonitor_GetSnapshot(uint32_t now,EthernetMonitorSnapshot *out)
{(void)now;if(out)*out=(EthernetMonitorSnapshot){true,netif_is_link_up(&gnetif)!=0U,0U};}
/** @brief 清检测器边界队列。 @param log 可空日志接口。 @return 无返回值。 */
void SoundEventTime_Init(void(*log)(const char*,int)){(void)log;event_pending=false;}
/** @brief 不生成硬件锚点。 @param now 毫秒。 @param observation 只读观测。 @return 无返回值。 */
void SoundEventTime_Observe(uint32_t now,const AudioTimeAnchorObservation *observation){(void)now;(void)observation;}
/** @brief 标量TOA由主机输入。 @param now 毫秒。 @param snapshot 采集快照。 @return 无返回值。 */
void SoundEventTime_Process(uint32_t now,const BSP_AudioSnapshot *snapshot){(void)now;assert(snapshot!=NULL);}
/** @brief 消费一次TOA标量。 @param out 非NULL输出。 @return 有事件时true。 */
bool SoundEventTime_TakeEvent(SoundEventTimeEvent *out)
{assert(out!=NULL && !temperature_low);++event_takes;if(!event_pending){memset(out,0,sizeof *out);return false;}*out=next_event;event_pending=false;++event_delivered;return true;}
/** @brief 不生成音频锚点。 @param out 可空输出。 @return false。 */
bool AudioTimeAnchor_TakeObservation(AudioTimeAnchorObservation *out){if(out)memset(out,0,sizeof *out);return false;}
/** @brief 不访问PCM。 @param now 毫秒。 @param observation 只读观测。 @return 无返回值。 */
void AudioTimeMapMonitor_Observe(uint32_t now,const AudioTimeAnchorObservation *observation){(void)now;(void)observation;}
/** @brief 提供受控SAI快照；DROP注入累计缺口，PROCESS决定后续STEP是否完成新块。 @param out 可空输出。 @return 无返回值。 */
void BSP_Audio_GetSnapshot(BSP_AudioSnapshot *out)
{if(out)*out=(BSP_AudioSnapshot){.running=true,.session=audio_session,.callback_count=1U,
    .last_callback_ms=tick_ms,.dropped_blocks=audio_dropped,.processed_blocks=audio_processed};}
/** @brief 返回检测计数元数据。 @param out 可空输出。 @return 无返回值。 */
void SoundDetectorService_GetSnapshot(SoundDetectorServiceSnapshot *out)
{if(out)*out=(SoundDetectorServiceSnapshot){.configured=true,.candidates=event_delivered};}
bool SoundDetectorService_GetToaCandidates(uint32_t sequence,
    SoundDetectorServiceToaCandidates *out)
{(void)sequence;if(out)out->count=0U;return false;}
/** @brief 复制温度边界值。 @param now 毫秒。 @param out 可空输出。 @return 无返回值。 */
void TemperatureService_GetSnapshot(uint32_t now,TemperatureSnapshot *out){(void)now;if(out)*out=host_temperature;}
/** @brief 保存真实TEMP解码结果。 @param now 毫秒。 @param temperature_milli_c 毫摄氏度。 @param sound_speed_mm_s 声速。 @param version 版本。 @param manual 手动标志。 @return true。 */
bool TemperatureService_SetRemote(uint32_t now,int32_t temperature_milli_c,uint32_t sound_speed_mm_s,uint32_t version,bool manual)
{(void)now;host_temperature=(TemperatureSnapshot){manual,true,version,0U,temperature_milli_c,sound_speed_mm_s};return true;}

/** @brief 分配PCB边界对象。 @return 静态PCB。 */
struct udp_pcb *udp_new(void){return &fake_pcb;}
/** @brief 取消PCB注册。 @param pcb 本地PCB。 @return 无返回值。 */
void udp_remove(struct udp_pcb *pcb){(void)pcb;receive_callback=NULL;}
/** @brief 检查5009端口绑定。 @param pcb PCB。 @param ip 地址。 @param port 端口。 @return ERR_OK。 */
err_t udp_bind(struct udp_pcb *pcb,const ip_addr_t *ip,u16_t port){(void)pcb;(void)ip;assert(port==5009U);return ERR_OK;}
/** @brief 模拟网口绑定。 @param pcb PCB。 @param netif 网口。 @return 无返回值。 */
void udp_bind_netif(struct udp_pcb *pcb,const struct netif *netif){(void)pcb;(void)netif;}
/** @brief 保存生产接收回调。 @param pcb PCB。 @param callback 接收函数。 @param context 上下文。 @return 无返回值。 */
void udp_recv(struct udp_pcb *pcb,udp_recv_fn callback,void *context){(void)pcb;receive_callback=callback;receive_context=context;}
/** @brief 提供有界发送pbuf。 @param layer 层。 @param length 字节数。 @param type 类型。 @return 静态pbuf。 */
struct pbuf *pbuf_alloc(pbuf_layer layer,u16_t length,pbuf_type type)
{(void)layer;(void)type;assert(length<=HOST_BYTES);memset(&tx_pbuf,0,sizeof tx_pbuf);tx_pbuf.payload=tx_payload;tx_pbuf.len=tx_pbuf.tot_len=length;return &tx_pbuf;}
/** @brief 保存生产codec字节。 @param packet pbuf。 @param data 输入。 @param length 字节数。 @return ERR_OK。 */
err_t pbuf_take(struct pbuf *packet,const void *data,u16_t length){assert(packet==&tx_pbuf && length<=packet->tot_len);memcpy(packet->payload,data,length);return ERR_OK;}
/** @brief 静态pbuf所有权边界。 @param packet 已处理pbuf。 @return 1。 */
u8_t pbuf_free(struct pbuf *packet){(void)packet;return 1U;}
/** @brief 输出真实网络字节和失败尝试。 @param pcb PCB。 @param packet pbuf。 @param destination 对端。 @param port 端口。 @return 受控结果。 */
err_t udp_sendto(struct udp_pcb *pcb,struct pbuf *packet,const ip_addr_t *destination,u16_t port)
{(void)pcb;assert(port==5009U && ip_addr_cmp(destination,&peer_ip));++packet_attempts;
 (void)printf("{\"type\":\"packet\",\"tick_ms\":%" PRIu32 ",\"ok\":%u,\"hex\":\"",tick_ms,send_ok?1U:0U);
 Hex(packet->payload,packet->tot_len);puts("\"}");return send_ok?ERR_OK:ERR_MEM;}
/** @brief 生产接收器读取magic和完整字节。 @param packet pbuf。 @param data 输出。 @param length 字节数。 @param offset 起点。 @return 已复制数。 */
u16_t pbuf_copy_partial(const struct pbuf *packet,void *data,u16_t length,u16_t offset)
{assert(packet!=NULL && (uint32_t)offset+length<=packet->tot_len);memcpy(data,(const uint8_t*)packet->payload+offset,length);return length;}
/** @brief 在App的LwIP阶段交付输入，生产Receive处理所有协议。 @return 无返回值。 */
void MX_LWIP_Process(void)
{LegacyLwipProcess();while(queue_count!=0U && receive_callback!=NULL){
 HostPacket *entry=&packets[queue_head];struct pbuf packet={0};packet.payload=entry->data;packet.len=packet.tot_len=entry->length;
 receive_callback(receive_context,&fake_pcb,&packet,&peer_ip,5009U);queue_head=(queue_head+1U)%HOST_QUEUE;--queue_count;}}

/** @brief 输出生产缓存、域和UI文本；不改变年龄或缓存。 @return 无返回值。 */
static void Snapshot(void)
{
 MeasurementPresentationSnapshot p;MeasurementResult_GetPresentation(&p);TimeDomainSnapshot d;TimeDomain_Get(&d);
 UI_StatusSnapshot s;UI_StatusSnapshot_Read(tick_ms,&s);UI_StatusText text;assert(UI_StatusFormat(&s,tick_ms,&text));
 uint32_t rx_ok=0U,rx_bad=0U;UDPSoundEvent_GetSnapshot(NULL,NULL,NULL,&rx_ok,&rx_bad,NULL);
 (void)printf("{\"type\":\"snapshot\",\"role\":\"%c\",\"tick_ms\":%" PRIu32
 ",\"common_ns\":%" PRIu64 ",\"domain_valid\":%u,\"domain\":%" PRIu32
 ",\"state\":%u,\"id\":%" PRIu32 ",\"rev\":%" PRIu32 ",\"epoch\":%" PRIu32
 ",\"has\":%u,\"fresh\":%u,\"formal\":%u,\"provisional\":%u,\"mm\":%" PRId32
 ",\"direction\":%u,\"source_ns\":%" PRIu64 ",\"fresh_deadline_ns\":%" PRIu64
 ",\"display_deadline_ns\":%" PRIu64 ",\"temp_version\":%" PRIu32
 ",\"distance_text\":\"%s\",\"quality_text\":\"%s\",\"rx_ok\":%" PRIu32
 ",\"rx_bad\":%" PRIu32 ",\"packet_attempts\":%" PRIu32 ",\"event_takes\":%" PRIu32
 ",\"audio_allowed\":%u,\"anchor_allowed\":%u,\"audio_valid\":%u,\"audio_dropped\":%" PRIu32 "}\n",
 identity.role==BOARD_ROLE_A?'A':'B',tick_ms,common_ns,d.valid?1U:0U,d.id,(unsigned)p.state,
 p.measurement_id,p.state_revision,p.authority_clock_epoch,p.has_value?1U:0U,p.fresh?1U:0U,
 p.formal?1U:0U,p.provisional?1U:0U,p.distance_mm,(unsigned)p.direction,p.source_common_ns,
 p.fresh_deadline_ns,p.display_deadline_ns,p.temperature_version,text.distance,text.quality,
 rx_ok,rx_bad,packet_attempts,event_takes,audio_allowed,anchor_allowed,s.audio_valid?1U:0U,s.audio_dropped);
}
/** @brief 排队十六进制网络输入。 @param hex 完整十六进制串。 @return 无返回值。 */
static void Queue(const char *hex)
{const size_t length=strcspn(hex,"\r\n ");assert(length%2U==0U && length/2U<=HOST_BYTES && queue_count<HOST_QUEUE);
 HostPacket *entry=&packets[(queue_head+queue_count)%HOST_QUEUE];entry->length=(uint16_t)(length/2U);
 for(size_t i=0U;i<length/2U;++i){unsigned value=0U;assert(sscanf(hex+2U*i,"%2x",&value)==1);entry->data[i]=(uint8_t)value;}++queue_count;}
/** @brief 一个板角色的独立进程入口。 @param argc 参数数。 @param argv 角色、启动毫秒、共同时间。 @return 正常0，错误非零。 */
int main(int argc,char **argv)
{
 assert(argc==4 && (argv[1][0]=='A' || argv[1][0]=='B'));
 const uint32_t ids[2][3]={{0x001A002DU,0x30365105U,0x36363631U},{0x00380050U,0x35385104U,0x37313532U}};
 memcpy(info_uid,ids[argv[1][0]=='A'?0U:1U],sizeof info_uid);assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&identity));
 IP_ADDR4(&peer_ip,identity.peer_ip[0],identity.peer_ip[1],identity.peer_ip[2],identity.peer_ip[3]);
 IP_ADDR4(&gnetif.ip_addr,identity.local_ip[0],identity.local_ip[1],identity.local_ip[2],identity.local_ip[3]);gnetif.flags=NETIF_FLAG_UP|NETIF_FLAG_LINK_UP;
 common_ns=(uint64_t)strtoull(argv[3],NULL,10);Reset((uint32_t)strtoul(argv[2],NULL,10),true);
 puts("{\"type\":\"done\"}");fflush(stdout);char command[1024];
 while(fgets(command,sizeof command,stdin)!=NULL){
  unsigned value=0U;uint32_t at=0U,seq=0U;uint64_t clock=0U,event_time=0U;
  if(strncmp(command,"STEP ",5U)==0){
   char *cursor=command+5,*end=NULL;
   at=(uint32_t)strtoul(cursor,&end,10);cursor=end;
   clock=(uint64_t)strtoull(cursor,&end,10);cursor=end;
   seq=(uint32_t)strtoul(cursor,&end,10);cursor=end;
   event_time=(uint64_t)strtoull(cursor,&end,10);
   if(end!=cursor){
   tick_ms=at;common_ns=clock;if(process_blocks)++audio_processed;if(seq!=0U){assert(!event_pending);next_event=(SoundEventTimeEvent){.sequence=seq,.session=audio_session,
    .time_ns=event_time,.frame=(uint64_t)seq*96000U,.event_class=EVENT_CLASS_CHIRP,.quality=98U};event_pending=true;}App_Process();
   }
  }else if(strncmp(command,"RX ",3U)==0)Queue(command+3);
  else if(strncmp(command,"SNAP",4U)==0)Snapshot();
  else if(sscanf(command,"SYNC %u",&value)==1)sync_faulted=value==0U;
  else if(sscanf(command,"CLOCK %u",&value)==1)clock_ok=value!=0U;
  else if(sscanf(command,"EPOCH %u",&value)==1)clock_epoch=value;
  else if(sscanf(command,"LOW %u",&value)==1)temperature_low=value!=0U;
  else if(sscanf(command,"SESSION %u",&value)==1)audio_session=value;
  else if(sscanf(command,"SEND %u",&value)==1)send_ok=value!=0U;
  else if(sscanf(command,"DROP %u",&value)==1)audio_dropped=value;
  else if(sscanf(command,"PROCESS %u",&value)==1)process_blocks=value!=0U;
  else if(strncmp(command,"REINIT",6U)==0)App_Init();
  else if(sscanf(command,"TEMP %u",&value)==1){host_temperature.temperature_milli_c=(int32_t)value;host_temperature.sound_speed_mm_s=331300U+(606U*value+500U)/1000U;++host_temperature.version;}
  else if(strncmp(command,"QUIT",4U)==0)break;
  else {fprintf(stderr,"unknown command: %s",command);return 2;}
  puts("{\"type\":\"done\"}");fflush(stdout);
 }return 0;
}
