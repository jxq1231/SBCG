/**
 * @file        pps_service.c
 * @brief       管理参考/跟随板整秒输出的资格、期限及变频交接。
 * @details     输出边沿由BSP硬件路径生成；本模块只授予有界预算并在观测失效时同步停止。
 * @note        仅串行前台；不写PTP时间，不新增IRQ/DMA，内部资格不表示示波器精度验收。
 */
#include "Sync/pps_service.h"
#include "BSP/bsp_pps.h"
#include "BSP/bsp_ptp.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#define SAMPLE_MAX_AGE_MS 2000U
#define RATE_REQUALIFY_MS 4000U
#define MAX_RATE_DELTA_PPB 1125
#define MAX_OFFSET_X2_NS INT64_C(20000)
#define MAX_DELAY_X2_NS INT64_C(4000000)
#define MAX_DRIFT_PPB 500

typedef enum { UNQUALIFIED, QUALIFIED, RATE_REQUALIFY, FAULT } Qualification;
static Qualification state;
static BoardRole board_role;
static bool configured, pending, preserve;
static uint32_t epoch, revision, sample_ms, rate_ms, log_ms, pending_revision;
static int32_t pending_frequency;
static void (*emit_line)(const char *, int);

/** @brief 缓存当前已确认的时钟代际；无参数，无返回值，仅串行前台纯读。 */
static void CacheGeneration(void)
{ epoch=BSP_PTP_GetEpoch(); revision=BSP_PTP_GetRateRevision(); }

/**
 * @brief 同步撤销输出资格，保留已有故障锁存。
 * @param now_ms 当前毫秒，不刷新样本年龄。
 * @return true表示停止成功且无既有FAULT；false禁止后续相位命令。
 * @note Stop不依赖读时，时钟故障时仍执行。
 */
bool PpsService_Invalidate(uint32_t now_ms)
{
    (void)now_ms;
    bool was_fault=state==FAULT;
    pending=preserve=false;
    state=BSP_PPS_Stop()==BSP_PPS_OK && !was_fault ? UNQUALIFIED : FAULT;
    CacheGeneration();
    return state!=FAULT;
}

/** @brief 锁存故障并尝试停止输出；无参数、无返回值，只有显式Init可以恢复。 */
static void Fail(void)
{ state=FAULT; (void)PpsService_Invalidate(0U); }

/**
 * @brief 检查本地时钟健康、未知代际及现有资格期限。
 * @param now_ms 当前单调毫秒，允许自然回绕。
 * @return true表示时钟健康且无锁存故障，不等于已有输出资格。
 * @note A未知代际后也须显式Init；B可用后续新鲜锁定重新获取。
 */
static bool Check(uint32_t now_ms)
{
    if (!configured || state==FAULT) { return false; }
    BSP_PtpTime sample;
    if (BSP_PTP_Read(&sample)!=BSP_PTP_OK) { Fail(); return false; }
    if (epoch!=BSP_PTP_GetEpoch() || revision!=BSP_PTP_GetRateRevision())
    { return PpsService_Invalidate(now_ms); }
    if (board_role==BOARD_ROLE_B &&
        ((state==QUALIFIED && (uint32_t)(now_ms-sample_ms)>=SAMPLE_MAX_AGE_MS) ||
         (state==RATE_REQUALIFY && ((uint32_t)(now_ms-sample_ms)>=SAMPLE_MAX_AGE_MS ||
                                   (uint32_t)(now_ms-rate_ms)>=RATE_REQUALIFY_MS))))
    { return PpsService_Invalidate(now_ms); }
    return true;
}

/**
 * @brief 校验资源、角色和读时并建立初始安全低状态。
 * @param now_ms 当前毫秒及日志起点。
 * @param role 已登记A/B身份。
 * @param log 可空日志回调，返回前消费或复制文本。
 * @return true表示初始化成功，尚未安排目标。
 * @note 可显式恢复FAULT；不自行初始化或修改PTP时钟。
 */
