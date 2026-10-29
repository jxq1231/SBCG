/**
 * @file        pps_service.h
 * @brief       声明参考板及跟随板的PPS输出资格与时效管理接口。
 * @details     A只依据本地PTP健康输出；B首次内部锁定后取得资格，纯频率更新允许有界重新资格。
 * @note        所有接口仅限串行前台；不调整伺服、不产生软件脉冲，内部资格不代表独立精度验收。
 */
#ifndef SBCG_PPS_SERVICE_H
#define SBCG_PPS_SERVICE_H

#include "BSP/board_identity.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief 只读PPS资格状态；供共同时间域门控使用，不改变输出/伺服。 */
typedef enum
{
    PPS_SERVICE_UNQUALIFIED = 0,
    PPS_SERVICE_QUALIFIED,
    PPS_SERVICE_RATE_REQUALIFY,
    PPS_SERVICE_FAULT
} PpsServiceState;

/** @brief PPS只读快照；qualified表示输出资格，不是外部示波器精度验收。 */
typedef struct
{
    bool configured;
    PpsServiceState state;
    bool qualified;
    bool faulted;
    bool armed;
    uint32_t sample_age_ms;
    uint32_t epoch;
    uint32_t emitted;
    uint32_t missed;
    uint32_t errors;
    uint32_t faults;              /**< 锁存故障次数；初始化清零，自动恢复不清零。 */
    uint32_t recoveries;          /**< FAULT后BSP重新初始化成功次数。 */
    uint32_t last_fault_status;   /**< 最近一次BSP_PpsStatus数值，供现场诊断。 */
} PpsServiceSnapshot;

/**
 * @brief 复制当前PPS资格/时效/硬件观察；NULL返回false，不改变状态。
 * @param now_ms 当前HAL毫秒；无符号差处理回绕。
 * @param out 可写快照，失败时清零。
 * @return true表示服务已配置且硬件快照可读；false其余。
 * @note 仅串行前台；不刷新样本年龄、不授予/撤销资格、不替代示波器验收。
 */
bool PpsService_GetSnapshot(uint32_t now_ms, PpsServiceSnapshot *out);

/**
 * @brief   校验PPS资源及本地PTP健康，初始化停用的输出服务。
 * @param   now_ms 当前HAL毫秒计数，允许自然回绕。
 * @param   role 已登记的A或B角色；其它角色初始化失败。
 * @param   log 可为NULL的长期有效日志回调；返回前须消费或复制临时文本。
 * @return  true表示初始化完成且输出仍停用；false表示角色、时钟或安全停用失败。
 * @note    在PtpClock初始化之后调用；只有本接口可以恢复锁存故障，不写PTP时间或频率。
 */
bool PpsService_Init(uint32_t now_ms, BoardRole role, void (*log)(const char *, int));

/**
 * @brief   同步取消输出并停用服务。
 * @return  true表示已确认安全停用；false表示停止失败并锁存FAULT。
 * @note    即使未初始化也尝试停止；成功卸载不会消除既有锁存故障。
 */
bool PpsService_Deinit(void);

/**
 * @brief   检查时钟代际和资格期限，向BSP传递剩余毫秒预算并限速报告状态。
 * @param   now_ms 当前HAL毫秒计数，调度间隔应小于uint32半范围。
 * @param   schedule_allowed 是否允许安排新目标；false仍检查安全、撤销资格和消费硬件观察。
 * @return  无返回值。
 * @note    每轮主循环调用，DHT启动低电平期间仍须调度；日志最多每1000ms一行。
 */
void PpsService_Process(uint32_t now_ms, bool schedule_allowed);

/**
 * @brief   消费一份伺服刚接受的观测，取得或撤销B板输出资格。
 * @param   now_ms 观测处理时的HAL毫秒计数。
 * @param   captured_ms 板间交换完成时的HAL毫秒计数，作为样本实际年龄起点。
 * @param   internal_locked 本次Observe之后、动作Commit之前的内部锁定证据。
 * @param   drift_valid 当前频率分段内的漂移是否已有有效估计。
 * @param   offset_x2_ns B-A时差的两倍纳秒，资格要求在[-20000,20000]。
 * @param   delay_x2_ns 平均路径时延的两倍纳秒，资格要求在[0,4000000]。
 * @param   drift_ppb 有效时使用的残余漂移ppb，资格要求在[-500,500]。
 * @return  无返回值。
 * @note    只接受串行前台已通过估计/伺服校验的样本；拒绝路径须调用Invalidate。
 */
void PpsService_Observe(uint32_t now_ms, uint32_t captured_ms, bool internal_locked,
    bool drift_valid, int64_t offset_x2_ns, int64_t delay_x2_ns, int32_t drift_ppb);

/**
 * @brief   在拒绝、对端重置、相位覆盖或同步故障前同步撤销PPS资格。
 * @param   now_ms 当前HAL毫秒计数；本接口不刷新样本时效。
 * @return  true表示输出已确认停止；false表示停止失败或已有FAULT，调用方不得继续相位写入。
 * @note    清除频率待提交和重新资格证据，保留FAULT；成功时记录当前代际供后续新样本重新获取。
 */
bool PpsService_Invalidate(uint32_t now_ms);

/**
 * @brief   在实际频率命令前决定是否保留已取得的输出资格。
 * @param   now_ms 当前HAL毫秒计数，执行时先检查样本和重新资格截止时间。
 * @param   previous_ppb 硬件原来已确认的绝对频率ppb。
 * @param   next_ppb 本次将应用的绝对频率ppb。
 * @return  true表示安全继续频率命令；false表示输出无法确认停止或已有FAULT。
 * @note    仅已具资格且频差不超过1125ppb可保持；重新资格期间第二次调频撤销，未具资格仍允许调频。
 */
bool PpsService_BeforeFrequency(uint32_t now_ms, int32_t previous_ppb, int32_t next_ppb);

/**
 * @brief   记录实际频率命令结果，成功且允许保持时开始4000ms重新资格期限。
 * @param   now_ms 命令完成后的当前HAL毫秒计数。
 * @param   success 仅BSP确认成功且伺服提交成功时为true。
 * @return  无返回值。
 * @note    必须与BeforeFrequency在同一串行前台动作内配对；失败或未知代际进入FAULT并停止输出。
 */
void PpsService_AfterFrequency(uint32_t now_ms, bool success);

#endif
