/**
 * @file        clap_ranging.c
 * @brief       独立单次击掌的A权威距离与B结果缓存实现。
 * @details     使用真实共同源时刻限制2.5秒新鲜和6.5秒展示；RSC2域、代际和源下界
 *              隔离旧包。结果冻结本次温补，下一次有效温变不改旧值与重传；
 *              结果重传只取当前结果，重复包只触发ACK，不刷新寿命。
 * @note        前台串行调用；不使用标准音跟踪器，不进行硬件初始化或Flash持久化。
 */
#include "clap_ranging.h"
#include <stddef.h>
#include <string.h>

/**
 * @brief 撤展示与待发送，保留当前域消费水位，防止恢复时重复包复活。
 * @param self 非NULL前台实例；不改变健康资格、角色、会话或结果ID水位。
 * @return 无返回值；调用方保证串行访问，撤值后须有新源才能恢复展示。
 */
void ClapRanging_Invalidate(ClapRanging *self)
{
    self->snapshot.has_value=false;
    self->snapshot.fresh=false;
    self->pending_tx=false;
    self->pending_ack=false;
}

/**
 * @brief B采用RST2标定合同；版本/偏移只前进，旧合同不能复活旧结果。
 * @param self 非NULL B前台实例。
 * @param version A当前标定版本。
 * @param offset_mm A当前实际应用偏移。
 * @return 无返回值；合同变化撤销不同合同旧结果，旧RST2忽略。
 */
void ClapRanging_ObserveCalibration(ClapRanging *self,uint16_t version,int16_t offset_mm)
{
    if(self==NULL)return;
    if(self->have_calibration_contract && version<self->calibration_contract_version)return;
    const bool changed=!self->have_calibration_contract ||
        version!=self->calibration_contract_version ||
        offset_mm!=self->calibration_contract_offset_mm;
    self->have_calibration_contract=true;
    self->calibration_contract_version=version;
    self->calibration_contract_offset_mm=offset_mm;
    if(changed && self->snapshot.has_value &&
       (self->snapshot.result.calibration_version!=version ||
        self->snapshot.result.calibration_offset_mm!=offset_mm))
    { ClapRanging_Invalidate(self); }
}

/** @brief 核对源下界、2ms未来容差和6.5s展示上限；self已具备健康资格。 */
static bool SourceValid(const ClapRanging *self,uint64_t source_ns)
{
    return source_ns!=0U && source_ns>=self->session.source_floor_ns &&
        (source_ns<=self->now_common_ns ||
         source_ns-self->now_common_ns<=RANGING_SESSION_FUTURE_NS) &&
        (source_ns>=self->now_common_ns ||
         self->now_common_ns-source_ns<CLAP_RANGING_DISPLAY_NS);
}

/** @brief 逐字段比较已接受结果，避免结构体padding或同键异值报文造成假重复。 */
static bool SameResult(const ClapResultMessage *a,const ClapResultMessage *b)
{
    return a->domain==b->domain && a->authority_epoch==b->authority_epoch &&
        a->id==b->id && a->source_common_ns==b->source_common_ns &&
        a->a_session==b->a_session && a->a_sequence==b->a_sequence &&
        a->b_session==b->b_session && a->b_sequence==b->b_sequence &&
        a->delta_ns==b->delta_ns && a->distance_mm==b->distance_mm &&
        a->sound_speed_mm_s==b->sound_speed_mm_s &&
        a->temperature_version==b->temperature_version &&
        a->temperature_milli_c==b->temperature_milli_c &&
        a->calibration_version==b->calibration_version &&
        a->calibration_offset_mm==b->calibration_offset_mm &&
        a->direction==b->direction && a->quality==b->quality;
}

