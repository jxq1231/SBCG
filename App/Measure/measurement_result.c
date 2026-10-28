/**
 * @file        measurement_result.c
 * @brief       两板测距唯一权威结果的缓存与有效性处理。
 * @details     Update确认域/时钟/会话/流上下文；A共享临时与确认结果的ID/revision，
 *              B分离原始权威载荷和本地过期展示，声音年龄只由实际共同时间推进。
 * @note        纯前台标量；无PCM/HAL/PTP/网络访问，无动态内存。
 */
#include "measurement_result.h"
#include "sound_pair.h"
#include <stddef.h>
#include <string.h>

static int32_t calibration_offset_mm;
static uint16_t calibration_version;
#define NS_PER_SEC 1000000000U
/* 只由前台读写的诊断副本；与tracker同放CPU-only区，给普通RAM保留堆栈余量。 */
static MeasurementDiagnosticsSnapshot diagnostic
    __attribute__((section(".ItcmState"), aligned(32)));
static uint32_t diagnostic_sequence;
/* A2b CPU-only状态放在ITCM NOLOAD段：不占320KB主RAM，不能被DMA访问。 */
static RangingTracker tracked
    __attribute__((section(".ItcmState"), aligned(32)));
static MeasurementPresentationSnapshot presentation
    __attribute__((section(".ItcmState"), aligned(32)));
static uint32_t presentation_id_counter;
static uint32_t presentation_revision;
/** @brief NOLOAD的纯CPU权威协调状态；初始化必须显式清零，不供DMA访问。 */
typedef struct
{
    MeasurementContext context;
    TemperatureSnapshot temperature;
    MeasurementPresentationSnapshot remote_raw;
    uint64_t now_common_ns;
    uint64_t remote_source_ns;
    uint64_t remote_fresh_ns;
    uint64_t remote_display_ns;
    uint32_t remote_id_highwater;
    bool mode;
    bool have_context;
    bool healthy;
    bool remote_seen;
    bool remote_eligible;
    bool remote_source_revoked;
} AuthorityCache;
static AuthorityCache authority_cache
    __attribute__((section(".ItcmState"), aligned(32)));
static void ClearPresentation(MeasurementPresentationState state,uint32_t now_ms);
static void InvalidatePresentation(uint32_t now_ms);
static bool CorrectedDistance(int64_t delta_ns,uint32_t speed_mm_s,int32_t *raw_mm,
                              int32_t *corrected_mm);
static void FillTrackerDiagnostic(RangingDecision decision);

/**
 * @brief 开始一次诊断决策并复制输入元数据。
 * @param pair 可NULL配对结果；非NULL时只读复制域/session/序号/源时刻/原始距离。
 * @param now_ms 当前HAL毫秒，仅记录，不参与业务判断。
 * @return 无返回值；清空上一次诊断，sequence递增。
 */
static void BeginDiagnostic(const SoundPairResult *pair,uint32_t now_ms)
{
    memset(&diagnostic,0,sizeof diagnostic);
    diagnostic.schema=2U;
    diagnostic.sequence=++diagnostic_sequence;
    diagnostic.now_ms=now_ms;
    diagnostic.status=MEASUREMENT_REJECTED;
    if(pair==NULL)return;
    diagnostic.pair_time_domain=pair->time_domain;
    diagnostic.a_session=pair->a_session;
    diagnostic.a_sequence=pair->a_sequence;
    diagnostic.b_session=pair->b_session;
    diagnostic.b_sequence=pair->b_sequence;
    diagnostic.a_time_ns=pair->a_time_ns;
    diagnostic.b_time_ns=pair->b_time_ns;
    diagnostic.source_common_ns=pair->a_time_ns>pair->b_time_ns?pair->a_time_ns:pair->b_time_ns;
    diagnostic.raw_delta_ns=pair->delta_ns;
    diagnostic.pair_distance_mm=pair->distance_mm;
    diagnostic.quality=pair->quality;
}

/**
 * @brief 初始化全部ITCM NOLOAD权威状态，仅在模块启动时调用。
 * @param now_ms 当前前台毫秒计数，仅记录初始捕获时间。
 * @return 无返回值；显式清零NOLOAD结构并重置ID/revision。
 */
