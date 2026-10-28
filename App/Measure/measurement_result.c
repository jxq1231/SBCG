/**
 * @file        measurement_result.c
 * @brief       两板测距唯一权威结果的缓存与有效性处理。
 * @details     配对结果必须有效且温度/声速当前有效；距离用当前声速重算，超龄/越界不显示数值。
 * @note        纯前台标量；无PCM/HAL/PTP/网络访问，无动态内存。
 */
#include "measurement_result.h"
#include "sound_pair.h"
#include <stddef.h>
#include <string.h>

static MeasurementResultSnapshot latest;
static uint32_t next_id;
static int32_t calibration_offset_mm;
static uint16_t calibration_version;
#define NS_PER_SEC 1000000000U
#define STABLE_WINDOW 5U
#define STABLE_MIN 3U
#define STABLE_MAX_DEVIATION_MM 15
#define STABLE_GAP_RESET_MS 3000U
static int32_t stable_values[STABLE_WINDOW];
static uint8_t stable_count, stable_head;
static uint32_t stable_last_ms;
static uint8_t stable_outliers;
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
static void ClearPresentation(MeasurementPresentationState state,uint32_t now_ms);

/** @brief 复制当前稳定窗口成员到诊断副本；窗口保持原插入顺序，供离线分析。 */
static void FillStableDiagnostic(void)
{
    diagnostic.stable_count=stable_count;
    for(uint8_t i=0U;i<STABLE_WINDOW;++i)diagnostic.stable_mm[i]=stable_values[i];
}

/**
 * @brief 开始一次诊断决策并复制输入元数据。
 * @param pair 可NULL配对结果；非NULL时只读复制域/session/序号/源时刻/原始距离。
 * @param now_ms 当前HAL毫秒，仅记录，不参与业务判断。
 * @return 无返回值；清空上一次诊断，sequence递增。
 */