/** @brief 按实际共同时间更新源年龄；不以本地接收或发送时间延长有效期。 */
static void RefreshAge(ClapRanging *self)
{
    if(!self->snapshot.has_value)return;
    const uint64_t source_ns=self->snapshot.result.source_common_ns;
    const uint64_t age_ns=self->now_common_ns>source_ns?
        self->now_common_ns-source_ns:0U;
    const uint64_t age_ms=age_ns/UINT64_C(1000000);
    self->snapshot.source_age_ms=age_ms>UINT32_MAX?UINT32_MAX:(uint32_t)age_ms;
    self->snapshot.fresh=age_ns<CLAP_RANGING_FRESH_NS;
    if(age_ns>=CLAP_RANGING_DISPLAY_NS)ClapRanging_Invalidate(self);
}

/** @brief 接受一条新结果，更新消费水位与源年龄；调用方已完成入口校验。 */
static void Accept(ClapRanging *self,const ClapResultMessage *message)
{
    self->snapshot.result=*message;
    self->snapshot.has_value=true;
    self->have_source=true;
    self->last_source_ns=message->source_common_ns;
    self->last_id=message->id;
    RefreshAge(self);
}

/** @brief 启动清零实例，无硬件或网络副作用。 */
void ClapRanging_Init(ClapRanging *self)
{ memset(self,0,sizeof *self); }

/**
 * @brief 采用协调者确认的资格和真实读时，失效立即撤值；参数语义见头文件。
 * @param self 非NULL前台实例。
 * @param role A/B板角色。
 * @param healthy 上游域、音频和读时健康结果。
 * @param session 当前RSC2资格，可NULL表示撤资格。
 * @param now_common_ns 当前共同时间纳秒，0或倒退失效。
 * @param now_ms 本机毫秒，仅维持统一调用契约，不作为源期限。
 * @param temperature A温补副本，可NULL；B忽略。
 * @param calibration A标定副本，可NULL表示无偏移；B忽略。
 * @return 无返回值；不保留参数指针，不从失效入口建立资格。
 */
void ClapRanging_Update(ClapRanging *self,BoardRole role,bool healthy,
                        const RangingSessionSnapshot *session,
                        uint64_t now_common_ns,uint32_t now_ms,
                        const TemperatureSnapshot *temperature,
                        const CalibrationSnapshot *calibration)
{
    (void)now_ms;
    const bool qualified=session!=NULL && session->established &&
        session->domain!=0U && session->authority_epoch!=0U && session->source_floor_ns!=0U;
    const bool changed=self->role!=role || (qualified &&
        (self->session.domain!=session->domain ||
         self->session.authority_epoch!=session->authority_epoch ||
         self->session.source_floor_ns!=session->source_floor_ns));
    if(changed)
    {
        ClapRanging_Invalidate(self);
        self->have_source=false;
        self->last_source_ns=0U;
        self->last_id=0U;
        self->have_tx=false;
    }
    self->role=role;
    if(qualified)self->session=*session;
    const bool clock_ok=now_common_ns!=0U &&
        (!self->have_clock || now_common_ns>=self->now_common_ns);
    if(now_common_ns!=0U)
    {
        self->now_common_ns=now_common_ns;
        self->have_clock=true;
    }
    self->healthy=healthy && qualified && clock_ok &&
        (role==BOARD_ROLE_A || role==BOARD_ROLE_B);
    if(role==BOARD_ROLE_A)
    {
        const CalibrationSnapshot none={0};
        const CalibrationSnapshot *cal=calibration!=NULL?calibration:&none;
        const int32_t offset=cal->calibrated && cal->enabled?cal->offset_mm:0;
        const bool temperature_ok=temperature!=NULL && temperature->valid &&
            temperature->version!=0U && temperature->sound_speed_mm_s>0U &&
            temperature->sound_speed_mm_s<=1000000U;
        const bool calibration_ok=offset>=-CALIBRATION_MAX_ABS_OFFSET_MM &&
            offset<=CALIBRATION_MAX_ABS_OFFSET_MM;
        self->temperature_ok=temperature_ok;
        /* 标定合同变化立即撤旧结果；温度失效只停止新发布，不清已冻结的旧结果。 */
        if(self->calibration.version!=cal->version ||
           self->calibration.enabled!=cal->enabled ||
           self->calibration.calibrated!=cal->calibrated ||
           self->calibration.offset_mm!=cal->offset_mm)
        { ClapRanging_Invalidate(self); }
        self->calibration=*cal;
        if(temperature_ok)self->temperature=*temperature;
        if(!calibration_ok)self->healthy=false;
    }
    else self->temperature_ok=true;
    if(!self->healthy)ClapRanging_Invalidate(self);
    else RefreshAge(self);
}