void MeasurementResult_Init(uint32_t now_ms)
{
    (void)now_ms;
    calibration_offset_mm = 0;
    calibration_version = 0U;
    memset(&diagnostic,0,sizeof diagnostic);
    diagnostic.schema=2U;
    diagnostic.status=MEASUREMENT_NONE;
    diagnostic_sequence=0U;
    const RangingTrackerConfig tracker_config=RangingTracker_DefaultConfig();
    RangingTracker_Init(&tracked,&tracker_config);
    memset(&presentation,0,sizeof presentation);
    presentation.state=MEASUREMENT_PRESENTATION_NONE;
    presentation_id_counter=0U;
    presentation_revision=0U;
    memset(&authority_cache,0,sizeof authority_cache);
}

/**
 * @brief 更新本地标定合同，参数变化时撤销当前展示并等待新的独立声音。
 * @param offset_mm 标定偏移，单位mm；发布要求可表示为int16且结果不越量程。
 * @param version 调用方提供的标定版本，原样携带到权威展示。
 * @return 无返回值；B不重算远程结果，A保留消费身份及外层计数器。
 */
void MeasurementResult_SetCalibration(int32_t offset_mm,uint16_t version)
{
    if(calibration_offset_mm!=offset_mm || calibration_version!=version)
    {
        if(authority_cache.mode && authority_cache.context.authority)
        {
            RangingTracker_Invalidate(&tracked,RANGING_INVALIDATE_FAULT,0U);
            ClearPresentation(MEASUREMENT_PRESENTATION_ACQUIRING,0U);
        }
    }
    calibration_offset_mm=offset_mm;
    calibration_version=version;
}

/**
 * @brief 硬撤销跟踪器及展示资格；不能用于普通单点拒绝或静默。
 * @param now_ms 当前前台毫秒，仅记录状态变化。
 * @return 无返回值；B原始接收载荷保留用于拒绝重放，A样本清空。
 */
static void InvalidatePresentation(uint32_t now_ms)
{
    authority_cache.healthy=false;authority_cache.remote_eligible=false;
    authority_cache.remote_source_revoked=true;
    RangingTracker_Invalidate(&tracked,RANGING_INVALIDATE_FAULT,now_ms);
    ClearPresentation(MEASUREMENT_PRESENTATION_FAULT,now_ms);
}

bool MeasurementResult_GetDiagnostics(MeasurementDiagnosticsSnapshot *out)
{
    if(out==NULL)return false;
    *out=diagnostic;
    return true;
}

/**
 * @brief 映射纯跟踪器状态到展示状态。
 * @param state 跟踪器的当前状态。
 * @return 对应展示状态；未知值按FAULT处理。
 */
static MeasurementPresentationState PresentationFromTracker(RangingTrackerState state)
{
    switch(state)
    {
    case RANGING_STATE_TRACKING:return MEASUREMENT_PRESENTATION_TRACKING;
    case RANGING_STATE_HOLD:return MEASUREMENT_PRESENTATION_HOLD;
    case RANGING_STATE_REACQUIRE:return MEASUREMENT_PRESENTATION_REACQUIRE;
    case RANGING_STATE_STALE:return MEASUREMENT_PRESENTATION_STALE;
    case RANGING_STATE_FAULT:return MEASUREMENT_PRESENTATION_FAULT;
    default:return MEASUREMENT_PRESENTATION_ACQUIRING;
    }
}

/**
 * @brief 用当前声速和标定把时间差换算为距离。
 * @param delta_ns B减A时间差，单位ns；由SoundPair的配对窗口界定在±250ms内。
 * @param speed_mm_s 非零声速，单位mm/s。
 * @param raw_mm 非NULL原始毫米输出，失败时不写入。
 * @param corrected_mm 非NULL标定后毫米输出，失败时不写入。
 * @return 偏移可用且标定后距离在0..10000mm时true。
 */
