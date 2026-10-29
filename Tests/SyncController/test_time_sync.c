/**
 * @file        test_time_sync.c
 * @brief       用真实接口和依赖替代验证同步协调器的写时钟门控。
 * @details     检查角色隔离、代际清理顺序、动作提交、外部变化、时效及生命周期；不模拟硬件精度。
 * @note        纯C伺服与四时刻数学由各自测试验证，本测试只检查模块之间的真实调用约定。
 */
#include "Sync/time_sync.h"
#include "Sync/clock_servo.h"
#include "Sync/sync_estimator.h"
#include "Network/ptp_board_sync.h"
#include "Network/udp_time_exchange.h"
#include "BSP/bsp_ptp.h"
#include "BSP/bsp_ptp_tx.h"
#include "Sync/ptp_clock.h"
#include "Sync/sync_observation.h"
#include "Audio/audio_time_anchor.h"
#include "Audio/audio_time_map_monitor.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint32_t now_tick, epoch, revision, captured, protocol_calls, observe_calls, tick_calls;
static uint32_t commit_calls, invalidate_calls, clock_writes, estimator_calls, notify_calls;
static bool deinit_ok, install_ok, udp_clear_ok, have_exchange, peer_reset, observe_ok;
static uint32_t process_advance_ms;
static bool capture_on_process;
static BSP_PtpStatus read_status, clean_status, write_status;
static SyncEstimatorStatus estimate_status;
static int32_t hardware_frequency;
static ClockServoAction requested;
static ClockServo *active_servo;
static char trace[256], logs[20000];
static unsigned trace_length;
static uint64_t last_reference;
static int64_t last_offset, last_delay;

/** @brief 记录依赖调用顺序。 @param c 单字节事件。 @return 无返回值。 */
static void Event(char c) { assert(trace_length+1U < sizeof trace); trace[trace_length++]=c; trace[trace_length]='\0'; }
/** @brief 返回测试毫秒。 @return 当前毫秒值。 @note 不隐式推进时间。 */
static uint32_t Tick(void) { return now_tick; }
/**
 * @brief 累积完整日志并拒绝截断或超长记录。
 * @param text 本次有效文本，不可为NULL。
 * @param length 字节数，不含NUL，须小于256。
 * @return 无返回值。
 */
static void Log(const char *text,int length)
{
    assert(length>0 && length<256);
    assert(strlen(text)==(size_t)length);
    assert(strlen(logs)+(size_t)length<sizeof logs);
    memcpy(logs+strlen(logs),text,(size_t)length+1U);
}

/** @brief 模拟协议卸载所有权检查。 @return 当前允许标志。 @note 不改协调器状态。 */
bool PtpBoardSync_Deinit(void) { return deinit_ok; }
/**
 * @brief 模拟最外层协议安装。
 * @param now 初始化毫秒，本替代不使用。
 * @param p 非NULL身份，本测试由协议真实测试负责核对内容。
 * @param t 非NULL毫秒回调。
 * @param l 可为NULL日志回调，不保存。
 * @return 当前安装允许标志。
 */
bool PtpBoardSync_Init(uint32_t now,const BoardIdentityProfile *p,uint32_t(*t)(void),void(*l)(const char*,int))
{ (void)now; assert(p && t); (void)l; return install_ok; }
/**
 * @brief 模拟协议推进，可注入处理耗时与新结果形成。
 * @param now 入口毫秒，本替代不使用。
 * @return 无返回值。
 * @note 只记录调度并推进测试tick，不发送网络数据。
 */
void PtpBoardSync_Process(uint32_t now)
{
    (void)now; ++protocol_calls; Event('P'); now_tick+=process_advance_ms;
    if(capture_on_process && have_exchange) captured=now_tick;
}
/**
 * @brief 消费一次预置四时刻。
 * @param e 非NULL输出，成功写入固定A/B方向的纳秒值。
 * @param ms 非NULL输出，成功写入形成时毫秒计数。
 * @return true表示消费一次，false不写输出。
 */
