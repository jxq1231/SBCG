/**
 * @file        clock_servo.c
 * @brief       用恒定频率分段斜率和两秒相位反馈驱动B时钟的纯C控制器。
 * @details     首次三个一致样本后可作大相位对齐；其后先估计至少一秒的
 *              残余漂移，再用f_new=f_applied-drift-offset_ns/tau更新绝对ppb。
 *              获取阶段tau=2秒；锁定后的8秒更新窗口使用tau=16秒，
 *              避免把快环增益直接用于长保持周期引起离散振荡。
 *              频率分段内不累积积分项；限幅后不会产生积分饱和。
 * @note        已锁且门槛满足时至少保持频率八秒，给音频模型留出稳定窗口。
 *              样本质量、时钟代际和硬件动作提交由上层配合，不做硬件访问。
 */
#include "Sync/clock_servo.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

#define MAX_TIMESTAMP_NS UINT64_C(4294967295999999999)
#define MAX_OFFSET_X2_NS INT64_C(8589934591999999998)
#define MAX_DELAY_X2_NS INT64_C(4000000)
#define MAX_JUMP_NS INT64_C(250000)
#define STEP_THRESHOLD_NS INT64_C(1000000)
#define LOCK_OFFSET_NS INT64_C(10000)
#define LOCK_DRIFT_PPB INT32_C(500)
#define HOLDOVER_MS UINT32_C(2000)
#define UNSYNCED_MS UINT32_C(5000)
#define MIN_RATE_MS UINT32_C(1000)
#define LOCKED_RATE_MS UINT32_C(8000)
#define MIN_SLOPE_NS UINT64_C(1000000000)
#define MAX_REFERENCE_GAP_NS UINT64_C(2000000000)
#define REFERENCE_BUDGET_NS UINT64_C(100000000)

/**
 * @brief 递增可观测计数，最大值饱和避免长期运行回绕。
 * @param value 当前状态持有的非NULL输入/输出计数器，不保存指针。
 * @return 无返回值。
 * @note 仅由前台状态处理调用，无硬件副作用。
 */
static void Increment(uint32_t *value)
{
    if (*value != UINT32_MAX) ++*value;
}

/**
 * @brief 清除锁定证据；不改变已应用频率。
 * @param s 调用方持有的非NULL输入/输出状态，不保存指针。
 * @return 无返回值。
 * @note 前台内部调用，只清除连续样本证据。
 */
static void ClearLock(ClockServo *s)
{
    s->lock_count=0;
    s->internal_locked=false;
}

/**
 * @brief 清除估计窗口和未提交动作，用于跳时或重新获取。
 * @param s 调用方持有的非NULL输入/输出状态，不保存指针。
 * @return 无返回值。
 * @note 前台内部调用；诊断计数和已应用频率保持，无硬件副作用。
 */
static void ResetHistory(ClockServo *s)
{
    ClearLock(s);
    s->have_sample=false;
    s->have_anchor=false;
    s->acquired=false;
    s->drift_valid=false;
    s->drift_ppb=0;
    s->bootstrap_count=0;
    s->pending=(ClockServoAction){CLOCK_SERVO_NONE,0,0};
}

/**
 * @brief 记录拒绝并中断连续锁定证据。
 * @param s 调用方持有的非NULL输入/输出状态，不保存指针。
 * @return false，便于直接返回测量拒绝结果。
 * @note 前台拒绝路径调用，增加拒绝计数；FAULT由外层提前处理。
 */
static bool Reject(ClockServo *s)
{
    Increment(&s->rejected_samples);
    ClearLock(s);
    if (s->state==CLOCK_SERVO_LOCKED_INTERNAL) s->state=CLOCK_SERVO_TRACKING;
    return false;
}

/**
 * @brief 检查两个有符号值的差是否在正负限值内。
 * @param difference 已由调用方证明不会溢出的差值。
 * @param limit 非负限值，单位与difference相同。
 * @return true表示在闭区间内；不对INT64_MIN求负。
 * @note 纯数值比较，不读写状态。
 */
static bool Within(int64_t difference,int64_t limit)
{
    return difference>=-limit && difference<=limit;
}

