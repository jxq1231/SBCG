/**
 * @file        calibration_control.c
 * @brief       前台500 mm标定双窗口控制和单请求交接。
 * @details     本轮身份（时钟代次、共同域、权威代次、本机音频会话）在采集窗首次有效即锁存，
 *              两个窗口内保持一致；采集按共同源时间选新测量，锁存后仅核验本轮身份和独立确认期限。
 * @note        不阻塞采集或测距；成功改变系数后的撤值和同步由调用方负责。
 */
#include "calibration_control.h"
#include <stddef.h>

_Static_assert(SBCG_CALIBRATION_CAPTURE_MS > 0U && SBCG_CALIBRATION_CAPTURE_MS < UINT32_MAX/2U,
               "invalid calibration capture window");
_Static_assert(SBCG_CALIBRATION_CONFIRM_MS > 0U && SBCG_CALIBRATION_CONFIRM_MS < UINT32_MAX/2U,
               "invalid calibration confirmation window");

static CalibrationControlSnapshot state;
static CalibrationAction pending;
static uint32_t phase_started_ms, start_clock_epoch, start_audio_session;
static uint32_t start_time_domain, start_authority_epoch;
static uint64_t source_floor_ns, last_clock_ns, audio_floor_frame;
static MeasurementContext locked_context;

/** @brief 清空活动标定和请求，已保存系数由Calibration模块独立管理。 */
void CalibrationControl_Init(void)
{
    state=(CalibrationControlSnapshot){0};
    pending=CALIBRATION_ACTION_NONE;
    start_time_domain=start_authority_epoch=0U;
}

/**
 * @brief 按当前阶段入队一次标定动作。
 * @param action 要执行的动作枚举。
 * @return 已排队为true；忙、阶段不匹配或非法动作返回false。
 * @note 前台调用；有效模式、权威及期限由Process在消费时核验。
 */
bool CalibrationControl_Request(CalibrationAction action)
{
    if(pending!=CALIBRATION_ACTION_NONE)return false;
    switch(action)
    {
    case CALIBRATION_ACTION_START:
    case CALIBRATION_ACTION_ENABLE:
    case CALIBRATION_ACTION_DISABLE:
    case CALIBRATION_ACTION_CLEAR:
        if(state.phase!=CALIBRATION_PHASE_IDLE)return false;
        break;
    case CALIBRATION_ACTION_CONFIRM:
        if(state.phase!=CALIBRATION_PHASE_WAIT_CONFIRM)return false;
        break;
    case CALIBRATION_ACTION_CANCEL:
        if(state.phase==CALIBRATION_PHASE_IDLE)return false;
        break;
    default:return false;
    }
    pending=state.action=action;
    state.result=CALIBRATION_CONTROL_PENDING;
    return true;
}

/**
 * @brief 发布一次状态转换，倒计时更新不触发事件。
 * @param result 要发布的控制状态或失败原因。
 * @return 无返回值。
 */
static void Report(CalibrationControlResult result)
{
    state.result=result;
    ++state.completed;
}

/**
 * @brief 结束当前流程并记录原因；候选副本只供终态诊断。
 * @param result 终止状态。
 * @return 无返回值。
 */
static void Finish(CalibrationControlResult result)
{
    state.phase=CALIBRATION_PHASE_IDLE;
    state.remaining_ms=0U;
    Report(result);
}

/**
 * @brief 判断锁存候选是否仍属于原时钟域和音频会话。
 * @param current 已刷新、非NULL上下文，本次借用。
 * @return 仍有效返回IDLE，否则返回域或音频会话失效原因。
 * @note 本机读时和代次已由调用方核验，普通测量年龄及新ID不参与比较。
 */
static CalibrationControlResult CandidateInvalidReason(const MeasurementContext *current)
{
    if(!current->stream_valid || current->a_session!=locked_context.a_session)
        return CALIBRATION_CONTROL_AUDIO_LOST;
    if(!current->domain_valid || current->time_domain!=locked_context.time_domain ||
       current->authority_epoch!=locked_context.authority_epoch ||
       current->source_floor_ns!=locked_context.source_floor_ns)
        return CALIBRATION_CONTROL_DOMAIN_LOST;
    return current->b_session==locked_context.b_session?
        CALIBRATION_CONTROL_IDLE:CALIBRATION_CONTROL_AUDIO_LOST;
}

/**
 * @brief 检查并捕获已由测量层校验过的首个合格新样本。
 * @param input 非NULL当前输入，只读且仅本次借用；sample可空。
 * @return 无返回值；捕获时开始独立确认计时。
 */