bool PtpBoardSync_TakeExchange(SyncExchange *e,uint32_t *ms)
{
    if (!have_exchange) return false;
    *e=(SyncExchange){UINT64_C(1000000000),UINT64_C(1000000500),UINT64_C(1000100500),UINT64_C(1000101000)};
    *ms=captured; have_exchange=false; return true;
}
/** @brief 验证协议收到当前代际并清队。 @param e 当前epoch。 @return 无返回值。 */
void PtpBoardSync_NotifyClockChange(uint32_t e) { assert(e==epoch); ++notify_calls; have_exchange=false; Event('N'); }
/** @brief 消费对端重置标志。 @return 是否存在标志。 @note 每标志只返回一次true。 */
bool PtpBoardSync_TakePeerReset(void) { bool result=peer_reset; peer_reset=false; return result; }
/** @brief 记录旧UDP清队检查。 @return 当前回调空闲允许标志。 @note false应阻止时钟写入。 */
bool UDPTimeExchange_NotifyClockChange(void) { Event('U'); return udp_clear_ok; }
/** @brief 记录本地PTP观察器失效通知。 @return 无返回值。 */
void PtpClock_NotifyClockChange(void) { Event('C'); }
/** @brief 验证音频锚点使用新鲜毫秒值。 @param now 当前测试毫秒。 @return 无返回值。 */
void AudioTimeAnchor_NotifyClockChange(uint32_t now) { assert(now==now_tick); Event('A'); }
/** @brief 记录音频模型清窗。 @return 无返回值。 */
void AudioTimeMapMonitor_NotifyClockChange(void) { Event('M'); }
/** @brief 记录旧四时刻观察队列清空。 @return 无返回值。 */
void SyncObservation_NotifyClockChange(void) { Event('S'); }

/** @brief 模拟本地读时。 @param time 非NULL输出，失败保持。 @return 当前预置读时状态。 */
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *time)
{ assert(time); if(read_status==BSP_PTP_OK) *time=(BSP_PtpTime){1U,1U}; return read_status; }
/** @brief 记录TX空闲前置检查。 @return 当前预置清理状态。 */
BSP_PtpStatus BSP_PTP_TxClean(void) { Event('T'); return clean_status; }
/** @brief 返回模拟时间代际。 @return 当前epoch。 */
uint32_t BSP_PTP_GetEpoch(void) { return epoch; }
/** @brief 返回模拟模型代际。 @return 当前revision。 */
uint32_t BSP_PTP_GetRateRevision(void) { return revision; }
/** @brief 返回模拟最近确认频率。 @return 有符号ppb。 */
int32_t BSP_PTP_GetFrequencyPpb(void) { return hardware_frequency; }
/**
 * @brief 模拟频率命令及其代际边界。
 * @param ppb 请求的绝对频率修正，单位ppb。
 * @return 当前预置硬件命令结果。
 * @note 成功才更新实际频率，失败同时提升epoch模拟不确定完成。
 */
BSP_PtpStatus BSP_PTP_SetFrequencyPpb(int32_t ppb)
{
    Event('F'); ++clock_writes; ++revision;
    if(write_status==BSP_PTP_OK) hardware_frequency=ppb; else ++epoch;
    return write_status;
}
/** @brief 模拟相位命令。 @param phase 必须与核心刚生成的纳秒动作相同。 @return 预置命令结果。 */
BSP_PtpStatus BSP_PTP_Step(int64_t phase)
{ assert(phase==requested.phase_ns); Event('H'); ++clock_writes; ++epoch; ++revision; return write_status; }
/**
 * @brief 验证协调器向估计器传递100ms交换上限。
 * @param e 非NULL匹配交换。
 * @param window 必须为100000000纳秒。
 * @param s 非NULL结果，成功填写-200纳秒分子与1000纳秒时延分子。
 * @return 预置估计器状态。
 */
