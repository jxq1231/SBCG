/**
 * @file        time_sync.c
 * @brief       协调固定A参考/B跟随的硬件四时刻交换与B时钟校正。
 * @details     B端先检查250ms结果时效及100ms交换历时，再交给纯C伺服；每次实际硬件动作之前清理所有旧时域
 *              配对、锚点及模型，随后立即向伺服提交有界BSP结果。A只推进协议，不运行伺服或写时钟。
 * @note        仅NO_SYS串行前台，放行信号为false时整次返回。旧服务须先安装，新服务须先卸载；SYNC固定为0，
 *              内部锁定仅是闭环观测状态，不代表独立精度验收。无新的IRQ/DMA/缓存配置。
 */
#include "Sync/time_sync.h"
#ifdef SBCG_USE_SOUND_TIME
#include "Detector/sound_event_time.h"
#endif
#include "Sync/clock_servo.h"
#include "Sync/ptp_clock.h"
#include "Sync/sync_observation.h"
#include "Network/ptp_board_sync.h"
#include "Network/udp_time_exchange.h"
#include "BSP/bsp_ptp.h"
#include "BSP/bsp_ptp_tx.h"
#include "Audio/audio_time_anchor.h"
#include "Audio/audio_time_map_monitor.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define EXCHANGE_MAX_AGE_MS 250U
#define EXCHANGE_MAX_WINDOW_NS UINT64_C(100000000)
#define CONTROL_LOG_PERIOD_MS 1000U
#define CONTROL_LINE_CAPACITY 256U

static bool configured, faulted;
static BoardRole board_role;
static ClockServo servo;
static uint32_t cached_epoch, cached_revision, last_log_ms, rejected_samples, errors;
static uint32_t (*read_tick)(void);
static void (*emit_line)(const char *,int);
/* 与App日志队列相同的固定上限，前台单一调用者，不保留外部字符串。 */
static char line[CONTROL_LINE_CAPACITY];
/**
 * @brief 复制当前同步状态，保持伺服与硬件完全不变。
 * @param now_ms 当前HAL毫秒；B有效样本年龄须小于2000ms。
 * @param out 可空输出，不保留指针；非NULL总是先清零。
 * @return 无返回值；A不提供B-A偏差，失效/FAULT不沿用旧偏差或内部锁。
 */
void TimeSync_GetSnapshot(uint32_t now_ms, TimeSyncSnapshot *out)
{
    if (out == NULL) { return; }
    *out = (TimeSyncSnapshot){0};
    out->configured = configured;
    out->role = board_role;
    out->faulted = faulted || (configured && board_role == BOARD_ROLE_B && servo.state == CLOCK_SERVO_FAULT);
    if (!configured || board_role != BOARD_ROLE_B) { return; }
    out->state = servo.state;
    out->sample_age_ms = now_ms - servo.last_sample_ms;
    out->offset_valid = !out->faulted && servo.have_sample && out->sample_age_ms < 2000U &&
        (servo.state == CLOCK_SERVO_TRACKING || servo.state == CLOCK_SERVO_LOCKED_INTERNAL);
    out->internal_locked = out->offset_valid && servo.internal_locked;
    if (out->offset_valid) { out->offset_x2_ns = servo.offset_x2_ns; }
}
_Static_assert(sizeof("$SYNC_CTRL,ROLE=B,STATE=LOCKED_INTERNAL,INTERNAL_LOCK=1,SYNC=0,"
    "OFFSET_X2_NS=-8589934591999999998,DELAY_X2_NS=4000000,DRIFT_PPB=-2147483648,"
    "FREQ_PPB=-100000,EPOCH=4294967295,RATE_REV=4294967295,VALID=4294967295,"
    "REJECT=4294967295,ERRORS=4294967295\r\n") <= CONTROL_LINE_CAPACITY,
    "control log exceeds application queue capacity");

/**
 * @brief   饱和递增协调器诊断计数。
 * @param   count 非NULL可写计数器。
 * @return  无返回值。
 * @note    仅前台；UINT32_MAX后保持，避免长期运行回绕。
 */
static void Increment(uint32_t *count) { if (*count != UINT32_MAX) { ++*count; } }

/**
 * @brief   将64位无符号整数转为十进制文本。
 * @param   value 待格式化数值。
 * @param   out 调用方至少21字节可写缓冲，不可为NULL。
 * @return  无返回值，输出以NUL结束。
 * @note    避免依赖嵌入式精简printf的64位支持。
 */
static void Decimal(uint64_t value,char out[21])
{
    char reversed[20]; unsigned count=0U;
    do { reversed[count++]=(char)('0'+value%10U); value/=10U; } while (value!=0U);
    for (unsigned i=0U;i<count;++i) { out[i]=reversed[count-i-1U]; }
    out[count]='\0';
}

