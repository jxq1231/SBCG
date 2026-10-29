/**
 * @file        clock_servo.h
 * @brief       B从钟纯C伺服的状态、动作及提交接口。
 * @details     输入已配对的硬件四时刻估计，输出受限相位或绝对频率请求。
 * @note        前台单一调用者使用；不访问HAL。成功提交前不认为动作已应用。
 */
#ifndef SBCG_CLOCK_SERVO_H
#define SBCG_CLOCK_SERVO_H

#include <stdbool.h>
#include <stdint.h>

#define CLOCK_SERVO_MAX_FREQUENCY_PPB INT32_C(100000)

/** @brief 伺服动作；频率为相对标称值的绝对ppb目标。 */
typedef enum { CLOCK_SERVO_NONE, CLOCK_SERVO_STEP, CLOCK_SERVO_FREQUENCY }
    ClockServoActionKind;

/** @brief 正相位推进B，正频率加快B；仅kind对应的参数有效。 */
typedef struct
{
    ClockServoActionKind kind;
    int64_t phase_ns;
    int32_t frequency_ppb;
} ClockServoAction;

/** @brief 内部估计状态，不代表独立外部测量验收通过。 */
typedef enum
{
    CLOCK_SERVO_ACQUIRING, CLOCK_SERVO_TRACKING, CLOCK_SERVO_LOCKED_INTERNAL,
    CLOCK_SERVO_HOLDOVER, CLOCK_SERVO_UNSYNCED, CLOCK_SERVO_FAULT
} ClockServoState;

/**
 * @brief 用户持有的固定大小状态；字段可读，修改只能通过接口。
 * @note offset_x2_ns为B-A的两倍纳秒；drift_ppb为当前已应用频率下的残余漂移。
 *       frequency_ppb只记录成功提交值；Init要求外部已恢复标称频率0ppb。
 */
typedef struct
{
    ClockServoState state;
    int64_t offset_x2_ns, delay_x2_ns;
    int32_t drift_ppb, frequency_ppb;
    uint32_t valid_samples, rejected_samples, lock_count;
    bool internal_locked;
    uint32_t last_sample_ms, last_frequency_ms, bootstrap_count;
    uint64_t reference_ns, anchor_reference_ns;
    int64_t anchor_offset_ns;
    bool have_sample, have_anchor, acquired, drift_valid;
    ClockServoAction pending;
} ClockServo;

/**
 * @brief 初始化伺服并清除故障；不操作实际时钟。
 * @param servo 非NULL输出；可重用，函数不保存外部指针。
 * @param now_ms 单调毫秒计数，允许uint32回绕，调用间隔须小于半个计数范围。
 * @return 无返回值；servo为NULL时无操作。
 * @note 外部必须先确认硬件已为0ppb，不能以本接口掩盖未知硬件状态。
 */
void ClockServo_Init(ClockServo *servo, uint32_t now_ms);

/**
 * @brief 用刚从BSP确认的实际频率重新初始化纯C状态，不写硬件。
 * @param servo 非NULL输出状态；初始化清空旧样本、动作和故障。
 * @param now_ms 当前单调毫秒计数，允许uint32回绕。
 * @param actual_frequency_ppb 已从有效硬件读回的绝对频率，限±100000ppb。
 * @return true表示进入ACQUIRING并采用实际频率；NULL返回false，越界进入FAULT。
 * @note 调用方须确认硬件时钟有效和不存在未完成命令；不能用猜测值解除FAULT。
 */
bool ClockServo_InitKnown(ClockServo *servo,uint32_t now_ms,
    int32_t actual_frequency_ppb);

/**
 * @brief 过滤一次测量并生成至多一个待提交动作。
 * @param servo 非NULL、已初始化状态；仅前台调用。
 * @param now_ms 单调毫秒计数，支持uint32回绕。
 * @param reference_ns 对应样本的A参考时刻，纳秒，须递增且符合毫秒历时预算。
 * @param offset_x2_ns B-A时差的两倍纳秒，正值需要减慢或后移B。
 * @param delay_x2_ns 平均路径时延的两倍纳秒，范围0至4000000。
 * @param action 非NULL输出；每次先置NONE，不保存指针。
 * @return true表示接受测量；false表示拒绝，action仍为NONE。
 * @note 上层先验证四时刻及交换历时不超过100ms；本接口没有四时刻输入。
 *       有待提交动作时拒绝新测量。动作必须原样立即提交；不在IRQ中执行。
 */
bool ClockServo_Observe(ClockServo *servo, uint32_t now_ms,
    uint64_t reference_ns, int64_t offset_x2_ns, int64_t delay_x2_ns,
    ClockServoAction *action);

/**
 * @brief 确认本次请求的实际硬件执行结果。
 * @param servo 非NULL状态；不保存外部指针。
 * @param now_ms 提交时单调毫秒计数，须在Observe后100ms内的同一前台周期。
 * @param action 非NULL且与待提交动作逐字段相同；NONE不需要提交。
 * @param success 仅实际硬件确认成功时为true；失败或不匹配进入FAULT。
 * @return 无返回值；FAULT只允许由外部恢复后Init解除。
 */
void ClockServo_Commit(ClockServo *servo, uint32_t now_ms,
    const ClockServoAction *action, bool success);

/**
 * @brief 主循环推进超时状态；2秒撤锁保持频率，5秒转未同步。
 * @param servo 可为NULL的状态；NULL无操作。
 * @param now_ms 当前单调毫秒计数，支持uint32回绕。
 * @return 无返回值；不产生硬件动作。
 */
void ClockServo_Tick(ClockServo *servo, uint32_t now_ms);

/**
 * @brief 对端重启或配对代际失效时丢弃估计历史，保留已确认频率。
 * @param servo 可为NULL的状态；FAULT保持不变。
 * @param now_ms 当前单调毫秒计数，作为重新获取起点。
 * @return 无返回值；不生成动作，需由新样本重新积累。
 */
void ClockServo_Invalidate(ClockServo *servo, uint32_t now_ms);

/**
 * @brief 拒绝质量不合格的测量，撤锁及清历史但不刷新最后有效样本时间。
 * @param servo 调用方持有的输入/输出状态，可为NULL；不保存指针。
 * @param now_ms 当前单调毫秒计数，允许uint32回绕，用于真实有效样本超时判断。
 * @return 无返回值；NULL无操作，FAULT保持。
 * @note 前台调用；增加拒绝计数并保留已确认频率。待确认动作存在时进入FAULT，
 *       不假定硬件回滚；持续拒绝仍按最后有效样本的2秒/5秒进入保持/未同步。
 */
void ClockServo_RejectMeasurement(ClockServo *servo,uint32_t now_ms);

/**
 * @brief 取得静态存储的状态名。
 * @param servo 可为NULL的只读状态；不保存指针。
 * @return 字符串无需释放；NULL或未知枚举返回FAULT。
 */
const char *ClockServo_StateName(const ClockServo *servo);

#endif