SyncEstimatorStatus SyncEstimator_Calculate(const SyncExchange *e,uint64_t window,SyncEstimate *s)
{
    assert(e && window==UINT64_C(100000000)); ++estimator_calls;
    if(estimate_status==SYNC_ESTIMATOR_OK) *s=(SyncEstimate){-200,1000};
    return estimate_status;
}
/** @brief 初始化替代核心。 @param s 非NULL输出。 @param now 起始毫秒。 @return 无返回值。 */
void ClockServo_Init(ClockServo *s,uint32_t now)
{ active_servo=s; memset(s,0,sizeof *s); s->state=CLOCK_SERVO_ACQUIRING; s->last_sample_ms=now; }
/**
 * @brief 模拟已知有界频率的核心初始化。
 * @param s 非NULL输出。
 * @param now 起始毫秒。
 * @param ppb BSP确认的频率修正。
 * @return 范围有效为true，越界为false。
 */
bool ClockServo_InitKnown(ClockServo *s,uint32_t now,int32_t ppb)
{
    if(ppb>100000 || ppb<-100000) return false;
    ClockServo_Init(s,now); s->frequency_ppb=ppb; return true;
}
/** @brief 记录核心超时调度。 @param s 非NULL状态。 @param now 当前毫秒。 @return 无返回值。 */
void ClockServo_Tick(ClockServo *s,uint32_t now)
{
    ++tick_calls;
    if(s->state==CLOCK_SERVO_FAULT) return;
    if(now-s->last_sample_ms>=5000U) s->state=CLOCK_SERVO_UNSYNCED;
    else if(now-s->last_sample_ms>=2000U) s->state=CLOCK_SERVO_HOLDOVER;
}
/**
 * @brief 记录实际估计输入并生成预置动作。
 * @param s 必须为已初始化核心。
 * @param now 本次毫秒计数，供立即提交校验。
 * @param reference A参考纳秒值。
 * @param offset B-A时差的两倍纳秒。
 * @param delay 平均时延的两倍纳秒。
 * @param a 非NULL动作输出，失败保持NONE。
 * @return true表示接受，false模拟核心拒绝。
 */
bool ClockServo_Observe(ClockServo *s,uint32_t now,uint64_t reference,int64_t offset,int64_t delay,ClockServoAction *a)
{
    ++observe_calls; assert(s==active_servo); last_reference=reference; last_offset=offset; last_delay=delay;
    *a=(ClockServoAction){CLOCK_SERVO_NONE,0,0};
    if(!observe_ok || s->state==CLOCK_SERVO_FAULT) return false;
    s->last_sample_ms=now; ++s->valid_samples; s->offset_x2_ns=offset; s->delay_x2_ns=delay;
    *a=requested; s->pending=*a; return true;
}
/**
 * @brief 验证立即原样提交，并模拟成功或故障状态。
 * @param s 非NULL核心。
 * @param now 成功时必须等于Observe毫秒。
 * @param a 非NULL原样动作，失败的NONE允许用于故障。
 * @param success 仅实际BSP成功时应为true。
 * @return 无返回值。
 */
void ClockServo_Commit(ClockServo *s,uint32_t now,const ClockServoAction *a,bool success)
{
    Event('K'); ++commit_calls;
    if(!success) { s->state=CLOCK_SERVO_FAULT; s->internal_locked=false; s->pending.kind=CLOCK_SERVO_NONE; return; }
    assert(now==s->last_sample_ms && a->kind==s->pending.kind);
    if(a->kind==CLOCK_SERVO_FREQUENCY) { s->frequency_ppb=a->frequency_ppb; s->state=CLOCK_SERVO_TRACKING; }
    else s->state=CLOCK_SERVO_ACQUIRING;
    s->pending.kind=CLOCK_SERVO_NONE;
}
/** @brief 记录清历史并保留频率。 @param s 非NULL核心。 @param now 当前毫秒。 @return 无返回值。 */
void ClockServo_Invalidate(ClockServo *s,uint32_t now)
{
    Event('V'); ++invalidate_calls; s->internal_locked=false; s->lock_count=0U;
    s->acquired=false; s->have_anchor=false; s->have_sample=false;
    s->last_sample_ms=now; if(s->state!=CLOCK_SERVO_FAULT) s->state=CLOCK_SERVO_ACQUIRING;
}
/**
 * @brief 模拟前置测量拒绝，保留最后有效时间与已确认频率。
 * @param s 非NULL核心，FAULT不改变。
 * @param now 当前毫秒，只用于推进真实超时，不作为新有效样本。
 * @return 无返回值。
 * @note 清除内部锁及连续证据；2秒保持、5秒未同步以原有效时间为基准。
 */