/**
 * @brief   安全格式化带符号64位整数。
 * @param   value 待格式化数值，支持INT64_MIN。
 * @param   out 调用方至少22字节可写缓冲，不可为NULL。
 * @return  无返回值，输出以NUL结束。
 * @note    通过加一分解避免直接对最小负数取负。
 */
static void SignedDecimal(int64_t value,char out[22])
{
    if (value<0)
    {
        out[0]='-'; Decimal((uint64_t)(-(value+1))+1U,out+1);
    }
    else { Decimal((uint64_t)value,out); }
}

/**
 * @brief   只发送完整且有界的本次日志。
 * @param   length snprintf返回长度，不含NUL。
 * @return  无返回值。
 * @note    前台使用静态line；回调必须返回前复制或消费，NULL回调关闭输出。
 */
static void Emit(int length)
{
    if (emit_line!=NULL && length>0 && (size_t)length<sizeof line) { emit_line(line,length); }
}

/**
 * @brief   在不写硬件的情况下锁存协调故障。
 * @param   now_ms 当前单调毫秒值。
 * @return  无返回值。
 * @note    B使用失败的NONE提交进入核心FAULT，保留已确认频率；A不调用伺服。重复故障不重复计数。
 */
static void Fault(uint32_t now_ms)
{
    if (faulted) { return; }
    faulted=true; Increment(&errors);
    if (board_role==BOARD_ROLE_B)
    {
        const ClockServoAction none={CLOCK_SERVO_NONE,0,0};
        ClockServo_Commit(&servo,now_ms,&none,false);
    }
}

/**
 * @brief   清除所有可能跨时间或速率边界的前台消费者记录。
 * @param   now_ms 当前HAL毫秒计数，供音频重新等待一秒。
 * @return  true表示旧UDP回调不活动且所有消费者均已通知；false时未继续清理其它消费者。
 * @note    必须在任何硬件命令之前完成，不调用ClockServo_Invalidate，避免取消待提交动作。
 */
static bool InvalidateConsumers(uint32_t now_ms)
{
#ifdef SBCG_USE_SOUND_TIME
    /* 模型失效不得被可失败的协议卸载阻断。 */
    SoundEventTime_Invalidate(SOUND_TIME_CLOCK);
#endif
    if (!UDPTimeExchange_NotifyClockChange()) { return false; }
    PtpClock_NotifyClockChange();
    AudioTimeAnchor_NotifyClockChange(now_ms);
    AudioTimeMapMonitor_NotifyClockChange();
    SyncObservation_NotifyClockChange();
    PtpBoardSync_NotifyClockChange(BSP_PTP_GetEpoch());
    return true;
}

/**
 * @brief   缓存实际BSP代际，并再次发布命令完成或失败后的板间协议边界。
 * @return  无返回值。
 * @note    仅BSP命令返回后调用；失败也不沿用旧配对，不声明硬件回滚。
 */
static void PublishClockGeneration(void)
{
    cached_epoch=BSP_PTP_GetEpoch(); cached_revision=BSP_PTP_GetRateRevision();
    PtpBoardSync_NotifyClockChange(cached_epoch);
}

/**
 * @brief   按严格门控、消费者失效、硬件操作、立即提交的顺序执行一个动作。
 * @param   now_ms 与本次Observe相同的前台毫秒值。
 * @param   action 非NULL，必须是核心刚生成且尚未提交的原样动作。
 * @return  无返回值；失败使核心FAULT，不重试硬件。
 * @note    正phase推进B，正frequency加快B；仅实际BSP_OK允许成功提交。
 */
static void ApplyAction(uint32_t now_ms,const ClockServoAction *action)
{
    BSP_PtpStatus status=BSP_PTP_TxClean();
    bool issued=false;
    if (status==BSP_PTP_OK && !InvalidateConsumers(now_ms)) { status=BSP_PTP_NOT_READY; }
    if (status==BSP_PTP_OK)
    {
        if (action->kind==CLOCK_SERVO_STEP)
        { issued=true; status=BSP_PTP_Step(action->phase_ns); }
        else if (action->kind==CLOCK_SERVO_FREQUENCY)
        { issued=true; status=BSP_PTP_SetFrequencyPpb(action->frequency_ppb); }
        else { status=BSP_PTP_BAD_CONFIG; }
    }
    ClockServo_Commit(&servo,now_ms,action,status==BSP_PTP_OK);
    if (status!=BSP_PTP_OK) { faulted=true; Increment(&errors); }
    if (issued) { PublishClockGeneration(); }
    if (BSP_PTP_GetFrequencyPpb()!=servo.frequency_ppb) { Fault(now_ms); }
    char phase[22]; SignedDecimal(action->phase_ns,phase);
    Emit(snprintf(line,sizeof line,
        "$SYNC_ADJUST,KIND=%s,PHASE_NS=%s,FREQ_PPB=%" PRId32 ",STATUS=%u,EPOCH=%" PRIu32
        ",RATE_REV=%" PRIu32 ",SYNC=0\r\n",
        action->kind==CLOCK_SERVO_STEP ? "STEP" : "FREQUENCY",phase,action->frequency_ppb,
        (unsigned)status,BSP_PTP_GetEpoch(),BSP_PTP_GetRateRevision()));
}