static void TryCapture(const CalibrationControlInput *input)
{
    const MeasurementContext *context=&input->context;
    if(input->chirp_session!=0U && input->chirp_session==start_audio_session &&
       input->chirp_onset_frame>=audio_floor_frame)
        state.heard_chirp=true;

    const MeasurementCalibrationSample *sample=input->sample;
    if(sample==NULL || sample->first_source_common_ns<=source_floor_ns ||
       sample->source_common_ns>input->common_ns ||
       sample->measurement_id==0U || !context->domain_valid || !context->stream_valid ||
       context->a_session==0U || context->b_session==0U ||
       sample->time_domain==0U || sample->time_domain!=context->time_domain ||
       sample->authority_clock_epoch==0U || sample->authority_clock_epoch!=context->authority_epoch)
        return;
    state.heard_chirp=true;
    if(!Calibration_CanSetFromMeasurement(sample->raw_mm))return;

    locked_context=*context;
    state.candidate=*sample;
    state.phase=CALIBRATION_PHASE_WAIT_CONFIRM;
    phase_started_ms=input->now_ms;
    state.remaining_ms=SBCG_CALIBRATION_CONFIRM_MS;
    Report(CALIBRATION_CONTROL_CANDIDATE);
}

/**
 * @brief 消费前台输入，先检查当前期限和身份，再推进采集或应用动作。
 * @param input 非NULL只读输入，调用期间借用；NULL保持现状。
 * @return 系数版本改变时为true；采集、取消或失败返回false。
 * @note 采集和确认使用无符号HAL差计时；共同时间用于拒绝旧源和时钟回退。
 */
bool CalibrationControl_Process(const CalibrationControlInput *input)
{
    if(input==NULL)return false;
    state.authority=input->context.authority;
    Calibration_Get(&state.calibration);
    state.applied_offset_mm=state.authority?
        (state.calibration.enabled?state.calibration.offset_mm:0):input->applied_offset_mm;
    state.applied_version=state.authority?state.calibration.version:input->applied_version;
    if(!input->mode_allowed)
    {
        CalibrationControl_CancelPendingForMode();
        return false;
    }

    const CalibrationAction action=pending;
    pending=CALIBRATION_ACTION_NONE;
    if(!state.authority)
    {
        if(action!=CALIBRATION_ACTION_NONE || state.phase!=CALIBRATION_PHASE_IDLE)
            Finish(CALIBRATION_CONTROL_A_ONLY);
        return false;
    }

    const bool clock_ok=input->context.clock_valid &&
        input->context.local_clock_epoch!=0U && input->common_ns!=0U;
    if(state.phase!=CALIBRATION_PHASE_IDLE)
    {
        const bool waiting=state.phase==CALIBRATION_PHASE_WAIT_SOUND;
        const uint32_t limit_ms=waiting?SBCG_CALIBRATION_CAPTURE_MS:SBCG_CALIBRATION_CONFIRM_MS;
        const uint32_t elapsed_ms=input->now_ms-phase_started_ms;
        if(elapsed_ms>=limit_ms)
        {
            Finish(!waiting?CALIBRATION_CONTROL_CONFIRM_TIMEOUT:
                (state.heard_chirp?CALIBRATION_CONTROL_NO_QUALIFIED_SAMPLE:CALIBRATION_CONTROL_NO_SOUND));
            return false;
        }
        if(!clock_ok || input->context.local_clock_epoch!=start_clock_epoch ||
           input->common_ns<last_clock_ns)
        {
            Finish(CALIBRATION_CONTROL_CLOCK_LOST);
            return false;
        }
        /* 本轮身份：共同域、权威代次和音频会话在本轮第一次有效观测时锁存，
           之后在采集与确认两个窗口内都必须保持一致；变化即退出并报告原因。
           启动时尚未建立的项允许先等待，首次有效即锁定，避免静音环境无法开轮。
           瞬时失效（域/会话暂时无效）只暂停捕获——此时候选本来就通不过身份校验；
           只有恢复后的标识值发生变化才结束本轮，避免一次抖动白丢整轮。 */
        if(input->context.domain_valid && input->context.time_domain!=0U)
        {
            if(start_time_domain==0U)
            {
                start_time_domain=input->context.time_domain;
            }
            else if(input->context.time_domain!=start_time_domain)
            {
                Finish(CALIBRATION_CONTROL_DOMAIN_LOST);
                return false;
            }
        }
        if(input->context.authority_epoch!=0U)
        {
            if(start_authority_epoch==0U)
            {
                start_authority_epoch=input->context.authority_epoch;
            }
            else if(input->context.authority_epoch!=start_authority_epoch)
            {
                Finish(CALIBRATION_CONTROL_DOMAIN_LOST);
                return false;
            }
        }
        if(input->context.a_session!=0U)
        {
            if(start_audio_session==0U)
            {
                start_audio_session=input->context.a_session;
            }
            else if(input->context.a_session!=start_audio_session)
            {
                Finish(CALIBRATION_CONTROL_AUDIO_LOST);
                return false;
            }
        }
        if(!waiting)
        {
            const CalibrationControlResult reason=CandidateInvalidReason(&input->context);
            if(reason!=CALIBRATION_CONTROL_IDLE)
            {
                Finish(reason);
                return false;
            }
        }
        state.remaining_ms=limit_ms-elapsed_ms;
        last_clock_ns=input->common_ns;
        if(action==CALIBRATION_ACTION_CANCEL)
        {
            Finish(CALIBRATION_CONTROL_CANCELLED);
            return false;
        }
        if(waiting)
        {
            TryCapture(input);
            return false;
        }
    }

    const uint16_t before=state.calibration.version;
    switch(action)
    {
    case CALIBRATION_ACTION_START:
        state.candidate=(MeasurementCalibrationSample){0};
        state.heard_chirp=false;
        if(!clock_ok)
        {
            Finish(CALIBRATION_CONTROL_CLOCK_LOST);
            return false;
        }
        /* 采集窗的身份下界依赖本机音频会话；无会话时无法证明"本轮启动后产生"。 */
        if(input->context.a_session==0U)
        {
            Finish(CALIBRATION_CONTROL_AUDIO_LOST);
            return false;
        }
        state.phase=CALIBRATION_PHASE_WAIT_SOUND;
        state.remaining_ms=SBCG_CALIBRATION_CAPTURE_MS;
        phase_started_ms=input->now_ms;
        last_clock_ns=source_floor_ns=input->common_ns;
        start_clock_epoch=input->context.local_clock_epoch;
        start_audio_session=input->context.a_session;
        start_time_domain=input->context.domain_valid?input->context.time_domain:0U;
        start_authority_epoch=input->context.authority_epoch;
        audio_floor_frame=input->audio_floor_frame;
        Report(CALIBRATION_CONTROL_LISTENING);
        return false;
    case CALIBRATION_ACTION_CONFIRM:
        Finish(Calibration_SetFromMeasurement(state.candidate.raw_mm)?
            CALIBRATION_CONTROL_APPLIED:CALIBRATION_CONTROL_OUT_OF_RANGE);
        break;
    case CALIBRATION_ACTION_CLEAR:
        Calibration_Reset();
        Finish(CALIBRATION_CONTROL_APPLIED);
        break;
    case CALIBRATION_ACTION_ENABLE:
    case CALIBRATION_ACTION_DISABLE:
        Finish(Calibration_SetEnabled(action==CALIBRATION_ACTION_ENABLE)?
            CALIBRATION_CONTROL_APPLIED:CALIBRATION_CONTROL_NO_SAVED);
        break;
    default:return false;
    }

    Calibration_Get(&state.calibration);
    state.applied_offset_mm=state.calibration.enabled?state.calibration.offset_mm:0;
    state.applied_version=state.calibration.version;
    return before!=state.calibration.version;
}