static void BeginDiagnostic(const SoundPairResult *pair,uint32_t now_ms)
{
    memset(&diagnostic,0,sizeof diagnostic);
    diagnostic.schema=1U;
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

static uint64_t AbsDelta(int64_t value)
{
    return value < 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
}

/** @brief 清空中值滤波历史；初始化、失效或标定变化时调用。 */
static void ResetStable(void)
{
    stable_count=stable_head=0U;stable_last_ms=0U;stable_outliers=0U;
}

/** @brief 返回绝对int32差，输入距离为毫米且非负。 */
static int32_t StableAbs(int32_t a,int32_t b)
{ return a>=b?a-b:b-a; }

/**
 * @brief 对当前有效距离做窗口中值滤波并拒绝孤立离群样本。
 * @param distance_mm 已应用标定的非负距离。
 * @param now_ms 当前毫秒，用于检测跨播放间隙。
 * @return 可发布距离；拒绝本次离群值时返回-1，保持现有结果不变。
 * @note 窗口中值不跨3秒静默/重新初始化；连续3个离群样本后才重新起窗，避免真实间距变化被永久锁死。
 */
static int32_t StabilizeDistance(int32_t distance_mm,uint32_t now_ms)
{
    if(stable_count!=0U && (uint32_t)(now_ms-stable_last_ms)>=STABLE_GAP_RESET_MS)
    { ResetStable(); }
    if(stable_count>=STABLE_MIN)
    {
        int32_t sorted[STABLE_WINDOW];
        for(uint8_t i=0U;i<stable_count;++i)sorted[i]=stable_values[i];
        for(uint8_t i=1U;i<stable_count;++i)
        {
            const int32_t value=sorted[i];uint8_t j=i;
            while(j>0U && sorted[j-1U]>value){sorted[j]=sorted[j-1U];--j;}
            sorted[j]=value;
        }
        const int32_t median=sorted[stable_count/2U];
        if(StableAbs(distance_mm,median)>STABLE_MAX_DEVIATION_MM)
        {
            if(++stable_outliers>=3U){ResetStable();}
            else return -1;
        }
    }
    stable_outliers=0U;
    if(stable_count<STABLE_WINDOW)stable_values[stable_count++]=distance_mm;
    else {stable_values[stable_head]=distance_mm;stable_head=(uint8_t)((stable_head+1U)%STABLE_WINDOW);}
    stable_last_ms=now_ms;
    if(stable_count<STABLE_MIN)return distance_mm;
    int32_t sorted[STABLE_WINDOW];
    for(uint8_t i=0U;i<stable_count;++i)sorted[i]=stable_values[i];
    for(uint8_t i=1U;i<stable_count;++i)
    {
        const int32_t value=sorted[i];uint8_t j=i;
        while(j>0U && sorted[j-1U]>value){sorted[j]=sorted[j-1U];--j;}
        sorted[j]=value;
    }
    return sorted[stable_count/2U];
}

void MeasurementResult_Init(uint32_t now_ms)
{
    memset(&latest, 0, sizeof latest);
    latest.captured_ms = now_ms;
    latest.status = MEASUREMENT_NONE;
    next_id = 0U;
    calibration_offset_mm = 0;
    calibration_version = 0U;
    ResetStable();
    memset(&diagnostic,0,sizeof diagnostic);
    diagnostic.schema=1U;
    diagnostic.status=MEASUREMENT_NONE;
    diagnostic_sequence=0U;
    const RangingTrackerConfig tracker_config=RangingTracker_DefaultConfig();
    RangingTracker_Init(&tracked,&tracker_config);
    memset(&presentation,0,sizeof presentation);
    presentation.state=MEASUREMENT_PRESENTATION_NONE;
    presentation_id_counter=0U;
    presentation_revision=0U;
}

void MeasurementResult_SetCalibration(int32_t offset_mm,uint16_t version)
{
    if(calibration_offset_mm!=offset_mm || calibration_version!=version)
    { ResetStable(); }
    calibration_offset_mm=offset_mm;
    calibration_version=version;
}

bool MeasurementResult_PublishPair(const SoundPairResult *pair,
                                   const TemperatureSnapshot *temperature,
                                   uint32_t now_ms)
{
    BeginDiagnostic(pair,now_ms);
    const MeasurementResultSnapshot previous=latest;
    latest = (MeasurementResultSnapshot){0};
    latest.captured_ms = now_ms;
    latest.status = MEASUREMENT_REJECTED;
    if (pair == NULL || !pair->valid || pair->time_domain == 0U)
    {
        diagnostic.decision=MEASUREMENT_DECISION_REJECT_INVALID;
        FillStableDiagnostic();
        return false;
    }
    if (temperature == NULL || !temperature->valid ||
        temperature->sound_speed_mm_s == 0U)
    {
        diagnostic.decision=MEASUREMENT_DECISION_REJECT_TEMPERATURE;
        FillStableDiagnostic();
        return false;
    }
    const uint64_t magnitude = AbsDelta(pair->delta_ns);
    const uint64_t distance = (magnitude * temperature->sound_speed_mm_s +
                               NS_PER_SEC / 2U) / NS_PER_SEC;
    diagnostic.temperature_distance_mm=(uint32_t)distance;
    diagnostic.temperature_version=temperature->version;
    diagnostic.sound_speed_mm_s=temperature->sound_speed_mm_s;
    if (distance > MEASUREMENT_MAX_DISTANCE_MM)
    {
        diagnostic.decision=MEASUREMENT_DECISION_REJECT_RANGE;
        FillStableDiagnostic();
        return false;
    }
    const int32_t corrected=(int32_t)distance+calibration_offset_mm;
    diagnostic.corrected_mm=corrected;
    if (corrected < 0 || corrected > (int32_t)MEASUREMENT_MAX_DISTANCE_MM)
    {
        diagnostic.decision=MEASUREMENT_DECISION_REJECT_RANGE;
        FillStableDiagnostic();
        return false;
    }
    const int32_t stable=StabilizeDistance(corrected,now_ms);
    if(stable<0)
    {
        diagnostic.decision=MEASUREMENT_DECISION_REJECT_OUTLIER;
        FillStableDiagnostic();
        latest=previous;
        return false;
    }
    latest.status = MEASUREMENT_VALID;
    latest.valid = true;
    latest.id = ++next_id;
    latest.distance_mm = stable;
    latest.calibration_version = calibration_version;
    latest.calibration_offset_mm = (int16_t)calibration_offset_mm;
    latest.direction = pair->direction;
    latest.quality = pair->quality;
    latest.sound_speed_mm_s = temperature->sound_speed_mm_s;
    latest.temperature_version = temperature->version;
    latest.temperature_valid = true;
    latest.time_domain = pair->time_domain;
    diagnostic.decision=MEASUREMENT_DECISION_ACCEPT;
    diagnostic.status=MEASUREMENT_VALID;
    diagnostic.measurement_id=latest.id;
    diagnostic.published_mm=stable;
    FillStableDiagnostic();
    return true;
}

bool MeasurementResult_PublishRemote(const MeasurementResultSnapshot *result,
                                     uint32_t now_ms)
{
    BeginDiagnostic(NULL,now_ms);
    if (result == NULL || result->status != MEASUREMENT_VALID || !result->valid ||
        result->id == 0U || result->distance_mm < 0 ||
        result->distance_mm > (int32_t)MEASUREMENT_MAX_DISTANCE_MM ||
        result->quality > 100U || result->sound_speed_mm_s == 0U ||
        result->time_domain == 0U)
    {
        diagnostic.decision=MEASUREMENT_DECISION_REJECT_INVALID;
        return false;
    }
    /* 同一ID刷新年龄；旧ID只在当前结果已超龄后允许覆盖，避免乱序旧包覆盖新值。 */
    if (latest.status == MEASUREMENT_VALID && latest.id > result->id &&
        (uint32_t)(now_ms - latest.captured_ms) < MEASUREMENT_MAX_AGE_MS)
    {
        diagnostic.decision=MEASUREMENT_DECISION_REJECT_INVALID;
        return false;
    }
    latest = *result;
    latest.captured_ms = now_ms;
    latest.status = MEASUREMENT_VALID;
    latest.valid = true;
    latest.temperature_valid = true;
    diagnostic.decision=MEASUREMENT_DECISION_REMOTE_ACCEPT;
    diagnostic.status=MEASUREMENT_VALID;
    diagnostic.measurement_id=latest.id;
    diagnostic.published_mm=latest.distance_mm;
    diagnostic.corrected_mm=latest.distance_mm;
    diagnostic.pair_distance_mm=(uint32_t)latest.distance_mm;
    diagnostic.quality=latest.quality;
    diagnostic.sound_speed_mm_s=latest.sound_speed_mm_s;
    diagnostic.temperature_version=latest.temperature_version;
    return true;
}

void MeasurementResult_Invalidate(uint32_t now_ms)
{
    BeginDiagnostic(NULL,now_ms);
    diagnostic.decision=MEASUREMENT_DECISION_INVALIDATE;
    diagnostic.status=MEASUREMENT_NONE;
    latest = (MeasurementResultSnapshot){0};
    latest.captured_ms = now_ms;
    latest.status = MEASUREMENT_NONE;
    ResetStable();
    FillStableDiagnostic();
}

/** @brief 显式撤销tracker展示状态；域/流硬边界由协调者调用，普通单点拒绝不调用。 */
void MeasurementResult_InvalidatePresentation(uint32_t now_ms)
{
    RangingTracker_Invalidate(&tracked,RANGING_INVALIDATE_FAULT,now_ms);
    ClearPresentation(MEASUREMENT_PRESENTATION_FAULT,now_ms);
}

bool MeasurementResult_Get(uint32_t now_ms, MeasurementResultSnapshot *out)
{
    if (out == NULL) { return false; }
    *out = latest;
    if (out->status != MEASUREMENT_VALID) { out->valid = false; return false; }
    if ((uint32_t)(now_ms - out->captured_ms) >= MEASUREMENT_MAX_AGE_MS)
    { out->valid = false; return false; }
    return true;
}

bool MeasurementResult_GetDiagnostics(MeasurementDiagnosticsSnapshot *out)
{
    if(out==NULL)return false;
    *out=diagnostic;
    return true;
}

/** @brief tracker状态映射到展示状态；未知值按FAULT处理。 */
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

/** @brief 用温度声速和标定把pair的有符号delta换算成展示距离。 */
static int32_t TrackedDistanceMm(const SoundPairResult *pair,
                                 const TemperatureSnapshot *temperature)
{
    const uint64_t magnitude=pair->delta_ns<0?
        (uint64_t)(-(pair->delta_ns+1))+1U:(uint64_t)pair->delta_ns;
    const uint64_t distance=(magnitude*temperature->sound_speed_mm_s+
        NS_PER_SEC/2U)/NS_PER_SEC;
    const int32_t corrected=(int32_t)distance+calibration_offset_mm;
    return corrected<0?0:corrected;
}

/** @brief 清空展示并进入指定无值状态；保留tracker自己的历史规则。 */
static void ClearPresentation(MeasurementPresentationState state,uint32_t now_ms)
{
    presentation=(MeasurementPresentationSnapshot){0};
    presentation.state=state;
    presentation.captured_ms=now_ms;
    ++presentation_revision;
    presentation.state_revision=presentation_revision;
}

RangingDecision MeasurementResult_PublishTrackedPair(const SoundPairResult *pair,
                                                     const TemperatureSnapshot *temperature,
                                                     bool formal,
                                                     uint32_t now_ms,
                                                     uint64_t now_common_ns)
{
    if(pair==NULL || !pair->valid || pair->time_domain==0U)
    {
        ClearPresentation(MEASUREMENT_PRESENTATION_NONE,now_ms);
        return RANGING_DECISION_REJECT;
    }
    if(temperature==NULL || !temperature->valid || temperature->sound_speed_mm_s==0U)
    {
        RangingTracker_Invalidate(&tracked,RANGING_INVALIDATE_FAULT,now_ms);
        ClearPresentation(MEASUREMENT_PRESENTATION_FAULT,now_ms);
        return RANGING_DECISION_REJECT;
    }
    const RangingTrackerContext context={
        .time_domain=pair->time_domain,
        .a_session=pair->a_session,
        .b_session=pair->b_session,
        .a_clock_epoch=1U,
        .domain_valid=true,.stream_valid=true,.temperature_valid=true};
    RangingTracker_SetContext(&tracked,&context,now_ms,now_common_ns);
    RangingTracker_SetSoundSpeed(&tracked,temperature->sound_speed_mm_s);
    const RangingObservation observation={
        .time_domain=pair->time_domain,.a_session=pair->a_session,
        .a_sequence=pair->a_sequence,.b_session=pair->b_session,
        .b_sequence=pair->b_sequence,.a_time_ns=pair->a_time_ns,
        .b_time_ns=pair->b_time_ns,.delta_ns=pair->delta_ns,
        .proxy_quality=pair->quality,.evidence_kind=RANGING_EVIDENCE_PROXY,
        .geometry_assumed_valid=true};
    const RangingDecision decision=RangingTracker_Push(
        &tracked,&observation,now_ms,now_common_ns);
    RangingTrackerSnapshot snapshot;
    RangingTracker_Get(&tracked,&snapshot);
    const uint64_t source_ns=pair->a_time_ns>pair->b_time_ns?
        pair->a_time_ns:pair->b_time_ns;

    if(snapshot.has_value)
    {
        presentation=(MeasurementPresentationSnapshot){0};
        presentation.state=PresentationFromTracker(snapshot.state);
        presentation.has_value=true;
        presentation.fresh=snapshot.fresh;
        /* 当前证据仍是PROXY，不能把标准序列确认写成身份验证FORMAL；
           A/B统一显示CONFIRMING，等A4/编码身份通过后再置FORMAL。 */
        presentation.formal=false;
        presentation.provisional=true;
        (void)formal;
        presentation.filtered_delta_ns=snapshot.filtered_delta_ns;
        presentation.distance_mm=snapshot.distance_mm;
        presentation.direction=(uint8_t)snapshot.direction;
        presentation.quality=snapshot.quality;
        presentation.evidence_kind=snapshot.evidence_kind;
        presentation.measurement_id=snapshot.measurement_id;
        presentation.state_revision=snapshot.state_revision;
        presentation.time_domain=pair->time_domain;
        presentation.authority_clock_epoch=1U;
        presentation.temperature_version=temperature->version;
        presentation.sound_speed_mm_s=temperature->sound_speed_mm_s;
        presentation.source_common_ns=snapshot.source_common_ns;
        presentation.fresh_deadline_ns=snapshot.fresh_deadline_ns;
        presentation.display_deadline_ns=snapshot.display_deadline_ns;
        presentation.captured_ms=now_ms;
        if(presentation.state_revision==0U){++presentation_revision;
            presentation.state_revision=presentation_revision;}
        return decision;
    }

    /* 首对独立事件可临时显示，但不能称为FORMAL；不足3点时tracker无确认值。 */
    if(decision==RANGING_DECISION_CANDIDATE && pair->quality>=90U)
    {
        ++presentation_id_counter;
        presentation=(MeasurementPresentationSnapshot){0};
        presentation.state=MEASUREMENT_PRESENTATION_ACQUIRING;
        presentation.has_value=true;
        presentation.fresh=true;
        presentation.provisional=true;
        presentation.formal=false;
        presentation.filtered_delta_ns=pair->delta_ns;
        presentation.distance_mm=TrackedDistanceMm(pair,temperature);
        presentation.direction=pair->delta_ns>0?1U:(pair->delta_ns<0?2U:0U);
        presentation.quality=pair->quality;
        presentation.evidence_kind=RANGING_EVIDENCE_PROXY;
        presentation.measurement_id=presentation_id_counter;
        presentation.time_domain=pair->time_domain;
        presentation.authority_clock_epoch=1U;
        presentation.temperature_version=temperature->version;
        presentation.sound_speed_mm_s=temperature->sound_speed_mm_s;
        presentation.source_common_ns=source_ns;
        presentation.fresh_deadline_ns=source_ns+UINT64_C(2500000000);
        presentation.display_deadline_ns=source_ns+UINT64_C(6500000000);
        presentation.captured_ms=now_ms;
        ++presentation_revision;
        presentation.state_revision=presentation_revision;
        return decision;
    }
    const MeasurementPresentationState state=snapshot.state==RANGING_STATE_FAULT?
        MEASUREMENT_PRESENTATION_FAULT:MEASUREMENT_PRESENTATION_ACQUIRING;
    ClearPresentation(state,now_ms);
    return decision;
}

void MeasurementResult_GetPresentation(MeasurementPresentationSnapshot *out)
{
    if(out!=NULL)*out=presentation;
}

bool MeasurementResult_PublishRemotePresentation(const MeasurementPresentationSnapshot *remote,
                                                 uint32_t time_domain,
                                                 uint32_t now_ms)
{
    if(remote==NULL || time_domain==0U || remote->time_domain!=time_domain)
    { return false; }
    if(presentation.time_domain==time_domain &&
       remote->state_revision<presentation.state_revision)
    { return false; }
    if(presentation.time_domain==time_domain &&
       remote->state_revision==presentation.state_revision)
    { return true; } /* 同revision重复包不续租、不重写展示内容。 */
    presentation=*remote;
    presentation.captured_ms=now_ms;
    ++presentation_revision;
    return true;
}