/**
 * @brief A将本次CLAP的真实TDOA按当前声速和启用偏移重算距离，忽略pair旧距离。
 * @param self 非NULL已Update的前台实例。
 * @param pair 非NULL同域CLAP；A/B完整源身份、源时间和delta须自洽。
 * @return 成功true；拒绝不改变当前值、消费水位或重试节奏。
 */
bool ClapRanging_PublishPair(ClapRanging *self,const SoundPairResult *pair)
{
    if(!self->healthy || !self->temperature_ok || self->role!=BOARD_ROLE_A || pair==NULL ||
       !pair->valid || pair->status!=SOUND_PAIR_OK || pair->type!=SOUND_EVENT_CLAP ||
       pair->time_domain!=self->session.domain || pair->quality>100U ||
       pair->a_session==0U || pair->b_session==0U ||
       pair->a_sequence==0U || pair->b_sequence==0U ||
       pair->a_time_ns<self->session.source_floor_ns ||
       pair->b_time_ns<self->session.source_floor_ns || self->next_id==UINT32_MAX)
    { return false; }
    const uint64_t source_ns=pair->a_time_ns>pair->b_time_ns?pair->a_time_ns:pair->b_time_ns;
    const ClapResultMessage *old=&self->snapshot.result;
    if(!SourceValid(self,source_ns) || (self->have_source &&
       (source_ns<=self->last_source_ns ||
        (old->a_session==pair->a_session && pair->a_sequence<=old->a_sequence) ||
        (old->b_session==pair->b_session && pair->b_sequence<=old->b_sequence))))
    { return false; }
    const uint64_t magnitude=pair->b_time_ns>=pair->a_time_ns?
        pair->b_time_ns-pair->a_time_ns:pair->a_time_ns-pair->b_time_ns;
    /* 当前课程10m量程远小于100ms；先限时差，再进行有符号转换与乘法。 */
    if(magnitude>UINT64_C(100000000))return false;
    const int64_t delta=pair->b_time_ns>=pair->a_time_ns?(int64_t)magnitude:-(int64_t)magnitude;
    if(delta!=pair->delta_ns)return false;
    const int32_t offset=self->calibration.calibrated && self->calibration.enabled?
        self->calibration.offset_mm:0;
    const uint64_t raw_mm=(magnitude*self->temperature.sound_speed_mm_s+
        UINT64_C(500000000))/UINT64_C(1000000000);
    const int64_t corrected_mm=(int64_t)raw_mm+offset;
    if(raw_mm>CLAP_RANGING_MAX_DISTANCE_MM || corrected_mm<0 ||
       corrected_mm>CLAP_RANGING_MAX_DISTANCE_MM)return false;
    ClapResultMessage message={0};
    message.domain=self->session.domain;
    message.authority_epoch=self->session.authority_epoch;
    message.id=++self->next_id;
    message.a_session=pair->a_session;message.a_sequence=pair->a_sequence;
    message.b_session=pair->b_session;message.b_sequence=pair->b_sequence;
    message.source_common_ns=source_ns;message.delta_ns=delta;
    message.distance_mm=(int32_t)corrected_mm;
    message.direction=delta>0?SOUND_DIRECTION_A:(delta<0?SOUND_DIRECTION_B:SOUND_DIRECTION_UNKNOWN);
    message.quality=pair->quality;
    message.sound_speed_mm_s=self->temperature.sound_speed_mm_s;
    message.temperature_version=self->temperature.version;
    message.temperature_milli_c=self->temperature.temperature_milli_c;
    message.calibration_version=self->calibration.version;
    message.calibration_offset_mm=(int16_t)offset;
    Accept(self,&message);
    self->pending_tx=true;self->have_tx=false;
    return true;
}