static bool CorrectedDistance(int64_t delta_ns,uint32_t speed_mm_s,int32_t *raw_mm,
                              int32_t *corrected_mm)
{
    if(speed_mm_s==0U ||
       calibration_offset_mm<INT16_MIN || calibration_offset_mm>INT16_MAX)return false;
    const uint64_t magnitude=delta_ns<0?(uint64_t)0-(uint64_t)delta_ns:(uint64_t)delta_ns;
    const uint64_t raw=(magnitude*speed_mm_s+NS_PER_SEC/2U)/NS_PER_SEC;
    if(raw>MEASUREMENT_MAX_DISTANCE_MM)return false;
    const int64_t corrected=(int64_t)raw+calibration_offset_mm;
    if(corrected<0 || corrected>MEASUREMENT_MAX_DISTANCE_MM)return false;
    *raw_mm=(int32_t)raw;*corrected_mm=(int32_t)corrected;return true;
}

/**
 * @brief 逐字段比较权威载荷；本地captured_ms不属于线上的状态身份。
 * @param a 非NULL只读快照。
 * @param b 非NULL只读快照。
 * @return 除接收/捕获毫秒外所有公开载荷字段相等时true。
 */
static bool PresentationEqual(const MeasurementPresentationSnapshot *a,
                               const MeasurementPresentationSnapshot *b)
{
    return a->state==b->state && a->has_value==b->has_value && a->fresh==b->fresh &&
        a->formal==b->formal && a->provisional==b->provisional &&
        a->filtered_delta_ns==b->filtered_delta_ns && a->distance_mm==b->distance_mm &&
        a->direction==b->direction && a->quality==b->quality &&
        a->evidence_kind==b->evidence_kind && a->measurement_id==b->measurement_id &&
        a->state_revision==b->state_revision && a->time_domain==b->time_domain &&
        a->authority_clock_epoch==b->authority_clock_epoch &&
        a->temperature_version==b->temperature_version &&
        a->sound_speed_mm_s==b->sound_speed_mm_s &&
        a->source_common_ns==b->source_common_ns &&
        a->fresh_deadline_ns==b->fresh_deadline_ns &&
        a->display_deadline_ns==b->display_deadline_ns &&
        a->calibration_version==b->calibration_version &&
        a->calibration_offset_mm==b->calibration_offset_mm &&
        a->reason==b->reason && a->detection_version==b->detection_version &&
        a->source_key_crc32==b->source_key_crc32;
}

/**
 * @brief 撤销数值字段，保留来源期限供诊断和防续租判断。
 * @param value 非NULL、调用方持有的可写快照。
 * @param state 撤值后的展示状态。
 * @return 无返回值；无值状态不携带测量ID、方向、距离或delta。
 */
static void RemoveValue(MeasurementPresentationSnapshot *value,
                         MeasurementPresentationState state)
{
    value->state=state;value->has_value=false;value->fresh=false;value->formal=false;
    value->provisional=false;value->distance_mm=0;value->filtered_delta_ns=0;
    value->measurement_id=0U;value->direction=0U;
}

/**
 * @brief 用实际共同时间收紧展示资格；不更改源时间或截止时间。
 * @param value 非NULL可写展示副本，调用方拥有存储。
 * @param now_ns 当前共同时间，单位ns；健康检查由调用方完成。
 * @return 无返回值；到新鲜截止进入HOLD，到硬截止撤值进入STALE。
 */
static void ExpireValue(MeasurementPresentationSnapshot *value,uint64_t now_ns)
{
    const uint64_t age_ms=now_ns>value->source_common_ns && value->source_common_ns!=0U?
        (now_ns-value->source_common_ns)/RANGING_TRACKER_NS_PER_MS:0U;
    value->source_age_ms=age_ms>UINT32_MAX?UINT32_MAX:(uint32_t)age_ms;
    if(!value->has_value)return;
    if(now_ns>=value->display_deadline_ns)
    { RemoveValue(value,MEASUREMENT_PRESENTATION_STALE);return; }
    if(now_ns>=value->fresh_deadline_ns)
    {
        value->fresh=false;value->formal=false;
        if(value->state==MEASUREMENT_PRESENTATION_TRACKING ||
           value->state==MEASUREMENT_PRESENTATION_ACQUIRING)
        { value->state=MEASUREMENT_PRESENTATION_HOLD; }
    }
}

/**
 * @brief 提交A的权威内容；只有实际载荷变化才推进单一外层revision。
 * @param value 按值拥有的下一快照，内部覆盖上下文及标定元数据。
 * @param now_ms 当前前台毫秒，仅在内容变化时记录。
 * @return 无返回值；计数耗尽后撤销本地值并停止新测量。
 */