/**
 * @brief 复制控制状态，保留已锁候选及界面剩余时间。
 * @param out 调用方拥有的输出，可NULL且不保留地址。
 * @return 无返回值。
 */
void CalibrationControl_GetSnapshot(CalibrationControlSnapshot *out)
{
    if(out!=NULL)*out=state;
}

/** @brief 模式不允许时撤销活动流程或待处理请求，保持保存系数。 */
void CalibrationControl_CancelPendingForMode(void)
{
    if(pending==CALIBRATION_ACTION_NONE && state.phase==CALIBRATION_PHASE_IDLE)return;
    pending=CALIBRATION_ACTION_NONE;
    Finish(CALIBRATION_CONTROL_MODE_REJECT);
}

/**
 * @brief 将状态转成稳定日志字段。
 * @param result 待描述的控制状态。
 * @return 静态NUL结尾名称，未知值返回UNKNOWN。
 */
const char *CalibrationControl_ResultName(CalibrationControlResult result)
{
    switch(result)
    {
    case CALIBRATION_CONTROL_IDLE:return "IDLE";
    case CALIBRATION_CONTROL_PENDING:return "PENDING";
    case CALIBRATION_CONTROL_APPLIED:return "APPLIED";
    case CALIBRATION_CONTROL_A_ONLY:return "A_ONLY";
    case CALIBRATION_CONTROL_NO_SOUND:return "NO_CHIRP";
    case CALIBRATION_CONTROL_NO_QUALIFIED_SAMPLE:return "CHIRP_NO_SAMPLE";
    case CALIBRATION_CONTROL_NO_SAVED:return "NO_SAVED";
    case CALIBRATION_CONTROL_OUT_OF_RANGE:return "OUT_OF_RANGE";
    case CALIBRATION_CONTROL_MODE_REJECT:return "MODE_REJECT";
    case CALIBRATION_CONTROL_LISTENING:return "PLAY_SOUND";
    case CALIBRATION_CONTROL_CANDIDATE:return "WAIT_CONFIRM";
    case CALIBRATION_CONTROL_CANCELLED:return "CANCELLED";
    case CALIBRATION_CONTROL_CONFIRM_TIMEOUT:return "CONFIRM_TIMEOUT";
    case CALIBRATION_CONTROL_CLOCK_LOST:return "CLOCK_LOST";
    case CALIBRATION_CONTROL_DOMAIN_LOST:return "DOMAIN_LOST";
    case CALIBRATION_CONTROL_AUDIO_LOST:return "AUDIO_SESSION_LOST";
    default:return "UNKNOWN";
    }
}