/**
 * @brief B采用当前RSC2的A结果，精确重复仅再ACK，不能恢复已撤销的值。
 * @param self 非NULL已Update前台实例。
 * @param message 非NULL且已由CLP1解码校验的结果，不保留指针。
 * @return 接受新值或精确重复true；旧代际、源下界/源龄和次序不符false。
 */
bool ClapRanging_OnMessage(ClapRanging *self,const ClapResultMessage *message)
{
    if(!self->healthy || self->role!=BOARD_ROLE_B || message==NULL ||
       message->domain!=self->session.domain ||
       message->authority_epoch!=self->session.authority_epoch ||
       !SourceValid(self,message->source_common_ns))return false;
    if(self->have_calibration_contract)
    {
        if(message->calibration_version<self->calibration_contract_version)return false;
        if(message->calibration_version==self->calibration_contract_version &&
           message->calibration_offset_mm!=self->calibration_contract_offset_mm)return false;
    }
    if(self->have_source && message->id==self->last_id)
    {
        if(!SameResult(message,&self->snapshot.result))return false;
    }
    else
    {
        if(message->id==0U || (self->have_source &&
           (message->id<=self->last_id || message->source_common_ns<=self->last_source_ns)))
        { return false; }
        Accept(self,message);
    }
    if(!self->have_calibration_contract ||
       message->calibration_version>self->calibration_contract_version)
    {
        self->have_calibration_contract=true;
        self->calibration_contract_version=message->calibration_version;
        self->calibration_contract_offset_mm=message->calibration_offset_mm;
    }
    self->ack=(ClapAckMessage){message->domain,message->authority_epoch,
        message->id,message->source_common_ns};
    self->pending_ack=true;
    return true;
}

/** @brief A匹配当前待发结果全部四个ACK键；匹配才清重试。 */
bool ClapRanging_OnAck(ClapRanging *self,const ClapAckMessage *ack)
{
    const ClapResultMessage *message=&self->snapshot.result;
    if(!self->healthy || self->role!=BOARD_ROLE_A || !self->pending_tx || ack==NULL ||
       ack->domain!=message->domain || ack->authority_epoch!=message->authority_epoch ||
       ack->id!=message->id || ack->source_common_ns!=message->source_common_ns)return false;
    self->pending_tx=false;
    return true;
}

/** @brief A取待发副本，记录尝试而非发送成功；无符号毫秒差处理回绕。 */
bool ClapRanging_TakeTx(ClapRanging *self,uint32_t now_ms,ClapResultMessage *out)
{
    if(out==NULL || !self->healthy || self->role!=BOARD_ROLE_A ||
       !self->pending_tx || !self->snapshot.has_value ||
       (self->have_tx && (uint32_t)(now_ms-self->last_tx_ms)<CLAP_RANGING_RETRY_MS))return false;
    *out=self->snapshot.result;
    self->last_tx_ms=now_ms;self->have_tx=true;
    return true;
}

/** @brief B消费单个待发ACK；发送失败后由A下次结果重传再次触发。 */
bool ClapRanging_TakeAck(ClapRanging *self,ClapAckMessage *out)
{
    if(out==NULL || !self->healthy || self->role!=BOARD_ROLE_B || !self->pending_ack)return false;
    *out=self->ack;self->pending_ack=false;
    return true;
}

/** @brief 复制展示状态；不刷新读时、源期限或消费状态。 */
void ClapRanging_GetSnapshot(const ClapRanging *self,ClapRangingSnapshot *out)
{ if(out!=NULL)*out=self->snapshot; }