/**
 * @brief   处理非本协调器产生的代际变化，清历史但保留已确认频率。
 * @param   now_ms 前台当前毫秒计数。
 * @return  无返回值；旧UDP回调活动或B硬件记录不匹配时锁存故障。
 * @note    A不使用伺服；B每次放行还检查本地读时，避免在失效时钟上宣称内部锁定。
 */
static void CheckClockGeneration(uint32_t now_ms)
{
    const uint32_t epoch=BSP_PTP_GetEpoch(), revision=BSP_PTP_GetRateRevision();
    if (epoch!=cached_epoch || revision!=cached_revision)
    {
#ifdef SBCG_USE_SOUND_TIME
        /* 外部代际已改变；即使PPS撤资格失败，旧模型也绝不能继续有效。 */
        SoundEventTime_Invalidate(SOUND_TIME_CLOCK);
#endif
        if (!InvalidateConsumers(now_ms)) { Fault(now_ms); return; }
        cached_epoch=epoch; cached_revision=revision;
        if (board_role==BOARD_ROLE_B) { ClockServo_Invalidate(&servo,now_ms); }
    }
    if (board_role==BOARD_ROLE_B)
    {
        BSP_PtpTime sample;
        if (BSP_PTP_Read(&sample)!=BSP_PTP_OK || BSP_PTP_GetFrequencyPpb()!=servo.frequency_ppb)
        { Fault(now_ms); }
    }
}

/**
 * @brief   每至少一秒报告一次内部状态、时差、漂移与实际频率记录。
 * @param   now_ms 当前HAL毫秒计数，使用无符号差节流并支持自然回绕。
 * @return  无返回值。
 * @note    即使核心内部锁定，SYNC仍固定为0；不补发延迟期间日志，不声明外部同步精度。
 */
static void LogControl(uint32_t now_ms)
{
    if ((uint32_t)(now_ms-last_log_ms)<CONTROL_LOG_PERIOD_MS) { return; }
    last_log_ms=now_ms;
    const bool is_b=board_role==BOARD_ROLE_B;
    char offset[22],delay[22];
    SignedDecimal(is_b ? servo.offset_x2_ns : 0,offset);
    SignedDecimal(is_b ? servo.delay_x2_ns : 0,delay);
    Emit(snprintf(line,sizeof line,
        "$SYNC_CTRL,ROLE=%s,STATE=%s,INTERNAL_LOCK=%u,SYNC=0,OFFSET_X2_NS=%s,DELAY_X2_NS=%s,"
        "DRIFT_PPB=%" PRId32 ",FREQ_PPB=%" PRId32 ",EPOCH=%" PRIu32 ",RATE_REV=%" PRIu32
        ",VALID=%" PRIu32 ",REJECT=%" PRIu32 ",ERRORS=%" PRIu32 "\r\n",
        is_b ? "B" : "A",faulted ? "FAULT" : (is_b ? ClockServo_StateName(&servo) : "REFERENCE"),
        is_b && !faulted && servo.internal_locked ? 1U : 0U,offset,delay,
        is_b ? servo.drift_ppb : 0,BSP_PTP_GetFrequencyPpb(),BSP_PTP_GetEpoch(),
        BSP_PTP_GetRateRevision(),is_b ? servo.valid_samples : 0U,rejected_samples,errors));
}

/**
 * @brief   拒绝未进入核心的过期或非法四时刻，并立即撤销锁定与连续样本证据。
 * @param   now_ms 当前HAL毫秒计数，仅用于按最后有效样本推进失联超时。
 * @return  无返回值。
 * @note    仅B前台；保留已确认频率及最后有效样本毫秒，不写硬件，不把坏样本冒充有效输入或改音频时域。
 */
static void RejectMeasurement(uint32_t now_ms)
{
    Increment(&rejected_samples);
    ClockServo_RejectMeasurement(&servo,now_ms);
}