void ClockServo_RejectMeasurement(ClockServo *s,uint32_t now)
{
    Event('J');
    if(s->state==CLOCK_SERVO_FAULT) return;
    s->internal_locked=false; s->lock_count=0U; s->acquired=false;
    s->have_anchor=false; s->have_sample=false;
    const uint32_t elapsed=now-s->last_sample_ms;
    s->state=elapsed>=5000U ? CLOCK_SERVO_UNSYNCED :
        (elapsed>=2000U ? CLOCK_SERVO_HOLDOVER : CLOCK_SERVO_ACQUIRING);
}
/** @brief 返回替代核心的静态状态名。 @param s 非NULL核心。 @return 只读名称，无需释放。 */
const char *ClockServo_StateName(const ClockServo *s)
{
    static const char *const names[]={"ACQUIRING","TRACKING","LOCKED_INTERNAL","HOLDOVER","UNSYNCED","FAULT"};
    return names[s->state];
}

/** @brief 将所有替代依赖复位为允许操作的独立场景。 @return 无返回值。 */
static void Reset(void)
{
    deinit_ok=true; assert(TimeSync_Deinit());
    now_tick=1000U; epoch=revision=1U; captured=1000U;
    protocol_calls=observe_calls=tick_calls=commit_calls=invalidate_calls=clock_writes=estimator_calls=notify_calls=0U;
    deinit_ok=install_ok=udp_clear_ok=observe_ok=true; have_exchange=peer_reset=false;
    read_status=clean_status=write_status=BSP_PTP_OK; estimate_status=SYNC_ESTIMATOR_OK;
    hardware_frequency=0; requested=(ClockServoAction){CLOCK_SERVO_NONE,0,0}; active_servo=NULL;
    process_advance_ms=0U; capture_on_process=false;
    trace_length=0U; trace[0]=logs[0]='\0';
}
/** @brief 用最小身份替代建立指定角色。 @param role A或B。 @return 无返回值。 */
static void Init(BoardRole role)
{ BoardIdentityProfile p={0}; p.role=role; assert(TimeSync_Init(now_tick,&p,Tick,Log)); }
/** @brief 交接一个当前有效样本。 @return 无返回值。 */
static void Sample(void) { have_exchange=true; captured=now_tick; TimeSync_Process(now_tick,true); }
/** @brief 清理事件串以独立检查下一次调用。 @return 无返回值。 */
static void ClearTrace(void) { trace_length=0U; trace[0]='\0'; }

