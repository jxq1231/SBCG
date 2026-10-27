/**
 * @file        test_pps_service.c
 * @brief       在主机上验证真实PPS服务的资格边界和同步停机契约。
 * @details     BSP替身仅观察预算/停止调用和注入硬件结果，不模拟或证明引脚边沿。
 * @note        测试无HAL依赖；不访问实物、不写固件PTP时间。
 */
#include "Sync/pps_service.h"
#include "BSP/bsp_pps.h"
#include "BSP/bsp_ptp.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static BSP_PpsStatus init_status, stop_status, process_status;
static BSP_PtpStatus read_status;
static BSP_PpsSnapshot snapshot;
static uint32_t epoch = 1U, revision = 1U, last_budget, calls, stops;
static bool last_allowed;
static int32_t actual_frequency;
static char last_log[256];
static unsigned log_count;

/** @brief 返回资源校验结果并记录初始化后的安全低状态；无参数，返回注入的BSP状态。 */
BSP_PpsStatus BSP_PPS_Init(void)
{
    memset(&snapshot, 0, sizeof snapshot);
    snapshot.configured = init_status == BSP_PPS_OK;
    return init_status;
}
/** @brief 记录安全停机请求；无参数，返回注入状态，失败时故意保留armed供检查。 */
BSP_PpsStatus BSP_PPS_Stop(void)
{
    ++stops;
    if (stop_status == BSP_PPS_OK) { snapshot.armed = false; }
    return stop_status;
}
/**
 * @brief 记录服务提供的排程预算。
 * @param valid_for_ms 剩余资格毫秒。
 * @param schedule_allowed 是否允许新目标。
 * @return 注入的BSP结果；不产生真实脉冲。
 */
