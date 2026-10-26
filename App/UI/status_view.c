/**
 * @file status_view.c
 * @brief 将带有效标志和单位的状态副本转换为有界ASCII文本。
 * @details 对2000ms超龄、未知、不可用数值输出占位，不将本地/内部锁写成外部精度通过。
 * @note 不读取HAL或业务模块；文本独立于RGB565/TouchGFX视图，可供后续适配器复用。
 */
#include "status_view.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define STATUS_MAX_AGE_MS 2000U
#define OFFSET_DISPLAY_LIMIT_X2_NS INT64_C(2000000000)
/** @brief 把value转为非NULL且至少21字节的out十进制字符串，无返回值；不依赖nano的64位printf。 */
static void Decimal(uint64_t value,char out[21])
{
 char r[20];unsigned n=0;
 do{r[n++]=(char)('0'+value%10U);value/=10U;}while(value);
 for(unsigned i=0;i<n;++i){out[i]=r[n-i-1U];}
 out[n]=0;
}
/** @brief 将非NULL s在now_ms下格式化至非NULL out；错误清输出且返回false，无硬件副作用。 */
bool UI_StatusFormat(const UI_StatusSnapshot *s,uint32_t now_ms,UI_StatusText *out)
{
 if(out==NULL)return false;
 memset(out,0,sizeof *out);
 if(s==NULL)return false;
 const bool fresh=(uint32_t)(now_ms-s->captured_ms)<STATUS_MAX_AGE_MS;
 (void)snprintf(out->role,sizeof out->role,"%s",s->role==UI_ROLE_A?"BOARD A":s->role==UI_ROLE_B?"BOARD B":"UNKNOWN BOARD");
 const char *network=!fresh?"STALE":s->network==UI_NET_UP?"LINK UP":s->network==UI_NET_DOWN?"NO LINK":"UNKNOWN";
 (void)snprintf(out->network,sizeof out->network,"%s",network);
 const char *sync=!fresh?"STALE":s->sync==UI_SYNC_LOCAL?"LOCAL REFERENCE":s->sync==UI_SYNC_TRACKING?"TRACKING":s->sync==UI_SYNC_INTERNAL?"INTERNAL LOCK":s->sync==UI_SYNC_ACQUIRING?"ACQUIRING":s->sync==UI_SYNC_HOLDOVER?"HOLDOVER":s->sync==UI_SYNC_UNSYNCED?"NOT SYNCED":s->sync==UI_SYNC_FAULT?"FAULT":"UNKNOWN";
 (void)snprintf(out->sync,sizeof out->sync,"%s",sync);
 (void)snprintf(out->precision,sizeof out->precision,"EXT: NOT VERIFIED");
 (void)snprintf(out->offset,sizeof out->offset,"-- US");
 if(fresh && s->offset_valid){
  if(s->offset_x2_ns>OFFSET_DISPLAY_LIMIT_X2_NS || s->offset_x2_ns< -OFFSET_DISPLAY_LIMIT_X2_NS)(void)snprintf(out->offset,sizeof out->offset,"OUT OF RANGE");
  else {const uint32_t magnitude=(uint32_t)(s->offset_x2_ns<0?-s->offset_x2_ns:s->offset_x2_ns);
   (void)snprintf(out->offset,sizeof out->offset,"%s%" PRIu32 ".%03" PRIu32 " US",s->offset_x2_ns<0?"-":"",magnitude/2000U,(magnitude%2000U)/2U);}
 }
 (void)snprintf(out->temperature,sizeof out->temperature,"-- C");
 (void)snprintf(out->temperature_source,sizeof out->temperature_source,"%s",s->temperature_manual?"MANUAL":"AUTO");
 (void)snprintf(out->speed,sizeof out->speed,"-- M/S");
 if(fresh && s->temperature_valid){
  const uint32_t magnitude=s->temperature_milli_c<0?(uint32_t)(-(s->temperature_milli_c+1))+1U:(uint32_t)s->temperature_milli_c;
  (void)snprintf(out->temperature,sizeof out->temperature,"%s%" PRIu32 ".%" PRIu32 " C",s->temperature_milli_c<0?"-":"",magnitude/1000U,(magnitude%1000U)/100U);
  if(s->sound_speed_mm_s!=0U)(void)snprintf(out->speed,sizeof out->speed,"%" PRIu32 ".%" PRIu32 " M/S",s->sound_speed_mm_s/1000U,(s->sound_speed_mm_s%1000U)/100U);
 }
 (void)snprintf(out->audio,sizeof out->audio,"%s",!fresh?"STALE":!s->audio_valid?"NOT VALID":s->audio_running?"RUN":"STOPPED");
 (void)snprintf(out->audio_detail,sizeof out->audio_detail,"DROP -- ERR --");
 if(fresh)(void)snprintf(out->audio_detail,sizeof out->audio_detail,"DROP %" PRIu32 "%s ERR %" PRIu32 "%s",
  s->audio_dropped>9999U?9999U:s->audio_dropped,s->audio_dropped>9999U?"+":"",
  s->audio_errors>9999U?9999U:s->audio_errors,s->audio_errors>9999U?"+":"");
 (void)snprintf(out->counts,sizeof out->counts,"EVENTS --");
 if(fresh && s->audio_valid){
  if(s->candidates>UINT64_C(999999999999))(void)snprintf(out->counts,sizeof out->counts,"EVENTS 999999999999+");
  else {char number[21];Decimal(s->candidates,number);(void)snprintf(out->counts,sizeof out->counts,"EVENTS %s",number);}
 }
 (void)snprintf(out->distance,sizeof out->distance,"-- M");
 if(fresh && s->measurement_valid && s->distance_mm>=0)(void)snprintf(out->distance,sizeof out->distance,"%" PRId32 ".%03" PRId32 " M",s->distance_mm/1000,s->distance_mm%1000);
 (void)snprintf(out->direction,sizeof out->direction,"SIDE --");
 if(fresh && s->measurement_valid)
  (void)snprintf(out->direction,sizeof out->direction,"%s",
   s->measurement_direction==1?"SIDE A":s->measurement_direction==2?"SIDE B":"SIDE --");
 (void)snprintf(out->quality,sizeof out->quality,"QUALITY --");
 if(fresh && s->measurement_valid)
  (void)snprintf(out->quality,sizeof out->quality,"QUALITY %u",(unsigned)s->measurement_quality);
 return true;
}