static void CommitLocal(MeasurementPresentationSnapshot value,uint32_t now_ms)
{
    value.time_domain=authority_cache.context.time_domain;
    value.authority_clock_epoch=authority_cache.context.authority_epoch;
    value.temperature_version=authority_cache.temperature.version;
    value.sound_speed_mm_s=authority_cache.temperature.sound_speed_mm_s;
    if(value.sound_speed_mm_s==0U)value.sound_speed_mm_s=tracked.config.sound_speed_mm_s;
    value.calibration_version=calibration_version;
    value.detection_version=1U;
    value.calibration_offset_mm=(calibration_offset_mm>=INT16_MIN &&
        calibration_offset_mm<=INT16_MAX)?(int16_t)calibration_offset_mm:0;
    value.state_revision=presentation_revision;
    if(PresentationEqual(&presentation,&value))
    { presentation.source_age_ms=value.source_age_ms;return; }
    value.state_revision=++presentation_revision;value.captured_ms=now_ms;
    presentation=value;
}

/**
 * @brief 为一次新的独立源分配外层ID，临时和确认结果共享计数器。
 * @param value 非NULL、调用方持有的待发布快照。
 * @return 无返回值；ID按uint32回绕，正常寿命内不可达。
 */
static void AssignSourceId(MeasurementPresentationSnapshot *value)
{
    value->measurement_id=++presentation_id_counter;
}

/**
 * @brief 发布指定无值状态，保留外层ID和revision计数器代际。
 * @param state 目标展示状态。
 * @param now_ms 当前前台毫秒，仅作内容捕获记录。
 * @return 无返回值；A使用本地权威revision，B不改写远程权威revision。
 */
static void ClearPresentation(MeasurementPresentationState state,uint32_t now_ms)
{
    MeasurementPresentationSnapshot value=presentation;
    RemoveValue(&value,state);
    value.reason=state==MEASUREMENT_PRESENTATION_FAULT?RANGING_INVALIDATE_FAULT:0U;
    if(authority_cache.context.authority)CommitLocal(value,now_ms);
    else { value.captured_ms=now_ms;presentation=value; }
}

/**
 * @brief 判断前台协调者提供的域/会话/时钟/流身份是否完整健康。
 * @param context 仅调用内借用；NULL判无效。
 * @return 所有资格和非零身份齐全时true，不读取硬件。
 */
static bool TrustedContext(const MeasurementContext *context)
{
    return context!=NULL && context->domain_valid && context->stream_valid &&
        context->clock_valid && context->time_domain!=0U &&
        context->authority_epoch!=0U && context->local_clock_epoch!=0U &&
        context->a_session!=0U && context->b_session!=0U;
}

/**
 * @brief 比较影响历史资格的上下文身份，不把健康布尔变化当成新会话。
 * @param a 非NULL、只读的上次协调者上下文。
 * @param b 非NULL、只读的本次协调者上下文。
 * @return 角色、域、两种epoch、会话和源下界均相同时true。
 */
static bool SameContext(const MeasurementContext *a,const MeasurementContext *b)
{
    return a->authority==b->authority && a->time_domain==b->time_domain &&
        a->authority_epoch==b->authority_epoch && a->local_clock_epoch==b->local_clock_epoch &&
        a->a_session==b->a_session && a->b_session==b->b_session &&
        a->source_floor_ns==b->source_floor_ns;
}

/**
 * @brief 从B的原始权威载荷生成本地有效展示；局部过期不改写原始载荷。
 * @param now_ms 当前前台毫秒，仅在本地撤值时记录。
 * @return 无返回值；无资格时FAULT，有资格时以源截止收紧。
 */
static void RefreshRemote(uint32_t now_ms)
{
    if(!authority_cache.healthy || !authority_cache.remote_eligible)
    { ClearPresentation(MEASUREMENT_PRESENTATION_FAULT,now_ms);return; }
    presentation=authority_cache.remote_raw;
    ExpireValue(&presentation,authority_cache.now_common_ns);
}