/**
 * @brief   安全卸载板间最外层包装后停用本协调器。
 * @return  true表示卸载成功；false保持原协调状态与回调指针不变。
 * @note    不重启或校正硬件；必须先于旧UDPTimeExchange卸载。
 */
bool TimeSync_Deinit(void)
{
    if (!PtpBoardSync_Deinit()) { return false; }
    configured=false; read_tick=NULL; emit_line=NULL; board_role=BOARD_ROLE_UNCONFIGURED;
    return true;
}

/**
 * @brief   校验已初始化本地时钟与登记角色，安装协议并以实际已确认频率初始化B伺服。
 * @param   now_ms 前台当前毫秒计数，作为日志及获取起点。
 * @param   profile 只读登记身份，不可为NULL，函数仅保存角色，协议复制完整资料。
 * @param   tick 非NULL长期有效的毫秒读取回调。
 * @param   log 可为NULL的日志回调，返回前须消费或复制文本。
 * @return  true表示服务就绪；false不隐式初始化或复位硬件。
 * @note    已确认B频率必须由BSP记录且在核心范围内，支持健康运行状态重新安装而不擅自恢复0ppb。
 */
bool TimeSync_Init(uint32_t now_ms,const BoardIdentityProfile *profile,
                   uint32_t (*tick)(void),void (*log)(const char *,int))
{
    if (!TimeSync_Deinit()) { return false; }
    if (profile==NULL || tick==NULL ||
        (profile->role!=BOARD_ROLE_A && profile->role!=BOARD_ROLE_B)) { return false; }
    BSP_PtpTime sample;
    if (BSP_PTP_Read(&sample)!=BSP_PTP_OK) { return false; }
    if (profile->role==BOARD_ROLE_B &&
        !ClockServo_InitKnown(&servo,now_ms,BSP_PTP_GetFrequencyPpb())) { return false; }
    if (!PtpBoardSync_Init(now_ms,profile,tick,log)) { return false; }
    board_role=profile->role; read_tick=tick; emit_line=log;
    cached_epoch=BSP_PTP_GetEpoch(); cached_revision=BSP_PTP_GetRateRevision();
    last_log_ms=now_ms; rejected_samples=errors=0U; faulted=false; configured=true;
    return true;
}

/**
 * @brief   放行时推进协议，B端消费一项新鲜估计并立即执行/提交至多一个动作。
 * @param   now_ms 入口HAL毫秒计数，协议处理后由保存的tick刷新用于样本年龄和提交。
 * @param   processing_allowed false时整次返回，不发送、不估计、不清理描述符或写时钟。
 * @return  无返回值。
 * @note    A不调用核心；B失联由Tick撤锁，peerReset清历史。所有动作在包装回调之外的串行前台执行。
 */
void TimeSync_Process(uint32_t now_ms,bool processing_allowed)
{
    if (!configured || !processing_allowed) { return; }
    now_ms=read_tick();
    CheckClockGeneration(now_ms);
    PtpBoardSync_Process(now_ms);
    now_ms=read_tick();
    if (board_role==BOARD_ROLE_B) { ClockServo_Tick(&servo,now_ms); }
    const bool peer_changed=PtpBoardSync_TakePeerReset();
    if (peer_changed)
    {
        if (!InvalidateConsumers(now_ms)) { Fault(now_ms); }
        if (board_role==BOARD_ROLE_B) { ClockServo_Invalidate(&servo,now_ms); }
    }
    if (board_role==BOARD_ROLE_B && !peer_changed)
    {
        SyncExchange exchange; uint32_t captured_ms;
        if (PtpBoardSync_TakeExchange(&exchange,&captured_ms))
        {
            if (faulted || servo.state==CLOCK_SERVO_FAULT) { Increment(&rejected_samples); }
            else if ((uint32_t)(now_ms-captured_ms)>EXCHANGE_MAX_AGE_MS) { RejectMeasurement(now_ms); }
            else
            {
                SyncEstimate estimate;
                if (SyncEstimator_Calculate(&exchange,EXCHANGE_MAX_WINDOW_NS,&estimate)!=SYNC_ESTIMATOR_OK)
                { RejectMeasurement(now_ms); }
                else
                {
                    ClockServoAction action;
                    if (!ClockServo_Observe(&servo,now_ms,exchange.t1_a_tx_ns,
                        estimate.offset_x2_ns,estimate.mean_path_delay_x2_ns,&action))
                    { Increment(&rejected_samples); }
                    else if (action.kind!=CLOCK_SERVO_NONE) { ApplyAction(now_ms,&action); }
                }
            }
        }
    }
    LogControl(now_ms);
}
