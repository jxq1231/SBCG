/**
 * @file        ranging_session.c
 * @brief       RSC2固定24字节编解码和有界权威会话握手实现。
 * @details     A以双方保留代际的最大值加一分配新上下文；仅匹配挑战的CONFIRM放行A。
 *              B重启主动挑战，因此不依赖声音或尚未确认的RST2触发重同步。
 * @note        共同时间仅用于有界新鲜性，不是跨掉电唯一随机数。双方都掉电且TOKEN/
 *              时间线重现时，必须依赖调用方验证的旧包排空界限，不保证抗恶意重放。
 */
#include "Network/ranging_session.h"
#include <stddef.h>
#include <string.h>

enum { CLOSED=0,A_SOLICIT,A_OFFER,B_CHALLENGE,B_CONFIRM,ACTIVE };

/** @brief 读取大端32位。 @param data 非NULL四字节输入。 @return 主机整数。 */
static uint32_t Get32(const uint8_t *data)
{ return (uint32_t)data[0]<<24U|(uint32_t)data[1]<<16U|(uint32_t)data[2]<<8U|data[3]; }

/** @brief 写入大端32位。 @param data 非NULL四字节输出。 @param value 待写整数。 @return 无返回值。 */
static void Put32(uint8_t *data,uint32_t value)
{ data[0]=(uint8_t)(value>>24U);data[1]=(uint8_t)(value>>16U);data[2]=(uint8_t)(value>>8U);data[3]=(uint8_t)value; }

/** @brief 校验报文组合。 @param message 可NULL只读副本。 @return 非零域、合法类型/挑战组合为true。 */
static bool Valid(const RangingSessionMessage *message)
{
    if(message==NULL || message->domain==0U)return false;
    if(message->type==RANGING_SESSION_SOLICIT)return message->challenge_ns==0U;
    if(message->type<RANGING_SESSION_CHALLENGE || message->type>RANGING_SESSION_CONFIRM ||
       message->challenge_ns==0U)return false;
    return message->type==RANGING_SESSION_CHALLENGE || message->authority_epoch!=0U;
}

/** @brief 校验全部CPU字段后逐字段写入大端报文，失败不写输出；参数约定见头文件。 */
bool RangingSessionWire_Encode(const RangingSessionMessage *message,uint8_t *data,uint16_t length)
{
    if(data==NULL || length!=RANGING_SESSION_WIRE_BYTES || !Valid(message))return false;
    memcpy(data,"RSC2",4);data[4]=2U;data[5]=message->type;data[6]=data[7]=0U;
    Put32(data+8,message->domain);Put32(data+12,message->authority_epoch);
    Put32(data+16,(uint32_t)(message->challenge_ns>>32U));
    Put32(data+20,(uint32_t)message->challenge_ns);return true;
}

/** @brief 在局部副本完成全部校验后交付报文，拒绝错误版本或非零保留位；参数约定见头文件。 */
bool RangingSessionWire_Decode(const uint8_t *data,uint16_t length,RangingSessionMessage *out)
{
    if(data==NULL || out==NULL || length!=RANGING_SESSION_WIRE_BYTES ||
       memcmp(data,"RSC2",4)!=0 || data[4]!=2U || data[6]!=0U || data[7]!=0U)return false;
    const RangingSessionMessage message={data[5],Get32(data+8),Get32(data+12),
        (uint64_t)Get32(data+16)<<32U|Get32(data+20)};
    if(!Valid(&message))return false;
    *out=message;return true;
}

/** @brief 撤销当前资格但保留启动内权威水位。 @param self 非NULL实例。 @return 无返回值；通知前台撤销展示。 */
static void Close(RangingSession *self)
{
    if(self->healthy || self->established || self->phase!=CLOSED)self->changed=true;
    self->healthy=false;self->established=false;self->phase=CLOSED;self->have_tx=false;
    self->authority_epoch=0U;self->challenge_ns=0U;
}

/** @brief B创建下一挑战。 @param self 非NULL健康实例。 @param now_ms 本机创建毫秒。 @return 无返回值；尚无值。 */
static void BeginChallenge(RangingSession *self,uint32_t now_ms)
{
    self->established=false;self->authority_epoch=0U;self->phase=B_CHALLENGE;
    self->challenge_ns=self->now_common_ns>self->last_challenge_ns?self->now_common_ns:0U;
    if(self->challenge_ns!=0U)self->last_challenge_ns=self->challenge_ns;
    self->challenge_ms=now_ms;
    self->request_epoch=self->retained_epoch;self->have_tx=false;self->changed=true;
}