bool PpsService_Init(uint32_t now_ms,BoardRole role,void (*log)(const char *,int))
{
    configured=false; pending=preserve=false; state=UNQUALIFIED;
    board_role=role; emit_line=log; log_ms=now_ms;
    BSP_PtpTime sample;
    if (BSP_PPS_Init()!=BSP_PPS_OK ||
        (role!=BOARD_ROLE_A && role!=BOARD_ROLE_B) || BSP_PTP_Read(&sample)!=BSP_PTP_OK)
    { Fail(); return false; }
    CacheGeneration(); configured=true;
    if (role==BOARD_ROLE_A) { state=QUALIFIED; }
    return true;
}

/** @brief 停止并卸载服务；无参数，返回停止成功且未锁存故障，不清除FAULT。 */
bool PpsService_Deinit(void)
{ bool ok=PpsService_Invalidate(0U); configured=false; emit_line=NULL; return ok; }

/** @brief 复制PPS资格/时效/硬件观察；不改变资格或硬件。 */
bool PpsService_GetSnapshot(uint32_t now_ms,PpsServiceSnapshot *out)
{
    if(out==NULL)return false;
    *out=(PpsServiceSnapshot){0};
    BSP_PpsSnapshot hardware;
    if(!configured || !BSP_PPS_GetSnapshot(&hardware))return false;
    out->configured=true;
    out->state=state==QUALIFIED?PPS_SERVICE_QUALIFIED:
                state==RATE_REQUALIFY?PPS_SERVICE_RATE_REQUALIFY:
                state==FAULT?PPS_SERVICE_FAULT:PPS_SERVICE_UNQUALIFIED;
    out->qualified=state==QUALIFIED || state==RATE_REQUALIFY;
    out->faulted=state==FAULT;
    out->armed=hardware.armed;
    out->sample_age_ms=(uint32_t)(now_ms-sample_ms);
    out->epoch=hardware.epoch;
    out->emitted=hardware.emitted;
    out->missed=hardware.missed;
    out->errors=hardware.errors;
    return true;
}

/**
 * @brief 接收本次伺服已接受的质量观测。
 * @param now_ms 处理毫秒。
 * @param captured_ms 交换完成毫秒，调用者保有输入所有权。
 * @param internal_locked 本次Observe后、Commit前的内部锁。
 * @param drift_valid 当前速率分段是否已有漂移。
 * @param offset_x2_ns 时差的两倍纳秒。
 * @param delay_x2_ns 路径延迟的两倍纳秒。
 * @param drift_ppb 残余漂移ppb。
 * @return 无返回值。
 * @note RATE_REQUALIFY内暂容尚未形成漂移估计，但不容越界观测或截止延期。
 */
void PpsService_Observe(uint32_t now_ms,uint32_t captured_ms,bool internal_locked,
    bool drift_valid,int64_t offset_x2_ns,int64_t delay_x2_ns,int32_t drift_ppb)
{
    if (!Check(now_ms) || board_role!=BOARD_ROLE_B) { return; }
    bool valid=(uint32_t)(now_ms-captured_ms)<SAMPLE_MAX_AGE_MS &&
        offset_x2_ns>=-MAX_OFFSET_X2_NS && offset_x2_ns<=MAX_OFFSET_X2_NS &&
        delay_x2_ns>=0 && delay_x2_ns<=MAX_DELAY_X2_NS &&
        (!drift_valid || (drift_ppb>=-MAX_DRIFT_PPB && drift_ppb<=MAX_DRIFT_PPB));
    if (!valid || (state==QUALIFIED && !drift_valid))
    { (void)PpsService_Invalidate(now_ms); return; }
    if (internal_locked && drift_valid) { state=QUALIFIED; }
    if (state==QUALIFIED || state==RATE_REQUALIFY) { sample_ms=captured_ms; }
}

/**
 * @brief 在频率命令前判断是否允许保持已取得的资格。
 * @param now_ms 当前毫秒。
 * @param previous_ppb 调用者记录的原绝对频率ppb。
 * @param next_ppb 新绝对频率ppb。
 * @return true允许继续；false禁止硬件写入。
 * @note 64位差避免INT32边界溢出；只有单次小变频保留资格。
 */