BSP_PpsStatus BSP_PPS_Process(uint32_t valid_for_ms, bool schedule_allowed)
{
    last_budget = valid_for_ms; last_allowed = schedule_allowed; ++calls;
    if (valid_for_ms == 0U) { snapshot.armed = false; }
    else if (schedule_allowed) { snapshot.armed = true; snapshot.target_seconds = 2U; }
    return process_status;
}
/** @brief 复制测试快照到非NULL的out；返回复制是否成功，不访问硬件。 */
bool BSP_PPS_GetSnapshot(BSP_PpsSnapshot *out)
{ if (out == NULL) { return false; } *out = snapshot; return true; }
/** @brief 返回测试当前时间代际；无参数，无硬件副作用。 */
uint32_t BSP_PTP_GetEpoch(void) { return epoch; }
/** @brief 返回测试当前速率代际；无参数，无硬件副作用。 */
uint32_t BSP_PTP_GetRateRevision(void) { return revision; }
/** @brief 返回已确认测试频率ppb；无参数，不修改状态。 */
int32_t BSP_PTP_GetFrequencyPpb(void) { return actual_frequency; }
/** @brief 将测试本地时间写入非NULL的time；返回注入的读时状态，不产生新测量时钟。 */
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *time)
{
    if (read_status == BSP_PTP_OK) { time->seconds = 1U; time->nanoseconds = 0U; }
    return read_status;
}
/** @brief 复制length字节临时日志line；检查长度边界，日志内容归测试静态缓冲所有。 */
static void Log(const char *line, int length)
{
    assert(length > 0 && (size_t)length < sizeof last_log);
    memcpy(last_log, line, (size_t)length); last_log[length] = '\0'; ++log_count;
}
/** @brief 重置BSP注入状态并初始化指定role；now_ms为HAL测试时刻，失败直接终止断言。 */
static void Reset(BoardRole role, uint32_t now_ms)
{
    init_status = stop_status = process_status = BSP_PPS_OK; read_status = BSP_PTP_OK;
    epoch = revision = 1U; actual_frequency = 0; calls = stops = last_budget = 0U;
    log_count = 0U; last_log[0] = '\0';
    assert(PpsService_Init(now_ms, role, Log));
    assert(!snapshot.armed && calls == 0U);
}
/** @brief 提交带有效漂移及内部锁定的边界合格观测；now_ms也是其完成时刻，无返回值。 */
static void Locked(uint32_t now_ms)
{ PpsService_Observe(now_ms, now_ms, true, true, 20000, 4000000, 500); }
/** @brief 执行一次真实服务处理并断言传递的budget；now_ms允许回绕，allowed验证DHT门控语义。 */
static void Budget(uint32_t now_ms, bool allowed, uint32_t budget)
{
    unsigned before = calls;
    PpsService_Process(now_ms, allowed);
    assert(calls > before && last_budget == budget && last_allowed == allowed);
}
/** @brief 验证初始化、角色门控和首次锁定；无参数，无返回值。 */
static void TestAcquisition(void)
{
    Reset(BOARD_ROLE_B, 0U); Budget(0U, true, 0U);
    PpsService_Observe(1U, 1U, false, true, 0, 0, 0); Budget(1U, true, 0U);
    PpsService_Observe(2U, 2U, true, false, 0, 0, 0); Budget(2U, true, 0U);
    Locked(3U); Budget(3U, true, 2000U);
    assert(snapshot.armed);
    Reset(BOARD_ROLE_A, 0U); Budget(0U, true, 2000U); Budget(100000U, true, 2000U);
    assert(!PpsService_Init(0U, BOARD_ROLE_UNCONFIGURED, Log));
    assert(!snapshot.armed);
}
/** @brief 验证完成时刻年龄及毫秒回绕边界；无参数，无返回值。 */
static void TestFreshness(void)
{
    Reset(BOARD_ROLE_B, 0U); Locked(0U); Budget(1999U, false, 1U);
    uint32_t before = stops;
    Budget(2000U, false, 0U); assert(stops > before && !snapshot.armed);
    Locked(2001U); Budget(2001U, true, 2000U);
    PpsService_Observe(4001U, 2001U, true, true, 0, 0, 0); Budget(4001U, true, 0U);
    Reset(BOARD_ROLE_B, UINT32_MAX - 100U); Locked(UINT32_MAX - 100U);
    Budget(1898U, true, 1U); Budget(1899U, true, 0U);
}
/** @brief 验证质量上/下边界及溢出安全；无参数，无返回值。 */
static void TestQuality(void)
{
    const int64_t offsets[] = {20001, -20001, INT64_MIN, INT64_MAX};
    for (unsigned i = 0U; i < sizeof offsets / sizeof offsets[0]; ++i)
    {
        Reset(BOARD_ROLE_B, 0U); Locked(0U); Budget(0U, true, 2000U);
        PpsService_Observe(1U, 1U, true, true, offsets[i], 0, 0);
        assert(!snapshot.armed); Budget(1U, true, 0U);
    }
    const int32_t drifts[] = {501, -501, INT32_MIN, INT32_MAX};
    for (unsigned i = 0U; i < sizeof drifts / sizeof drifts[0]; ++i)
    {
        Reset(BOARD_ROLE_B, 0U); Locked(0U); Budget(0U, true, 2000U);
        PpsService_Observe(1U, 1U, false, true, 0, 0, drifts[i]);
        Budget(1U, true, 0U);
    }
    for (int64_t delay = -1; delay <= 4000001; delay += 4000002)
    {
        Reset(BOARD_ROLE_B, 0U); Locked(0U);
        PpsService_Observe(1U, 1U, true, true, 0, delay, 0); Budget(1U, true, 0U);
    }
    Reset(BOARD_ROLE_B, 0U); Locked(0U);
    PpsService_Observe(1U, 1U, false, false, 0, 0, 0); Budget(1U, true, 0U);
}
/** @brief 提交预期的小幅变频；now_ms为提交时刻，变化1000ppb且推进速率代际，无返回值。 */
static void SmallRate(uint32_t now_ms)
{
    assert(PpsService_BeforeFrequency(now_ms, actual_frequency, actual_frequency + 1000));
    actual_frequency += 1000; ++revision; PpsService_AfterFrequency(now_ms, true);
}
/** @brief 验证首次锁定同次调频、有界重新资格和第二次变频撤销；无参数，无返回值。 */
static void TestRateRequalification(void)
{
    Reset(BOARD_ROLE_B, 0U); Locked(0U); SmallRate(0U);
    Budget(0U, true, 2000U);
    PpsService_Observe(1000U, 1000U, false, false, 0, 0, 100000);
    Budget(1000U, true, 2000U);
    PpsService_Observe(2500U, 2500U, false, true, -20000, 0, -500);
    Budget(3999U, true, 1U); Budget(4000U, false, 0U);
    Reset(BOARD_ROLE_B, 0U); Locked(0U); SmallRate(0U);
    Locked(3000U); Budget(4000U, true, 1000U);
    Reset(BOARD_ROLE_B, 0U); Locked(0U); SmallRate(0U);
    assert(PpsService_BeforeFrequency(1U, 1000, 1100));
    actual_frequency = 1100; ++revision; PpsService_AfterFrequency(1U, true);
    Budget(1U, true, 0U);
    Reset(BOARD_ROLE_B, 0U); Locked(0U);
    assert(PpsService_BeforeFrequency(0U, 0, 1125)); actual_frequency = 1125;
    ++revision; PpsService_AfterFrequency(0U, true); Budget(0U, true, 2000U);
    Reset(BOARD_ROLE_B, 0U); Locked(0U);
    assert(PpsService_BeforeFrequency(0U, 0, 1126)); actual_frequency = 1126;
    ++revision; PpsService_AfterFrequency(0U, true); Budget(0U, true, 0U);
    Reset(BOARD_ROLE_B, 0U);
    assert(PpsService_BeforeFrequency(0U, INT32_MIN, INT32_MAX));
    actual_frequency = INT32_MAX; ++revision; PpsService_AfterFrequency(0U, true);
    Budget(0U, true, 0U);
}
/** @brief 验证未知代际、同步撤销和失效时钟的关闭边界；无参数，无返回值。 */
static void TestInvalidation(void)
{
    Reset(BOARD_ROLE_B, 0U); Locked(0U); Budget(0U, true, 2000U);
    ++epoch; Budget(1U, true, 0U); assert(!snapshot.armed);
    Locked(2U); Budget(2U, true, 2000U); ++revision; Budget(3U, true, 0U);
    Locked(4U); Budget(4U, true, 2000U);
    assert(PpsService_Invalidate(4U)); assert(!snapshot.armed); Budget(4U, true, 0U);
    Reset(BOARD_ROLE_B, 0U); Locked(0U); Budget(0U, true, 2000U);
    read_status = BSP_PTP_NOT_READY; Budget(1U, true, 0U); read_status = BSP_PTP_OK;
    Locked(2U); Budget(2U, true, 0U);
    assert(!PpsService_Invalidate(3U));
}
/** @brief 验证Stop失败锁存与显式Init恢复，以及调频命令失败；无参数，无返回值。 */
static void TestFaults(void)
{
    Reset(BOARD_ROLE_B, 0U); Locked(0U); Budget(0U, true, 2000U);
    stop_status = BSP_PPS_TARGET_ERROR;
    assert(!PpsService_Invalidate(0U));
    stop_status = BSP_PPS_OK; assert(!PpsService_Invalidate(1U));
    Locked(1U); Budget(1U, true, 0U);
    Reset(BOARD_ROLE_B, 0U); Locked(0U); stop_status = BSP_PPS_TARGET_ERROR;
    assert(!PpsService_BeforeFrequency(0U, 0, 1126));
    stop_status = BSP_PPS_OK; assert(!PpsService_Deinit());
    Reset(BOARD_ROLE_B, 0U); Locked(0U);
    assert(PpsService_BeforeFrequency(0U, 0, 1000));
    PpsService_AfterFrequency(0U, false); Locked(1U); Budget(1U, true, 0U);
    Reset(BOARD_ROLE_A, 0U); process_status = BSP_PPS_TARGET_ERROR;
    PpsService_Process(0U, true); process_status = BSP_PPS_OK;
    Budget(1U, true, 0U);
    init_status = BSP_PPS_BAD_CONFIG; assert(!PpsService_Init(0U, BOARD_ROLE_A, Log));
    Reset(BOARD_ROLE_A, 0U); assert(PpsService_Deinit()); assert(!snapshot.armed);
}
/** @brief 验证FAULT后1秒自动重试、A/B恢复状态及故障码；无参数，无返回值。 */
static void TestFaultRecovery(void)
{
    PpsServiceSnapshot out;
    Reset(BOARD_ROLE_A, 0U);
    process_status = BSP_PPS_TARGET_ERROR;
    PpsService_Process(0U, true);
    assert(PpsService_GetSnapshot(0U, &out));
    assert(out.faulted && out.faults == 1U &&
           out.last_fault_status == (uint32_t)BSP_PPS_TARGET_ERROR);
    process_status = BSP_PPS_OK;
    PpsService_Process(999U, true);
    assert(PpsService_GetSnapshot(999U, &out));
    assert(out.faulted && out.recoveries == 0U);
    PpsService_Process(1000U, true);
    assert(PpsService_GetSnapshot(1000U, &out));
    assert(!out.faulted && out.recoveries == 1U &&
           out.state == PPS_SERVICE_QUALIFIED && out.faults == 1U);
    Reset(BOARD_ROLE_B, 0U);
    Locked(0U); Budget(0U, true, 2000U);
    process_status = BSP_PPS_TARGET_ERROR;
    PpsService_Process(1000U, true);
    assert(PpsService_GetSnapshot(1000U, &out));
    assert(out.faulted && out.faults == 1U);
    process_status = BSP_PPS_OK;
    PpsService_Process(2000U, true);
    assert(PpsService_GetSnapshot(2000U, &out));
    assert(!out.faulted && out.recoveries == 1U &&
           out.state == PPS_SERVICE_UNQUALIFIED && !out.qualified);
    Locked(2000U); Budget(2000U, true, 2000U);
    assert(PpsService_GetSnapshot(2000U, &out));
    assert(out.qualified && !out.faulted);
    puts("PASS PPS recovery: fault code, 1s retry, A qualified/B unqualified-then-lock");
}