/**
 * @brief 将期望绝对频率限幅到公开控制范围。
 * @param frequency_ppb 64位中间频率，单位ppb。
 * @return 可交给硬件层的有界绝对ppb值。
 * @note 纯算术，不执行硬件频率更新。
 */
static int32_t ClampFrequency(int64_t frequency_ppb)
{
    if (frequency_ppb>CLOCK_SERVO_MAX_FREQUENCY_PPB)
        return CLOCK_SERVO_MAX_FREQUENCY_PPB;
    if (frequency_ppb<-CLOCK_SERVO_MAX_FREQUENCY_PPB)
        return -CLOCK_SERVO_MAX_FREQUENCY_PPB;
    return (int32_t)frequency_ppb;
}

/**
 * @brief 前台按标称0ppb重建纯C状态，不写硬件。
 * @param s 调用方拥有的输出状态，可为NULL；不保存指针。
 * @param now_ms 当前单调毫秒计数，允许uint32回绕。
 * @return 无返回值；NULL无操作。
 * @note 外部须先确认实际硬件为0ppb；非零频率重入使用InitKnown。
 */
void ClockServo_Init(ClockServo *s,uint32_t now_ms)
{
    (void)ClockServo_InitKnown(s,now_ms,0);
}

/**
 * @brief 前台采用已确认实际频率，清除先前测量、动作和故障状态。
 * @param s 调用方持有的输出状态，可为NULL；不保存外部指针。
 * @param now_ms 当前单调毫秒计数，允许uint32回绕。
 * @param actual_frequency_ppb 从有效BSP硬件状态取得的绝对ppb，允许±100000。
 * @return 有效输入返回true并进入ACQUIRING；NULL或频率越界返回false。
 * @note 越界输出FAULT且不发动作；外部必须先确认无待完成命令和时钟有效。
 */
bool ClockServo_InitKnown(ClockServo *s,uint32_t now_ms,int32_t actual_frequency_ppb)
{
    if (s==NULL) return false;
    memset(s,0,sizeof(*s));
    s->last_sample_ms=now_ms;
    s->last_frequency_ms=now_ms;
    if (actual_frequency_ppb>CLOCK_SERVO_MAX_FREQUENCY_PPB ||
        actual_frequency_ppb<-CLOCK_SERVO_MAX_FREQUENCY_PPB)
    {
        s->state=CLOCK_SERVO_FAULT;
        return false;
    }
    s->frequency_ppb=actual_frequency_ppb;
    s->state=CLOCK_SERVO_ACQUIRING;
    return true;
}

/**
 * @brief 丢弃旧对端的样本窗口，保留成功确认的频率及累计诊断数。
 * @param s 调用方持有的输入/输出状态，可为NULL；不保存指针。
 * @param now_ms 重新获取起点的单调毫秒计数，允许uint32回绕。
 * @return 无返回值；NULL无操作，FAULT不解除。
 * @note 只在前台对端重启/代际失效路径调用；有未确认动作时转FAULT。
 */
void ClockServo_Invalidate(ClockServo *s,uint32_t now_ms)
{
    if (s==NULL || s->state==CLOCK_SERVO_FAULT) return;
    /* 有动作尚未确认时丢弃会隐瞒硬件实际状态，必须显式故障。 */
    if (s->pending.kind!=CLOCK_SERVO_NONE)
    {
        ResetHistory(s);
        s->state=CLOCK_SERVO_FAULT;
        return;
    }
    ResetHistory(s);
    s->last_sample_ms=now_ms;
    s->state=CLOCK_SERVO_ACQUIRING;
}

/**
 * @brief 记录质量拒绝并清除旧估计证据，保留真实最后有效样本时刻。
 * @param s 调用方持有的输入/输出状态，可为NULL，不保存指针。
 * @param now_ms 当前单调毫秒计数，允许uint32回绕。
 * @return 无返回值；NULL无操作，既有FAULT不改变。
 * @note 前台质量拒绝路径使用；保留已确认频率，按最后有效样本推进2/5秒超时。
 *       若存在待确认动作，实际硬件状态可能未知，转FAULT而非假定未执行。
 */