/** @brief 检查B挑战相对当前共同时间的有界新鲜性。 @param self 非NULL实例。 @param challenge_ns B共同时间纳秒。 @return 过去不足1500ms或未来最多2ms为true。 */
static bool Fresh(const RangingSession *self,uint64_t challenge_ns)
{
    if(challenge_ns>self->now_common_ns)
        return challenge_ns-self->now_common_ns<=RANGING_SESSION_FUTURE_NS;
    return self->now_common_ns-challenge_ns<
        (uint64_t)RANGING_SESSION_CHALLENGE_MS*UINT64_C(1000000);
}

/** @brief 清零当前启动实例并保存角色，不生成任何硬件或网络身份；参数约定见头文件。 */
void RangingSession_Init(RangingSession *self,BoardRole role)
{
    if(self==NULL)return;
    memset(self,0,sizeof *self);self->role=role;
}

/** @brief 先撤销硬边界，再从独立健康输入进入待握手状态；参数单位和所有权见头文件。 */
void RangingSession_Update(RangingSession *self,bool domain_valid,uint32_t domain,
                           uint32_t local_clock_epoch,bool common_read_ok,
                           uint64_t now_common_ns,uint32_t now_ms)
{
    if(self==NULL)return;
    const bool clock_boundary=self->have_clock && common_read_ok &&
        (self->clock_epoch!=local_clock_epoch || now_common_ns<self->last_common_ns);
    const bool domain_boundary=self->domain!=domain;
    if(clock_boundary)self->last_challenge_ns=0U;
    if(clock_boundary || domain_boundary)Close(self);
    if(common_read_ok && now_common_ns!=0U && local_clock_epoch!=0U) {
        self->last_common_ns=now_common_ns;self->have_clock=true;
    }
    self->clock_epoch=local_clock_epoch;self->now_common_ns=now_common_ns;self->domain=domain;
    if(!domain_valid || domain==0U || !common_read_ok || now_common_ns==0U ||
       local_clock_epoch==0U || clock_boundary || self->exhausted ||
       (self->role!=BOARD_ROLE_A && self->role!=BOARD_ROLE_B)) { Close(self);return; }
    if(!self->healthy) {
        self->healthy=true;self->changed=true;self->have_tx=false;
        if(self->role==BOARD_ROLE_A)self->phase=A_SOLICIT;
        else BeginChallenge(self,now_ms);
    }
    else if(self->phase==B_CHALLENGE && self->challenge_ns==0U)
        BeginChallenge(self,now_ms);
    RangingSession_Tick(self,now_ms);
}

/** @brief 按本机无符号毫秒差推进挑战超时，重传不会重置创建时刻；参数约定见头文件。 */
void RangingSession_Tick(RangingSession *self,uint32_t now_ms)
{
    if(self==NULL || !self->healthy)return;
    if((self->phase==A_OFFER || self->phase==B_CHALLENGE || self->phase==B_CONFIRM) &&
       (uint32_t)(now_ms-self->challenge_ms)>=RANGING_SESSION_CHALLENGE_MS) {
        if(self->phase==A_OFFER) {
            self->phase=A_SOLICIT;self->established=false;self->have_tx=false;self->changed=true;
        } else if(self->phase==B_CHALLENGE)BeginChallenge(self,now_ms);
        else self->phase=ACTIVE;
    }
}

/** @brief 由当前阶段生成一份CPU报文并提前记录尝试时间，避免网络失败忙重试；参数约定见头文件。 */
bool RangingSession_TakeOutgoing(RangingSession *self,uint32_t now_ms,RangingSessionMessage *out)
{
    if(self==NULL || out==NULL)return false;
    RangingSession_Tick(self,now_ms);
    if(!self->healthy || self->exhausted ||
       (self->have_tx && (uint32_t)(now_ms-self->last_tx_ms)<RANGING_SESSION_RETRY_MS))return false;
    RangingSessionMessage message={0};message.domain=self->domain;
    message.authority_epoch=self->authority_epoch;message.challenge_ns=self->challenge_ns;
    switch(self->phase) {
    case A_SOLICIT:
        message.type=RANGING_SESSION_SOLICIT;message.authority_epoch=self->retained_epoch;
        message.challenge_ns=0U;break;
    case A_OFFER:message.type=RANGING_SESSION_OFFER;break;
    case B_CHALLENGE:
        if(self->challenge_ns==0U)return false;
        message.type=RANGING_SESSION_CHALLENGE;message.authority_epoch=self->request_epoch;break;
    case B_CONFIRM:message.type=RANGING_SESSION_CONFIRM;break;
    default:return false;
    }
    self->have_tx=true;self->last_tx_ms=now_ms;*out=message;return true;
}