/** @brief 验证日志频率、有界长度及精度声明；无参数，无返回值。 */
static void TestLogs(void)
{
    Reset(BOARD_ROLE_B, 0U); Locked(0U); SmallRate(0U);
    PpsService_Process(999U, false); assert(log_count == 0U);
    PpsService_Process(1000U, false); assert(log_count == 1U);
    assert(strstr(last_log, "$PPS,ROLE=B,STATE=RATE_REQUALIFY,QUALIFIED=1,") != NULL);
    assert(strstr(last_log, ",SYNC=0\r\n") != NULL);
    PpsService_Process(1999U, false); assert(log_count == 1U);
    snapshot.emitted = snapshot.missed = snapshot.errors = UINT32_MAX;
    PpsService_Process(2000U, false); assert(log_count == 2U && strlen(last_log) < 256U);
}
/** @brief 验证只读资格快照，不改变PPS输出/伺服状态。 */
static void TestSnapshot(void)
{
    PpsServiceSnapshot out;
    assert(!PpsService_GetSnapshot(0U,NULL));
    Reset(BOARD_ROLE_A,0U);
    assert(PpsService_GetSnapshot(10U,&out));
    assert(out.configured && out.qualified && !out.faulted &&
           out.state==PPS_SERVICE_QUALIFIED && out.sample_age_ms==10U);
    assert(PpsService_Deinit());
    assert(!PpsService_GetSnapshot(20U,&out));
}
/** @brief 依次运行真实服务边界测试；无参数，成功返回0。 */
int main(void)
{
    TestAcquisition(); TestFreshness(); TestQuality(); TestRateRequalification();
    TestInvalidation(); TestFaults(); TestFaultRecovery(); TestLogs(); TestSnapshot();
    puts("PPS service: acquisition, quality, timeouts, frequency, faults, logs PASS");
    return 0;
}