void ClockServo_RejectMeasurement(ClockServo *s,uint32_t now_ms)
{
    if (s==NULL || s->state==CLOCK_SERVO_FAULT) return;
    Increment(&s->rejected_samples);
    bool pending=s->pending.kind!=CLOCK_SERVO_NONE;
    ResetHistory(s);
    if (pending)
    {
        s->state=CLOCK_SERVO_FAULT;
        return;
    }
    s->state=CLOCK_SERVO_ACQUIRING;
    ClockServo_Tick(s,now_ms);
}

/**
 * @brief 通过无符号差处理毫秒回绕，推进保持及未同步状态。
 * @param s 调用方持有的输入/输出状态，可为NULL；不保存指针。
 * @param now_ms 主循环当前毫秒计数；有效调度间隔小于计数半范围。
 * @return 无返回值；不生成动作，不更改已确认频率。
 * @note 前台调用；2秒撤锁/清历史，5秒未同步，待确认动作超时转FAULT。
 */
void ClockServo_Tick(ClockServo *s,uint32_t now_ms)
{
    if (s==NULL || s->state==CLOCK_SERVO_FAULT) return;
    uint32_t elapsed_ms=now_ms-s->last_sample_ms;
    if (elapsed_ms>=HOLDOVER_MS)
    {
        if (s->pending.kind!=CLOCK_SERVO_NONE)
        {
            ResetHistory(s);
            s->state=CLOCK_SERVO_FAULT;
            return;
        }
        ResetHistory(s);
        s->state=elapsed_ms>=UNSYNCED_MS ? CLOCK_SERVO_UNSYNCED : CLOCK_SERVO_HOLDOVER;
    }
}

/**
 * @brief 参考A时刻须递增，且与本地毫秒调度历时相差不超过100ms。
 * @param s 调用方持有的非NULL只读状态，已接受过样本，不保存指针。
 * @param now_ms 新样本的单调毫秒时刻，允许回绕。
 * @param reference_ns 新的参考时刻，已验证范围。
 * @return true表示同一连续参考分段；false不改变状态。
 * @note 前台检查；A时刻均已通过范围校验，减法不会发生无符号下溢。
 */
static bool ReferenceContinuous(const ClockServo *s,uint32_t now_ms,uint64_t reference_ns)
{
    if (reference_ns<=s->reference_ns) return false;
    uint64_t elapsed_ns=reference_ns-s->reference_ns;
    uint64_t tick_elapsed_ns=(uint64_t)(now_ms-s->last_sample_ms)*1000000;
    if (elapsed_ns>MAX_REFERENCE_GAP_NS) return false;
    uint64_t difference_ns=elapsed_ns>tick_elapsed_ns ? elapsed_ns-tick_elapsed_ns : tick_elapsed_ns-elapsed_ns;
    return difference_ns<=REFERENCE_BUDGET_NS;
}

/**
 * @brief 在同一个已应用频率分段内估计残余漂移，避免把变频前后混合。
 * @param s 调用方持有的非NULL输入/输出状态，已写入新参考时刻和时差，不保存指针。
 * @return 无返回值；至少一秒后drift_valid才为true。
 * @note 两个offset_ns各不超MAX_TIMESTAMP_NS，减法小于INT64_MAX；
 *       乘法前再次限差，避免极端输入使差分乘1e9溢出。
 */
static void EstimateDrift(ClockServo *s)
{
    int64_t offset_ns=s->offset_x2_ns/2;
    if (!s->have_anchor)
    {
        s->anchor_reference_ns=s->reference_ns;
        s->anchor_offset_ns=offset_ns;
        s->have_anchor=true;
        return;
    }
    uint64_t elapsed_ns=s->reference_ns-s->anchor_reference_ns;
    if (elapsed_ns<MIN_SLOPE_NS) return;
    int64_t difference_ns=offset_ns-s->anchor_offset_ns;
    if (!Within(difference_ns,INT64_C(1000000000)))
    {
        s->drift_valid=false;
        return;
    }
    int64_t drift_ppb=difference_ns*INT64_C(1000000000)/(int64_t)elapsed_ns;
    if (drift_ppb>INT32_MAX || drift_ppb<INT32_MIN)
    {
        s->drift_valid=false;
        return;
    }
    s->drift_ppb=(int32_t)drift_ppb;
    s->drift_valid=true;
}