/** @brief 执行生命周期和真实调用顺序断言。 @return 全部通过为0。 */
int main(void)
{
    Reset(); Init(BOARD_ROLE_A); requested=(ClockServoAction){CLOCK_SERVO_STEP,500,0};
    Sample(); assert(protocol_calls==1U && observe_calls==0U && clock_writes==0U && tick_calls==0U);
    TimeSync_Process(now_tick,false); assert(protocol_calls==1U && clock_writes==0U);
    assert(active_servo==NULL);
    Reset(); Init(BOARD_ROLE_B); have_exchange=true; requested=(ClockServoAction){CLOCK_SERVO_STEP,500,0};
    TimeSync_Process(now_tick,false);
    assert(protocol_calls==0U && tick_calls==0U && observe_calls==0U && clock_writes==0U && have_exchange);
    puts("PASS: A runs only protocol and blocked foreground performs no transport, servo or hardware action");

    Reset(); Init(BOARD_ROLE_B); Sample();
    assert(observe_calls==1U && last_reference==UINT64_C(1000000000) && last_offset==-200 && last_delay==1000);
    assert(commit_calls==0U && clock_writes==0U);
    ClearTrace(); requested=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,-100}; Sample();
    assert(strcmp(trace,"PTUCMSNFKN")==0 && strchr(trace,'A')==NULL);
    assert(active_servo->frequency_ppb==-100 && hardware_frequency==-100 && invalidate_calls==0U);
    assert(strstr(logs,"$SYNC_ADJUST,") && strstr(logs,"FREQ_PPB=-100") && !strstr(logs,"SYNC=1"));
    ClearTrace(); requested=(ClockServoAction){CLOCK_SERVO_STEP,-500,0}; Sample();
    assert(strcmp(trace,"PTUCAMSNHKN")==0 && active_servo->frequency_ppb==-100);
    puts("PASS: matched estimate uses A reference; phase actions invalidate all consumers, continuous frequency keeps the anchor cadence");
    Reset(); Init(BOARD_ROLE_B); requested=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,20};
    write_status=BSP_PTP_UPDATE_TIMEOUT; Sample();
    assert(clock_writes==1U && active_servo->state==CLOCK_SERVO_FAULT);
    assert(strstr(trace,"FKUCAMSN")!=NULL);
    puts("PASS: a failed frequency write revokes the consumers that a successful one would have kept");

    Reset(); Init(BOARD_ROLE_B); requested=(ClockServoAction){CLOCK_SERVO_STEP,10,0}; clean_status=BSP_PTP_NOT_READY;
    Sample(); assert(clock_writes==0U && active_servo->state==CLOCK_SERVO_FAULT && commit_calls==1U);
    assert(strstr(trace,"U")==NULL);
    Reset(); Init(BOARD_ROLE_B); requested=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,50}; udp_clear_ok=false;
    Sample(); assert(clock_writes==0U && active_servo->state==CLOCK_SERVO_FAULT);
    Reset(); Init(BOARD_ROLE_B); requested=(ClockServoAction){CLOCK_SERVO_STEP,10,0}; write_status=BSP_PTP_UPDATE_TIMEOUT;
    Sample(); assert(clock_writes==1U && active_servo->state==CLOCK_SERVO_FAULT && commit_calls==1U);
    assert(strstr(trace,"CAMSNHK")!=NULL);
    puts("PASS: busy TX, active legacy callback and uncertain hardware failure produce FAULT without false success");

    Reset(); Init(BOARD_ROLE_B); have_exchange=true; captured=749U; TimeSync_Process(now_tick,true);
    assert(observe_calls==0U && estimator_calls==0U);
    have_exchange=true; captured=750U; TimeSync_Process(now_tick,true); assert(observe_calls==1U);
    have_exchange=true; captured=now_tick+1U; TimeSync_Process(now_tick,true); assert(observe_calls==1U);
    estimate_status=SYNC_ESTIMATOR_BAD_ORDER; Sample(); assert(observe_calls==1U);
    observe_ok=false; estimate_status=SYNC_ESTIMATOR_OK; Sample(); assert(clock_writes==0U);
    puts("PASS: samples older than 250ms and invalid quartets are rejected before control; core rejection never writes");

    Reset(); hardware_frequency=70; Init(BOARD_ROLE_B);
    active_servo->state=CLOCK_SERVO_LOCKED_INTERNAL; active_servo->internal_locked=true;
    active_servo->lock_count=8U; active_servo->acquired=true;
    have_exchange=true; captured=now_tick-251U; TimeSync_Process(now_tick,true);
    assert(!active_servo->internal_locked && active_servo->lock_count==0U && !active_servo->acquired);
    assert(active_servo->frequency_ppb==70 && hardware_frequency==70 && clock_writes==0U);
    Sample(); assert(!active_servo->internal_locked && active_servo->lock_count==0U);
    active_servo->state=CLOCK_SERVO_LOCKED_INTERNAL; active_servo->internal_locked=true;
    active_servo->lock_count=8U; active_servo->acquired=true;
    estimate_status=SYNC_ESTIMATOR_WINDOW_EXCEEDED; Sample();
    assert(!active_servo->internal_locked && active_servo->lock_count==0U && !active_servo->acquired);
    assert(active_servo->frequency_ppb==70 && clock_writes==0U);
    estimate_status=SYNC_ESTIMATOR_OK; Sample(); assert(active_servo->lock_count==0U);
    active_servo->internal_locked=true; active_servo->lock_count=8U;
    estimate_status=SYNC_ESTIMATOR_NEGATIVE_DELAY; Sample();
    assert(!active_servo->internal_locked && active_servo->lock_count==0U && active_servo->frequency_ppb==70);
    puts("PASS: stale or invalid quartets immediately revoke internal lock and continuity evidence while preserving actual frequency");

    for(unsigned bad_kind=0U;bad_kind<2U;++bad_kind)
    {
        Reset(); hardware_frequency=42; Init(BOARD_ROLE_B); Sample();
        const uint32_t last_valid_ms=active_servo->last_sample_ms;
        active_servo->internal_locked=true; active_servo->state=CLOCK_SERVO_LOCKED_INTERNAL;
        active_servo->lock_count=8U;
        for(unsigned i=1U;i<=24U;++i)
        {
            now_tick=last_valid_ms+i*250U; have_exchange=true;
            captured=bad_kind==0U ? now_tick-251U : now_tick;
            estimate_status=bad_kind==0U ? SYNC_ESTIMATOR_OK : SYNC_ESTIMATOR_WINDOW_EXCEEDED;
            TimeSync_Process(now_tick,true);
            assert(active_servo->last_sample_ms==last_valid_ms);
            assert(!active_servo->internal_locked && active_servo->lock_count==0U);
            assert(active_servo->frequency_ppb==42 && hardware_frequency==42 && clock_writes==0U);
            if(i>=20U) assert(active_servo->state==CLOCK_SERVO_UNSYNCED);
            else if(i>=8U) assert(active_servo->state==CLOCK_SERVO_HOLDOVER);
        }
    }
    puts("PASS: continuous stale or failed quartets retain the last valid tick and reach holdover at 2s and unsynced at 5s");

    Reset(); Init(BOARD_ROLE_B); process_advance_ms=10U; capture_on_process=true;
    requested=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,20}; Sample();
    assert(observe_calls==1U && clock_writes==1U && active_servo->last_sample_ms==1010U);
    puts("PASS: newly completed packets use refreshed HAL time rather than the stale process entry tick");

    Reset(); Init(BOARD_ROLE_B); requested=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,70}; Sample();
    ClearTrace(); requested.kind=CLOCK_SERVO_NONE; ++epoch; ++revision; TimeSync_Process(now_tick,true);
    assert(invalidate_calls==1U && active_servo->frequency_ppb==70 && hardware_frequency==70);
    assert(strstr(trace,"UCAMSNV")!=NULL);
    ++revision; TimeSync_Process(now_tick,true); assert(invalidate_calls==2U && active_servo->frequency_ppb==70);
    peer_reset=true; TimeSync_Process(now_tick,true); assert(invalidate_calls==3U && clock_writes==1U);
    hardware_frequency=71; TimeSync_Process(now_tick,true);
    assert(active_servo->state==CLOCK_SERVO_FAULT && clock_writes==1U);
    puts("PASS: external epoch and peer reset clear history but preserve committed frequency; mismatch fails safely");

    Reset(); Init(BOARD_ROLE_B); deinit_ok=false;
    assert(!TimeSync_Deinit()); Sample(); assert(protocol_calls==1U);
    BoardIdentityProfile p={0}; p.role=BOARD_ROLE_A;
    assert(!TimeSync_Init(now_tick,&p,Tick,Log)); Sample(); assert(observe_calls==2U);
    deinit_ok=true; assert(TimeSync_Deinit()); TimeSync_Process(now_tick,true); assert(protocol_calls==2U);
    Reset(); p.role=BOARD_ROLE_B; hardware_frequency=1;
    assert(TimeSync_Init(now_tick,&p,Tick,Log) && active_servo->frequency_ppb==1);
    hardware_frequency=100001; assert(!TimeSync_Init(now_tick,&p,Tick,Log));
    hardware_frequency=0; read_status=BSP_PTP_NOT_READY; assert(!TimeSync_Init(now_tick,&p,Tick,Log));
    read_status=BSP_PTP_OK; install_ok=false; assert(!TimeSync_Init(now_tick,&p,Tick,Log));
    puts("PASS: failed ownership teardown preserves service; init adopts known frequency and rejects unavailable or invalid B clocks");

    Reset(); Init(BOARD_ROLE_B); now_tick+=1000U; TimeSync_Process(now_tick,true);
    assert(strstr(logs,"$SYNC_CTRL,") && strstr(logs,"ROLE=B") && strstr(logs,"INTERNAL_LOCK=0,SYNC=0"));
    assert(strstr(logs,"OFFSET_X2_NS=") && strstr(logs,"DELAY_X2_NS=") && strstr(logs,"DRIFT_PPB="));
    assert(strstr(logs,"EPOCH=1,RATE_REV=1") && strstr(logs,"VALID=") && strstr(logs,"REJECT=") && strstr(logs,"ERRORS="));
    const size_t length=strlen(logs); TimeSync_Process(now_tick,true); assert(strlen(logs)==length);
    now_tick+=6000U; TimeSync_Process(now_tick,true); assert(active_servo->state==CLOCK_SERVO_UNSYNCED);
    puts("PASS: control logs remain bounded, rate limited, explicitly unsynchronized and advance timeout state without packets");

    Reset(); now_tick=UINT32_MAX-500U; Init(BOARD_ROLE_B); now_tick=500U; TimeSync_Process(now_tick,true);
    assert(strstr(logs,"$SYNC_CTRL,")!=NULL);
    Reset(); epoch=revision=UINT32_MAX; hardware_frequency=-100000; Init(BOARD_ROLE_B);
    active_servo->offset_x2_ns=-INT64_C(8589934591999999998);
    active_servo->delay_x2_ns=4000000; active_servo->drift_ppb=INT32_MIN;
    active_servo->state=CLOCK_SERVO_LOCKED_INTERNAL; active_servo->internal_locked=true;
    active_servo->valid_samples=UINT32_MAX;
    now_tick+=1000U; TimeSync_Process(now_tick,true);
    assert(strstr(logs,"OFFSET_X2_NS=-8589934591999999998")!=NULL);
    assert(strstr(logs,"INTERNAL_LOCK=1,SYNC=0")!=NULL && strstr(logs,"DRIFT_PPB=-2147483648")!=NULL);
    assert(strstr(logs,"EPOCH=4294967295,RATE_REV=4294967295")!=NULL);
    puts("PASS: log cadence survives tick wrap and full-width signed fields fit complete nano-safe lines");

    Reset(); Init(BOARD_ROLE_B); read_status=BSP_PTP_BAD_CONFIG; Sample();
    assert(active_servo->state==CLOCK_SERVO_FAULT && clock_writes==0U);
    read_status=BSP_PTP_OK; peer_reset=true; Sample();
    assert(active_servo->state==CLOCK_SERVO_FAULT && clock_writes==0U);
    assert(TimeSync_Init(now_tick,&(BoardIdentityProfile){.role=BOARD_ROLE_B},Tick,NULL));
    requested=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,10}; Sample();
    assert(clock_writes==1U && active_servo->frequency_ppb==10);
    assert(!TimeSync_Init(now_tick,NULL,Tick,Log));
    assert(!TimeSync_Init(now_tick,&(BoardIdentityProfile){.role=BOARD_ROLE_UNCONFIGURED},Tick,Log));
    assert(!TimeSync_Init(now_tick,&(BoardIdentityProfile){.role=BOARD_ROLE_B},NULL,Log));
    puts("PASS: clock loss faults without implicit recovery; explicit known-state reinstall supports disabled logging and rejects invalid inputs");
    return 0;
}