/**
 * @brief 用可信上下文和真实共同时间推进权威缓存、温补及新鲜度。
 * @param context 调用内借用并复制；NULL或不健康时立即撤值。
 * @param temperature 调用内借用并复制；A要求有效非零声速，B不重算。
 * @param now_ms 当前前台毫秒，仅用于诊断和候选接收间隔。
 * @param now_common_ns 实际共同读时，单位ns；0或同代际倒退撤值。
 * @return 无返回值；只有本函数可以建立健康上下文，pair/RST2不能授予资格。
 */
void MeasurementResult_Update(const MeasurementContext *context,
                               const TemperatureSnapshot *temperature,
                               uint32_t now_ms,uint64_t now_common_ns)
{
    const bool first=!authority_cache.have_context;
    const bool changed=context!=NULL && (first || !SameContext(&authority_cache.context,context));
    const bool new_clock=context!=NULL && (first ||
        context->time_domain!=authority_cache.context.time_domain ||
        context->local_clock_epoch!=authority_cache.context.local_clock_epoch);
    const bool new_authority=context!=NULL && (first ||
        context->authority!=authority_cache.context.authority ||
        context->time_domain!=authority_cache.context.time_domain ||
        context->authority_epoch!=authority_cache.context.authority_epoch);
    authority_cache.mode=true;
    if(context!=NULL) { authority_cache.context=*context;authority_cache.have_context=true; }
    if(new_clock)authority_cache.now_common_ns=0U;
    if(new_authority)
    {
        authority_cache.remote_seen=false;authority_cache.remote_eligible=false;
        authority_cache.remote_source_revoked=false;
        authority_cache.remote_id_highwater=0U;authority_cache.remote_source_ns=0U;
        authority_cache.remote_fresh_ns=authority_cache.remote_display_ns=0U;
        memset(&authority_cache.remote_raw,0,sizeof authority_cache.remote_raw);
    }
    if(changed)
    {
        RangingTracker_Invalidate(&tracked,RANGING_INVALIDATE_DOMAIN,now_ms);
        authority_cache.remote_eligible=false;
        ClearPresentation(MEASUREMENT_PRESENTATION_ACQUIRING,now_ms);
    }
    const bool temp_valid=temperature!=NULL && temperature->valid &&
        temperature->sound_speed_mm_s!=0U;
    const bool clock_valid=now_common_ns!=0U &&
        now_common_ns>=authority_cache.now_common_ns;
    if(!TrustedContext(context) || !clock_valid ||
       (authority_cache.context.authority && !temp_valid))
    {
        authority_cache.healthy=false;authority_cache.remote_eligible=false;
        authority_cache.remote_source_revoked=true;
        RangingTracker_Invalidate(&tracked,RANGING_INVALIDATE_FAULT,now_ms);
        ClearPresentation(MEASUREMENT_PRESENTATION_FAULT,now_ms);return;
    }
    authority_cache.now_common_ns=now_common_ns;authority_cache.healthy=true;
    if(!context->authority) { RefreshRemote(now_ms);return; }
    authority_cache.temperature=*temperature;
    const RangingTrackerContext tracker_context={context->time_domain,context->a_session,
        context->b_session,context->local_clock_epoch,true,true,true};
    RangingTracker_SetContext(&tracked,&tracker_context,now_ms,now_common_ns);
    RangingTracker_SetSoundSpeed(&tracked,temperature->sound_speed_mm_s);
    RangingTracker_Tick(&tracked,now_ms,now_common_ns);
    RangingTrackerSnapshot snapshot;RangingTracker_Get(&tracked,&snapshot);
    MeasurementPresentationSnapshot value=presentation;
    if(snapshot.accepted_count==0U && !value.has_value &&
       value.state==MEASUREMENT_PRESENTATION_FAULT)
    { value.state=MEASUREMENT_PRESENTATION_ACQUIRING; }
    if(snapshot.state==RANGING_STATE_FAULT)
    { ClearPresentation(MEASUREMENT_PRESENTATION_FAULT,now_ms);return; }
    if(snapshot.accepted_count>0U)
    { value.state=PresentationFromTracker(snapshot.state);value.fresh=snapshot.fresh; }
    if(value.has_value)
    {
        int32_t raw_mm,corrected_mm;
        if(!CorrectedDistance(value.filtered_delta_ns,temperature->sound_speed_mm_s,
            &raw_mm,&corrected_mm))
        {
            RangingTracker_Invalidate(&tracked,RANGING_INVALIDATE_FAULT,now_ms);
            ClearPresentation(MEASUREMENT_PRESENTATION_FAULT,now_ms);return;
        }
        value.distance_mm=corrected_mm;
    }
    ExpireValue(&value,now_common_ns);CommitLocal(value,now_ms);
}