/**
 * @brief 用真实残余漂移窗口推进连续八样本锁定及频率控制。
 * @param s 调用方持有的非NULL输入/输出状态，已写入新样本，不保存指针。
 * @param now_ms 新样本单调毫秒时刻。
 * @param a 调用方持有的非NULL输出；调用前为NONE，不保存指针。
 * @return 无返回值；硬件行为只通过a请求。
 * @note 仅前台内部调用，输出动作尚未应用，不更新已确认频率。
 */
static void Control(ClockServo *s,uint32_t now_ms,ClockServoAction *a)
{
    int64_t offset_ns=s->offset_x2_ns/2;
    bool qualified=s->drift_valid && Within(s->offset_x2_ns,LOCK_OFFSET_NS*2) &&
        Within(s->drift_ppb,LOCK_DRIFT_PPB);
    if (qualified)
    {
        if (s->lock_count<8) ++s->lock_count;
        s->internal_locked=s->lock_count>=8;
    }
    else ClearLock(s);
    s->state=s->internal_locked ? CLOCK_SERVO_LOCKED_INTERNAL : CLOCK_SERVO_TRACKING;
    if (!s->drift_valid) return;
    /* 锁定候选期保持同一频率，使八样本条件具有连续分段意义。 */
    if (qualified && !s->internal_locked) return;
    uint32_t interval_ms=s->internal_locked ? LOCKED_RATE_MS : MIN_RATE_MS;
    if ((uint32_t)(now_ms-s->last_frequency_ms)<interval_ms) return;
    int64_t phase_tau_s=s->internal_locked ? 16 : 2;
    int32_t desired_ppb=ClampFrequency((int64_t)s->frequency_ppb-s->drift_ppb-offset_ns/phase_tau_s);
    if (desired_ppb==s->frequency_ppb) return;
    *a=(ClockServoAction){CLOCK_SERVO_FREQUENCY,0,desired_ppb};
}

/**
 * @brief 前台消费一个已通过四时刻校验的测量，输出至多一个动作请求。
 * @param s 调用方持有的输入/输出状态，须非NULL且已初始化，不保存指针。
 * @param now_ms 本次处理的单调毫秒计数，允许uint32回绕，倒退则拒绝。
 * @param reference_ns A参考硬件时刻，纳秒，须单调且符合本地调度历时预算。
 * @param offset_x2_ns B-A两倍纳秒；正值代表B领先，产生负向校正。
 * @param delay_x2_ns 平均路径时延的两倍纳秒，允许0至4000000。
 * @param a 非NULL调用方输出，每次先写NONE，函数不保存指针。
 * @return true表示接受样本；false表示参数、质量、参考、故障或待提交状态拒绝。
 * @note 调用方先校验交换历时不超过100ms；请求尚未完成时不处理新样本。
 *       接受样本不等同锁定，动作只有实际硬件成功Commit后才计为应用。
 */
bool ClockServo_Observe(ClockServo *s,uint32_t now_ms,uint64_t reference_ns,
    int64_t offset_x2_ns,int64_t delay_x2_ns,ClockServoAction *a)
{
    if (a==NULL) return false;
    *a=(ClockServoAction){CLOCK_SERVO_NONE,0,0};
    if (s==NULL || s->state==CLOCK_SERVO_FAULT) return false;
    if (s->have_sample && (uint32_t)(now_ms-s->last_sample_ms)>INT32_MAX)
    {
        ResetHistory(s);
        s->state=CLOCK_SERVO_UNSYNCED;
        return Reject(s);
    }
    ClockServo_Tick(s,now_ms);
    if (s->state==CLOCK_SERVO_FAULT) return false;
    if (s->pending.kind!=CLOCK_SERVO_NONE) return Reject(s);
    if (reference_ns>MAX_TIMESTAMP_NS || offset_x2_ns>MAX_OFFSET_X2_NS ||
        offset_x2_ns<-MAX_OFFSET_X2_NS || delay_x2_ns<0 || delay_x2_ns>MAX_DELAY_X2_NS)
        return Reject(s);
    if (s->have_sample && !ReferenceContinuous(s,now_ms,reference_ns))
    {
        ResetHistory(s);
        s->state=CLOCK_SERVO_ACQUIRING;
        return Reject(s);
    }
    int64_t offset_ns=offset_x2_ns/2;
    if (s->have_sample && !Within(offset_ns-s->offset_x2_ns/2,MAX_JUMP_NS))
    {
        /* 单个大相位只重启三个一致样本的获取，不直接执行覆盖。 */
        ResetHistory(s);
        s->state=CLOCK_SERVO_ACQUIRING;
    }
    s->have_sample=true;
    s->last_sample_ms=now_ms;
    s->reference_ns=reference_ns;
    s->offset_x2_ns=offset_x2_ns;
    s->delay_x2_ns=delay_x2_ns;
    Increment(&s->valid_samples);
    EstimateDrift(s);
    if (!s->acquired)
    {
        if (s->bootstrap_count<3) ++s->bootstrap_count;
        if (s->bootstrap_count<3) return true;
        s->acquired=true;
        if (!Within(offset_x2_ns,STEP_THRESHOLD_NS*2))
        {
            *a=(ClockServoAction){CLOCK_SERVO_STEP,-offset_ns,0};
            s->pending=*a;
            return true;
        }
    }
    Control(s,now_ms,a);
    if (a->kind!=CLOCK_SERVO_NONE) s->pending=*a;
    return true;
}