bool PpsService_BeforeFrequency(uint32_t now_ms,int32_t previous_ppb,int32_t next_ppb)
{
    if (!Check(now_ms)) { return false; }
    const int64_t delta=(int64_t)next_ppb-previous_ppb;
    bool keep=board_role==BOARD_ROLE_B && state==QUALIFIED && !pending &&
        previous_ppb==BSP_PTP_GetFrequencyPpb() &&
        delta>=-MAX_RATE_DELTA_PPB && delta<=MAX_RATE_DELTA_PPB;
    const int32_t actual=BSP_PTP_GetFrequencyPpb();
    if (!keep && !PpsService_Invalidate(now_ms)) { return false; }
    pending=true; preserve=keep; pending_frequency=next_ppb;
    pending_revision=revision;
    if (next_ppb!=actual)
    {
        if (pending_revision==UINT32_MAX) { Fail(); return false; }
        ++pending_revision;
    }
    return true;
}

/**
 * @brief 消费实际变频结果并建立有界重新资格期限。
 * @param now_ms 命令完成毫秒。
 * @param success BSP及提交是否成功。
 * @return 无返回值。
 * @note 不接受未知epoch、速率代际或实际频率；失败同步停止并锁存FAULT。
 */
void PpsService_AfterFrequency(uint32_t now_ms,bool success)
{
    if (!pending || !success || epoch!=BSP_PTP_GetEpoch() ||
        pending_revision!=BSP_PTP_GetRateRevision() || pending_frequency!=BSP_PTP_GetFrequencyPpb())
    { Fail(); return; }
    if (preserve) { state=RATE_REQUALIFY; rate_ms=now_ms; }
    pending=preserve=false; CacheGeneration();
}

/**
 * @brief 推进硬件目标观察并输出最多每秒一条诊断。
 * @param now_ms 当前毫秒。
 * @param schedule_allowed 是否允许新增目标，false仍检查截止并停止失效输出。
 * @return 无返回值。
 * @note 静态状态归本模块所有；SYNC=0明确保留外部精度未验收边界。
 */
void PpsService_Process(uint32_t now_ms,bool schedule_allowed)
{
    uint32_t budget=0U;
    if (Check(now_ms) && (state==QUALIFIED || state==RATE_REQUALIFY))
    {
        budget=board_role==BOARD_ROLE_A ? SAMPLE_MAX_AGE_MS : SAMPLE_MAX_AGE_MS-(uint32_t)(now_ms-sample_ms);
        if (state==RATE_REQUALIFY)
        {
            uint32_t remaining=RATE_REQUALIFY_MS-(uint32_t)(now_ms-rate_ms);
            if (remaining<budget) { budget=remaining; }
        }
    }
    if (BSP_PPS_Process(budget,schedule_allowed)!=BSP_PPS_OK) { Fail(); }
    if (!configured || emit_line==NULL || (uint32_t)(now_ms-log_ms)<1000U) { return; }
    log_ms=now_ms;
    BSP_PpsSnapshot snapshot;
    if (!BSP_PPS_GetSnapshot(&snapshot)) { Fail(); return; }
    const char *name=state==QUALIFIED ? "QUALIFIED" : state==RATE_REQUALIFY ? "RATE_REQUALIFY" :
                     state==FAULT ? "FAULT" : "UNQUALIFIED";
    char line[256];
    int length=snprintf(line,sizeof line,
        "$PPS,ROLE=%s,STATE=%s,QUALIFIED=%u,ARMED=%u,TARGET_SEC=%" PRIu32
        ",EMITTED=%" PRIu32 ",MISSED=%" PRIu32 ",ERRORS=%" PRIu32 ",SYNC=0\r\n",
        board_role==BOARD_ROLE_A ? "A" : "B",name,
        state==QUALIFIED || state==RATE_REQUALIFY ? 1U : 0U,snapshot.armed ? 1U : 0U,
        snapshot.target_seconds,snapshot.emitted,snapshot.missed,snapshot.errors);
    if (length>0 && (size_t)length<sizeof line) { emit_line(line,length); }
}