/** @brief 按域、角色、挑战与精确代际推进三次握手，单侧重启继承存活对端水位；参数约定见头文件。 */
bool RangingSession_OnMessage(RangingSession *self,const RangingSessionMessage *message,uint32_t now_ms)
{
    if(self==NULL || !Valid(message))return false;
    RangingSession_Tick(self,now_ms);
    if(!self->healthy || self->exhausted || message->domain!=self->domain)return false;
    if(self->role==BOARD_ROLE_A) {
        if(message->type==RANGING_SESSION_CHALLENGE) {
            if(!Fresh(self,message->challenge_ns))return false;
            if(message->challenge_ns==self->challenge_ns &&
               message->authority_epoch==self->request_epoch && self->authority_epoch!=0U)
                return self->phase==A_OFFER || self->phase==ACTIVE;
            if(message->challenge_ns<=self->last_challenge_ns)return false;
            const uint32_t highest=self->retained_epoch>message->authority_epoch?
                self->retained_epoch:message->authority_epoch;
            if(highest==UINT32_MAX) { self->exhausted=true;Close(self);return false; }
            self->authority_epoch=highest+1U;self->retained_epoch=self->authority_epoch;
            self->request_epoch=message->authority_epoch;self->challenge_ns=message->challenge_ns;
            self->last_challenge_ns=message->challenge_ns;self->challenge_ms=now_ms;
            self->phase=A_OFFER;self->established=false;self->have_tx=false;self->changed=true;
            return true;
        }
        if(message->type==RANGING_SESSION_CONFIRM &&
           message->authority_epoch==self->authority_epoch && message->challenge_ns==self->challenge_ns &&
           (self->phase==A_OFFER || self->phase==ACTIVE)) {
            if(self->phase==A_OFFER) { self->phase=ACTIVE;self->established=true;self->changed=true; }
            return true;
        }
        return false;
    }
    if(message->type==RANGING_SESSION_SOLICIT) {
        if(self->phase!=B_CHALLENGE && self->phase!=B_CONFIRM)BeginChallenge(self,now_ms);
        return true;
    }
    if(message->type!=RANGING_SESSION_OFFER || message->challenge_ns!=self->challenge_ns ||
       (uint32_t)(now_ms-self->challenge_ms)>=RANGING_SESSION_CHALLENGE_MS ||
       !Fresh(self,message->challenge_ns))return false;
    if((self->phase==B_CONFIRM || self->phase==ACTIVE) && self->established)
        return message->authority_epoch==self->authority_epoch;
    if(self->phase!=B_CHALLENGE || message->authority_epoch<=self->retained_epoch)return false;
    self->authority_epoch=message->authority_epoch;self->retained_epoch=message->authority_epoch;
    self->last_challenge_ns=self->challenge_ns;self->phase=B_CONFIRM;
    self->established=true;self->have_tx=false;self->changed=true;return true;
}

/** @brief 交付无指针资格副本，不读取硬件也不推进协议；参数约定见头文件。 */
void RangingSession_Get(const RangingSession *self,RangingSessionSnapshot *out)
{
    if(out==NULL)return;
    memset(out,0,sizeof *out);
    if(self==NULL)return;
    out->established=self->healthy && self->established && !self->exhausted;
    out->exhausted=self->exhausted;out->domain=self->domain;
    out->authority_epoch=self->authority_epoch;out->source_floor_ns=self->challenge_ns;
}

/** @brief 消费一次撤值/重建通知，多次尚未消费的变化可合并；参数约定见头文件。 */
bool RangingSession_TakeContextChanged(RangingSession *self)
{
    if(self==NULL)return false;
    const bool changed=self->changed;self->changed=false;return changed;
}