/**
 * @brief 将当前支持窗口及本次跟踪决策写入schema2诊断。
 * @param decision 本次真实tracker决策。
 * @return 无返回值；成员按当前声速及标定转换，候选不冒充接受成员。
 */
static void FillTrackerDiagnostic(RangingDecision decision)
{
    diagnostic.schema=2U;diagnostic.tracker_decision=(uint8_t)decision;
    diagnostic.state_revision=presentation.state_revision;
    diagnostic.measurement_id=presentation.measurement_id;
    diagnostic.accepted_source_common_ns=presentation.source_common_ns;
    diagnostic.accepted_count=(uint8_t)tracked.accepted_count;
    diagnostic.candidate_count=(uint8_t)tracked.candidate_count;
    diagnostic.stable_count=(uint8_t)tracked.accepted_count;
    diagnostic.status=presentation.has_value?MEASUREMENT_VALID:MEASUREMENT_NONE;
    diagnostic.published_mm=presentation.distance_mm;
    diagnostic.temperature_version=authority_cache.temperature.version;
    diagnostic.sound_speed_mm_s=authority_cache.temperature.sound_speed_mm_s;
    for(uint32_t i=0U;i<tracked.accepted_count;++i)
    {
        int32_t raw_mm=0,corrected_mm=0;
        diagnostic.accepted_delta_ns[i]=tracked.accepted_ns[i];
        if(CorrectedDistance(tracked.accepted_ns[i],diagnostic.sound_speed_mm_s,
            &raw_mm,&corrected_mm))diagnostic.stable_mm[i]=corrected_mm;
    }
}

/**
 * @brief 将独立配对交给已由Update授权的A跟踪器；单包不能改变上下文。
 * @param pair 调用内借用；必须匹配域/两侧session、源下界和新鲜预算。
 * @param temperature 调用内借用的当前温度，非法时Update硬撤值。
 * @param formal 上游序列门控；当前仅PROXY证据，不授予FORMAL。
 * @param now_ms 实际前台接收毫秒计数。
 * @param now_common_ns 实际共同读时纳秒；不允许用pair源时刻补读时失败。
 * @return 返回真实tracker决策；临时和确认新源共享唯一外层ID计数器。
 */