/**
 * @brief 根据实际硬件执行结果确认待提交动作，成功后重新建立估计分段。
 * @param s 调用方持有的输入/输出状态，可为NULL；不保存指针。
 * @param now_ms 确认时单调毫秒计数，须在请求样本后100ms内。
 * @param a 非NULL只读请求，须与pending逐字段相同；不保存指针。
 * @param success 仅硬件已确认成功时为true；false表示执行失败或结果不确定。
 * @return 无返回值；NULL状态无操作，错配/过期/失败进入FAULT。
 * @note 前台请求执行后立即调用；成功STEP清历史，成功变频用下个样本建锚。
 *       FAULT不能用后续样本解除，必须外部确认硬件后重新初始化。
 */
void ClockServo_Commit(ClockServo *s,uint32_t now_ms,const ClockServoAction *a,bool success)
{
    if (s==NULL || s->state==CLOCK_SERVO_FAULT) return;
    if (!success || a==NULL || s->pending.kind==CLOCK_SERVO_NONE ||
        a->kind!=s->pending.kind || a->phase_ns!=s->pending.phase_ns ||
        a->frequency_ppb!=s->pending.frequency_ppb ||
        (uint32_t)(now_ms-s->last_sample_ms)>100)
    {
        ResetHistory(s);
        s->state=CLOCK_SERVO_FAULT;
        return;
    }
    if (a->kind==CLOCK_SERVO_STEP)
    {
        ResetHistory(s);
        s->last_sample_ms=now_ms;
        s->state=CLOCK_SERVO_ACQUIRING;
    }
    else
    {
        s->frequency_ppb=a->frequency_ppb;
        s->last_frequency_ms=now_ms;
        s->pending=(ClockServoAction){CLOCK_SERVO_NONE,0,0};
        /* 硬件命令可能跨毫秒；由后续首个新样本建立恒定频率分段。 */
        s->have_anchor=false;
        s->drift_valid=false;
        ClearLock(s);
        s->state=CLOCK_SERVO_TRACKING;
    }
}

/**
 * @brief 将只读伺服状态转换为机器可读状态名。
 * @param s 调用方持有的只读状态，可为NULL；不保存指针。
 * @return 静态字符串，无需释放；NULL或未知状态返回FAULT。
 * @note 无状态变化、无硬件访问，LOCKED_INTERNAL仅为协议内部估计。
 */
const char *ClockServo_StateName(const ClockServo *s)
{
    if (s==NULL) return "FAULT";
    switch(s->state)
    {
    case CLOCK_SERVO_ACQUIRING: return "ACQUIRING";
    case CLOCK_SERVO_TRACKING: return "TRACKING";
    case CLOCK_SERVO_LOCKED_INTERNAL: return "LOCKED_INTERNAL";
    case CLOCK_SERVO_HOLDOVER: return "HOLDOVER";
    case CLOCK_SERVO_UNSYNCED: return "UNSYNCED";
    default: return "FAULT";
    }
}