RangingDecision MeasurementResult_PublishTrackedPair(const SoundPairResult *pair,
                                                     const TemperatureSnapshot *temperature,
                                                     bool formal,uint32_t now_ms,
                                                     uint64_t now_common_ns)
{
    (void)formal;
    if(authority_cache.mode)
    {
        const MeasurementContext trusted=authority_cache.context;
        MeasurementResult_Update(&trusted,temperature,now_ms,now_common_ns);
    }
    BeginDiagnostic(pair,now_ms);
    if(!authority_cache.mode || !authority_cache.healthy || !authority_cache.context.authority)
    { diagnostic.decision=MEASUREMENT_DECISION_REJECT_INVALID;return RANGING_DECISION_REJECT; }
    const uint64_t source_ns=pair==NULL?0U:
        (pair->a_time_ns>pair->b_time_ns?pair->a_time_ns:pair->b_time_ns);
    int32_t raw_mm=0,corrected_mm=0;
    if(pair==NULL || !pair->valid || pair->time_domain!=authority_cache.context.time_domain ||
       pair->a_session!=authority_cache.context.a_session ||
       pair->b_session!=authority_cache.context.b_session ||
       source_ns<authority_cache.context.source_floor_ns ||
       (now_common_ns>=source_ns && now_common_ns-source_ns>=
        (uint64_t)tracked.config.fresh_age_ms*RANGING_TRACKER_NS_PER_MS) ||
       !CorrectedDistance(pair->delta_ns,authority_cache.temperature.sound_speed_mm_s,
           &raw_mm,&corrected_mm))
    { diagnostic.decision=MEASUREMENT_DECISION_REJECT_INVALID;
      FillTrackerDiagnostic(RANGING_DECISION_REJECT);return RANGING_DECISION_REJECT; }
    diagnostic.temperature_distance_mm=(uint32_t)raw_mm;diagnostic.corrected_mm=corrected_mm;
    const RangingObservation observation={pair->time_domain,pair->a_session,pair->a_sequence,
        pair->b_session,pair->b_sequence,pair->a_time_ns,pair->b_time_ns,pair->delta_ns,
        pair->quality,RANGING_EVIDENCE_PROXY,true};
    const RangingDecision decision=RangingTracker_Push(&tracked,&observation,now_ms,now_common_ns);
    RangingTrackerSnapshot snapshot;RangingTracker_Get(&tracked,&snapshot);
    MeasurementPresentationSnapshot value=presentation;
    if(snapshot.has_value)
    {
        value.state=PresentationFromTracker(snapshot.state);value.has_value=true;
        value.fresh=snapshot.fresh;value.formal=false;value.provisional=true;
        value.filtered_delta_ns=snapshot.filtered_delta_ns;
        if(!CorrectedDistance(snapshot.filtered_delta_ns,authority_cache.temperature.sound_speed_mm_s,
            &raw_mm,&corrected_mm))
        { InvalidatePresentation(now_ms);return RANGING_DECISION_FAULT; }
        value.distance_mm=corrected_mm;value.direction=(uint8_t)snapshot.direction;
        value.quality=snapshot.quality;value.evidence_kind=snapshot.evidence_kind;
        value.source_common_ns=snapshot.source_common_ns;
        value.fresh_deadline_ns=snapshot.fresh_deadline_ns;
        value.display_deadline_ns=snapshot.display_deadline_ns;
        if(decision==RANGING_DECISION_ACCEPT)AssignSourceId(&value);
    }
    else if(decision==RANGING_DECISION_CANDIDATE && pair->quality>=90U)
    {
        value=(MeasurementPresentationSnapshot){0};
        value.state=MEASUREMENT_PRESENTATION_ACQUIRING;value.has_value=value.fresh=true;
        value.provisional=true;value.filtered_delta_ns=pair->delta_ns;value.distance_mm=corrected_mm;
        value.direction=pair->delta_ns>0?1U:(pair->delta_ns<0?2U:0U);
        value.quality=pair->quality;value.evidence_kind=RANGING_EVIDENCE_PROXY;
        value.source_common_ns=source_ns;
        value.fresh_deadline_ns=source_ns+(uint64_t)tracked.config.fresh_age_ms*RANGING_TRACKER_NS_PER_MS;
        value.display_deadline_ns=source_ns+(uint64_t)tracked.config.display_max_age_ms*RANGING_TRACKER_NS_PER_MS;
        AssignSourceId(&value);
    }
    else if(snapshot.state==RANGING_STATE_FAULT)
    { RemoveValue(&value,MEASUREMENT_PRESENTATION_FAULT); }
    ExpireValue(&value,now_common_ns);CommitLocal(value,now_ms);
    diagnostic.decision=decision==RANGING_DECISION_ACCEPT?MEASUREMENT_DECISION_ACCEPT:
        ((decision==RANGING_DECISION_HOLD || decision==RANGING_DECISION_REACQUIRE)?
         MEASUREMENT_DECISION_REJECT_OUTLIER:MEASUREMENT_DECISION_REJECT_INVALID);
    FillTrackerDiagnostic(decision);return decision;
}

/**
 * @brief 复制前台已经按真实共同时间推进的有效展示副本。
 * @param out 调用方拥有的输出存储；NULL不操作。
 * @return 无返回值；本函数不采样硬件、不刷新年龄、不改变revision。
 */
void MeasurementResult_GetPresentation(MeasurementPresentationSnapshot *out)
{ if(out!=NULL)*out=presentation; }

/**
 * @brief 检查远程展示载荷的状态、标志、期限和数值边界。
 * @param remote 调用内借用的解码结果；NULL判非法。
 * @return 载荷组合有效时true；不依赖结构体padding或本地温补。
 */
static bool RemoteValid(const MeasurementPresentationSnapshot *remote)
{
    if(remote==NULL || remote->state>MEASUREMENT_PRESENTATION_FAULT ||
       remote->state_revision==0U || remote->quality>100U || remote->direction>2U ||
       remote->evidence_kind>RANGING_EVIDENCE_CODED || remote->sound_speed_mm_s==0U ||
       (remote->formal && (!remote->has_value || !remote->fresh || remote->provisional ||
        remote->evidence_kind==RANGING_EVIDENCE_PROXY)))return false;
    if(!remote->has_value)return !remote->fresh && !remote->formal &&
        remote->measurement_id==0U && remote->distance_mm==0 && remote->filtered_delta_ns==0 &&
        remote->state!=MEASUREMENT_PRESENTATION_TRACKING &&
        remote->state!=MEASUREMENT_PRESENTATION_HOLD && remote->state!=MEASUREMENT_PRESENTATION_REACQUIRE;
    return remote->measurement_id!=0U && remote->source_common_ns!=0U &&
        remote->source_common_ns>=authority_cache.context.source_floor_ns &&
        remote->state!=MEASUREMENT_PRESENTATION_STALE && remote->state!=MEASUREMENT_PRESENTATION_FAULT &&
        remote->state!=MEASUREMENT_PRESENTATION_NONE && remote->distance_mm>=0 &&
        remote->distance_mm<=(int32_t)MEASUREMENT_MAX_DISTANCE_MM &&
        remote->fresh_deadline_ns>remote->source_common_ns &&
        remote->display_deadline_ns>=remote->fresh_deadline_ns &&
        (!remote->fresh || (remote->state==MEASUREMENT_PRESENTATION_ACQUIRING ||
                           remote->state==MEASUREMENT_PRESENTATION_TRACKING)) &&
        (remote->source_common_ns<=authority_cache.now_common_ns ||
         remote->source_common_ns-authority_cache.now_common_ns<=UINT64_C(2000000));
}

/**
 * @brief B缓存同域同权威epoch的原始载荷，再单独计算本地有效展示。
 * @param remote 调用内借用，接受后按值复制；captured_ms不参与同revision比较。
 * @param time_domain 调用方当前共同域，仍须与可信Update上下文完全一致。
 * @param now_ms 本地接收毫秒，仅记录，不延长声音源期限。
 * @return 新载荷或完全相同的重传返回true；冲突、旧revision和续租返回false。
 */
bool MeasurementResult_PublishRemotePresentation(const MeasurementPresentationSnapshot *remote,
                                                 uint32_t time_domain,uint32_t now_ms)
{
    if(!authority_cache.mode || !authority_cache.healthy || authority_cache.context.authority ||
       remote==NULL || time_domain==0U || time_domain!=authority_cache.context.time_domain ||
       remote->time_domain!=time_domain ||
       remote->authority_clock_epoch!=authority_cache.context.authority_epoch || !RemoteValid(remote))return false;
    if(authority_cache.remote_seen)
    {
        if(remote->state_revision<authority_cache.remote_raw.state_revision)return false;
        if(remote->state_revision==authority_cache.remote_raw.state_revision)
            return PresentationEqual(remote,&authority_cache.remote_raw);
    }
    if(remote->has_value)
    {
        if(remote->measurement_id<authority_cache.remote_id_highwater)return false;
        if(remote->measurement_id==authority_cache.remote_id_highwater)
        {
            if(!authority_cache.remote_eligible || authority_cache.remote_source_revoked ||
               remote->source_common_ns!=authority_cache.remote_source_ns ||
               remote->fresh_deadline_ns>authority_cache.remote_fresh_ns ||
               remote->display_deadline_ns>authority_cache.remote_display_ns)return false;
        }
        else if(authority_cache.remote_id_highwater!=0U &&
                remote->source_common_ns<=authority_cache.remote_source_ns)return false;
        authority_cache.remote_id_highwater=remote->measurement_id;
        authority_cache.remote_source_ns=remote->source_common_ns;
        authority_cache.remote_fresh_ns=remote->fresh_deadline_ns;
        authority_cache.remote_display_ns=remote->display_deadline_ns;
        authority_cache.remote_eligible=true;authority_cache.remote_source_revoked=false;
    }
    else { authority_cache.remote_eligible=true;authority_cache.remote_source_revoked=true; }
    authority_cache.remote_raw=*remote;authority_cache.remote_raw.captured_ms=now_ms;
    authority_cache.remote_seen=true;RefreshRemote(now_ms);return true;
}
