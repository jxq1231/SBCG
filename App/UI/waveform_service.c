/**
 * @file waveform_service.c
 * @brief 复制已验证PCM到CPU历史环，按共同PTP窗口生成两板原始波形快照。
 * @details RAW模式固定20ms窗口、10Hz计划；PERIOD模式按约3个周期缩短窗口以观察
 *          两板相位。A用AudioTimeMap从共同时间反算本板DMA帧，B用同一绝对窗口和
 *          自己的AudioTimeMap取本地帧。无计划、失域或未收敛时显示本板最近20ms
 *          原始PCM（LOCAL），不画人工零线，不做插值、滤波、平滑或独立相位补偿。
 *          A的全滞后周期搜索按前台轮次分批，避免占用PTP报文处理窗口。
 * @note .WaveformState为CPU独占SDRAM NOLOAD；Init先清元数据，数组写后才读。
 *       所有入口前台串行调用，Stage只在借用期间访问DMA块，不保存指针。
 */
#include "waveform_service.h"
#include <stddef.h>
#include <string.h>

#define REFRESH_MS WAVEFORM_REFRESH_MS
#define STREAM_TIMEOUT_MS 650U
#define PLAN_TIMEOUT_MS 650U
/** @brief B允许计划在deadline之后到达的余量；A主循环偶发百余毫秒时避免退回LOCAL。 */
#define PLAN_LATE_NS UINT64_C(250000000)
#define HISTORY_DELAY_NS UINT64_C(350000000)
#define DISPLAY_LEAD_NS UINT64_C(100000000)
#define MODEL_HOLD_NS UINT64_C(1500000000)
#define NS_PER_SEC UINT64_C(1000000000)
#define PERIOD_WINDOW_FRAMES 2048U
#define PERIOD_MIN_LAG 8U
#define PERIOD_MAX_LAG 480U
#define PERIOD_SCRATCH_FRAMES (PERIOD_WINDOW_FRAMES + PERIOD_MAX_LAG)
#define PERIOD_LAGS_PER_SLICE 64U
#define PERIOD_MIN_RMS_PCM 50.0f
#define PERIOD_REFRESH_MS WAVEFORM_PERIOD_REFRESH_MS
#define PERIOD_CONFIRM_RELATIVE 0.005
#define PERIOD_ERROR_LIMIT 0.08f
#define PERIOD_NEAR_BEST 0.02f

/** @brief 仅CPU访问的大数组；DMA从不写此区，因而无需增加缓存维护。 */
typedef struct {
    int16_t ring[WAVEFORM_RING_FRAMES];
    int16_t stage[BSP_AUDIO_HALF_FRAMES];
    int16_t period[PERIOD_SCRATCH_FRAMES];
    float period_error[PERIOD_MAX_LAG + 2U];
} WaveformStorage;
#if defined(__GNUC__)
static WaveformStorage storage __attribute__((section(".WaveformState"), aligned(32)));
#else
static WaveformStorage storage;
#endif
_Static_assert(sizeof(WaveformStorage) < 128U * 1024U, "waveform SDRAM budget");

/** @brief 小型前台元数据；不含任何借用指针。 */
static struct {
    bool initialized, staged, have_ring, healthy, model_valid, have_left;
    bool pending, outgoing, have_sequence, have_refresh;
    bool phase_valid, have_period_check, have_period_evidence;
    BoardRole role;
    uint32_t stage_session, stage_count, session, last_block_ms, refresh_ms;
    uint32_t sequence, received_sequence, plan_ms, anchor_ms, period_ms;
    uint32_t now_ms, period_good_ms, local_period_ms;
    uint8_t confirmation_count, reason;
    uint64_t stage_first, first_frame, end_frame, common_ns;
    RangingSessionSnapshot domain;
    AudioTimeMapAnchor left, right;
    double model_frame, model_ns, frames_per_ns, phase_origin_ns, phase_period_ns;
    /** 最近一次共同PERIOD计划的周期；B本地回退时沿用，使LOCAL与SYNC同样显示约3个周期。 */
    double local_period_ns;
    double candidate_period_ns, candidate_first_ns, candidate_middle_ns;
    /** 同一份PCM副本和建窗时的坐标；next_lag为0表示没有待处理搜索。 */
    struct {
        uint64_t start_ns;
        double start_frame_offset, frames_per_ns, crossing_period;
        float best;
        uint32_t next_lag;
    } period_search;
    WaveformPlan plan;
    WaveformSnapshot snapshot;
} state;

/** @brief 发布不可用状态，撤掉旧点资格并增加界面代数；不读数组。 @return 无返回值。 */
static void Invalidate(void)
{
    uint32_t seq = state.snapshot.frame_seq + 1U;
    memset(&state.snapshot, 0, sizeof state.snapshot);
    state.snapshot.frame_seq = seq;
}

/** @brief 清理PCM连续性，防止断流/End失败后显示拼接历史。 @return 无返回值。 */
static void ClearHistory(void)
{
    state.have_ring = false;
    state.pending = false;
    state.outgoing = false;
    state.phase_valid = false;
    state.period_search.next_lag = 0U;
    state.have_period_check = false;
    state.have_period_evidence = false;
    state.confirmation_count = 0U;
    state.candidate_period_ns = 0.0;
    state.local_period_ns = 0.0;
    state.reason = WAVEFORM_REASON_STREAM;
    Invalidate();
}

/** @brief 复位前台元数据，NOLOAD数组无需清零。 @return 无返回值。 */
void WaveformService_Init(void)
{
    memset(&state, 0, sizeof state);
    state.initialized = true;
}

/** @brief 时钟所有者在跳变/连续调频动作前划分模型代际，保留独立PCM历史。 @return 无返回值。 @note 恢复共同显示至少需要两次边界后的有效锚点；此函数不访问外部数组。 */
void WaveformService_NotifyClockChange(void)
{
    if (!state.initialized) return;
    state.have_left = false;
    state.model_valid = false;
    state.pending = false;
    state.outgoing = false;
    state.have_refresh = false;
    state.phase_valid = false;
    state.period_search.next_lag = 0U;
    state.have_period_check = false;
    state.have_period_evidence = false;
    state.confirmation_count = 0U;
    state.candidate_period_ns = 0.0;
    state.reason = WAVEFORM_REASON_CLOCK_MODEL;
    if (state.snapshot.synced) Invalidate();
}

/** @brief 只在原始借用有效期间复制左声道。 @param block 只读借用，检查边界后不保留指针。 @return 无返回值。 */
void WaveformService_StageBlock(const BSP_AudioBlock *block)
{
    if (!state.initialized) return;
    state.staged = false;
    if (block == NULL || block->samples == NULL || !block->index_valid ||
        block->session == 0U || block->frame_count == 0U ||
        block->frame_count > BSP_AUDIO_HALF_FRAMES ||
        block->first_frame > UINT64_MAX - block->frame_count) return;
    state.stage_first = block->first_frame;
    state.stage_count = block->frame_count;
    state.stage_session = block->session;
    for (uint32_t i = 0; i < block->frame_count; ++i)
        storage.stage[i] = block->samples[2U * i];
    state.staged = true;
}

/** @brief End成功后将stage提交到环；失败清历史。 @param accepted 原始End返回值。 @param now_ms 当前HAL毫秒。 @return 无返回值。 */
void WaveformService_CompleteBlock(bool accepted, uint32_t now_ms)
{
    if (!state.initialized) return;
    if (!accepted || !state.staged) {
        state.staged = false;
        ClearHistory();
        return;
    }
    state.staged = false;
    if (state.session != state.stage_session) {
        state.model_valid = false;
        state.have_left = false;
        ClearHistory();
    }
    if (!state.have_ring || state.stage_first != state.end_frame) {
        ClearHistory();
        state.first_frame = state.stage_first;
    }
    state.session = state.stage_session;
    for (uint32_t i = 0; i < state.stage_count; ++i)
        storage.ring[(state.stage_first + i) % WAVEFORM_RING_FRAMES] = storage.stage[i];
    state.end_frame = state.stage_first + state.stage_count;
    if (state.end_frame - state.first_frame > WAVEFORM_RING_FRAMES)
        state.first_frame = state.end_frame - WAVEFORM_RING_FRAMES;
    state.have_ring = true;
    state.last_block_ms = now_ms;
}

/** @brief 由有效锚点对建立用于显示的有界仿射模型。 @param observation 前台扇出副本，不持有指针。 @return 无返回值。 */
void WaveformService_ObserveAnchor(const AudioTimeAnchorObservation *observation)
{
    if (!state.initialized) return;
    AudioTimeMapResult validation;
    if (observation == NULL || !observation->anchor.valid ||
        observation->anchor.session != state.session) {
        state.have_left = false;
        state.model_valid = false;
        state.pending = false;
        state.period_search.next_lag = 0U;
        return;
    }
    if (!state.have_left) {
        state.right = observation->anchor;
        state.have_left = true;
        return;
    }
    state.left = state.right;
    state.right = observation->anchor;
    /* 各端点先除再加，退帧也不产生无符号差回绕；顺序校验仍只归AudioTimeMap负责。 */
    uint64_t midpoint = state.left.frame_hi / 2U + state.right.frame_lo / 2U +
        ((state.left.frame_hi % 2U + state.right.frame_lo % 2U) / 2U);
    state.model_valid = AudioTimeMap_Estimate(&state.left, &state.right,
        state.session, midpoint, &validation) == AUDIO_TIME_MAP_OK;
    if (!state.model_valid) {
        state.pending = false;
        state.period_search.next_lag = 0U;
        return;
    }
    double left_frame = ((double)state.left.frame_lo + (double)state.left.frame_hi + 1.0) / 2.0;
    double right_frame = ((double)state.right.frame_lo + (double)state.right.frame_hi + 1.0) / 2.0;
    uint64_t left_ns = (uint64_t)state.left.sec * NS_PER_SEC + state.left.nsec;
    uint64_t right_ns = (uint64_t)state.right.sec * NS_PER_SEC + state.right.nsec;
    state.model_frame = right_frame;
    state.model_ns = (double)right_ns;
    state.frames_per_ns = (right_frame - left_frame) / (double)(right_ns - left_ns);
    state.anchor_ms = observation->captured_ms;
}

/** @brief 将共同模型时间逆算为零基PCM帧。 @param ns 共同纳秒。 @return 连续帧坐标，仅供显示。 */
static double FrameAt(uint64_t ns)
{
    return state.model_frame + ((double)ns - state.model_ns) * state.frames_per_ns - 1.0;
}

/** @brief 取已在环内验证的PCM值。 @param frame 零基帧。 @return 左声道PCM16。 */
static int16_t Sample(uint64_t frame)
{
    return storage.ring[frame % WAVEFORM_RING_FRAMES];
}

/** @brief 把double帧坐标圆整为无符号帧。 @param value 帧坐标。 @return 四舍五入后的帧。 */
static uint64_t RoundFrame(double value)
{
    return value <= 0.0 ? 0U : (uint64_t)(value + 0.5);
}

/** @brief 将周期窗口及最大滞后邻域线性复制到CPU副本。 @param start 窗口起点零基帧。 @return 完整落在环内true。 */
static bool LoadPeriodWindow(uint64_t start)
{
    if (start < state.first_frame || start > state.end_frame ||
        state.end_frame - start < PERIOD_SCRATCH_FRAMES) return false;
    uint32_t index = (uint32_t)(start % WAVEFORM_RING_FRAMES);
    for (uint32_t i = 0U; i < PERIOD_SCRATCH_FRAMES; ++i) {
        storage.period[i] = storage.ring[index];
        if (++index == WAVEFORM_RING_FRAMES) index = 0U;
    }
    return true;
}

/** @brief 判断窗口是否有足以做周期搜索的PCM能量。 @return 抽样均方超过门限true。 */
static bool PeriodWindowHasSignal(void)
{
    float energy = 0.0f;
    for (uint32_t i = 0U; i < PERIOD_WINDOW_FRAMES; i += 4U) {
        const float value = storage.period[i];
        energy += value * value;
    }
    return energy > 512.0f * PERIOD_MIN_RMS_PCM * PERIOD_MIN_RMS_PCM;
}

/** @brief 计算候选周期的归一化差分能量。 @param lag 周期帧数。 @return 0高度重复，1附近不相关。 */
static float PeriodError(uint32_t lag)
{
    float error = 0.0f, energy = 0.0f, sum_a = 0.0f, sum_b = 0.0f;
    for (uint32_t i = 0U; i < PERIOD_WINDOW_FRAMES; i += 4U) {
        const float a = storage.period[i], b = storage.period[i + lag];
        const float difference = a - b;
        error += difference * difference;
        energy += a * a + b * b;
        sum_a += a;
        sum_b += b;
    }
    energy -= (sum_a * sum_a + sum_b * sum_b) / 512.0f;
    return energy > 512.0f * PERIOD_MIN_RMS_PCM * PERIOD_MIN_RMS_PCM ? error / energy : 2.0f;
}

/** @brief 用窗口内上升过零间隔估计基频分数周期。 @return 8..480帧周期；不足或不一致时0。 */
static double ZeroCrossPeriod(void)
{
    double first = -1.0, previous = -1.0, last = -1.0;
    double min_interval = 1.0e9, max_interval = 0.0;
    uint32_t cycles = 0U;
    for (uint32_t i = 0U; i + 1U < PERIOD_SCRATCH_FRAMES; ++i) {
        const double left = storage.period[i], right = storage.period[i + 1U];
        if (left > 0.0 || right <= 0.0) continue;
        const double crossing = (double)i - left / (right - left);
        if (first < 0.0) {
            first = crossing;
        } else {
            const double interval = crossing - previous;
            if (interval <= 0.0) continue;
            if (interval < min_interval) min_interval = interval;
            if (interval > max_interval) max_interval = interval;
            cycles++;
        }
        previous = crossing;
        last = crossing;
    }
    if (cycles == 0U || last <= first) return 0.0;
    double mean = (last - first) / (double)cycles;
    if (mean < 7.75 || mean > (double)PERIOD_MAX_LAG) return 0.0;
    if (cycles >= 2U && (max_interval > mean * 1.50 || min_interval < mean * 0.67)) return 0.0;
    return mean;
}

/**
 * @brief 从已复制的窗口验证过零周期与其分频候选。
 * @param crossing_period 过零提议，单位帧；0表示没有可靠提议。
 * @return 验证通过的分数周期（帧），否则0，由全滞后搜索继续判断。
 */
static double CheckCrossingPeriod(double crossing_period)
{
    /* 干净周期只验证过零提议及2/3/4/5分频候选，不新增常态全滞后扫描。 */
    if (crossing_period > 0.0) {
        float errors[6] = {2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f};
        float crossing_best = 2.0f;
        for (uint32_t divisor = 1U; divisor <= 5U; ++divisor) {
            const double proposal = crossing_period / (double)divisor;
            if (proposal < 7.75) continue;
            const uint32_t lag = (uint32_t)(proposal + 0.5);
            if (lag < PERIOD_MIN_LAG || lag > PERIOD_MAX_LAG) continue;
            const float at = PeriodError(lag);
            if (at <= PeriodError(lag - 1U) && at <= PeriodError(lag + 1U)) {
                errors[divisor] = at;
                if (at < crossing_best) crossing_best = at;
            }
        }
        const float crossing_limit = crossing_best + PERIOD_NEAR_BEST < PERIOD_ERROR_LIMIT ?
            crossing_best + PERIOD_NEAR_BEST : PERIOD_ERROR_LIMIT;
        for (uint32_t divisor = 5U; divisor >= 1U; --divisor)
            if (errors[divisor] <= crossing_limit) return crossing_period / (double)divisor;
    }
    return 0.0;
}

/**
 * @brief 全滞后扫描完成后，从同一窗口的误差选取最短的近优局部谷。
 * @return 分数周期（帧），未找到可靠谷时为0；细化仅作用于周期元数据。
 */
static double SelectScannedPeriod(void)
{
    const float best = state.period_search.best;
    const double crossing_period = state.period_search.crossing_period;
    if (best > PERIOD_ERROR_LIMIT) return 0.0;
    const float limit = best + PERIOD_NEAR_BEST < PERIOD_ERROR_LIMIT ?
        best + PERIOD_NEAR_BEST : PERIOD_ERROR_LIMIT;
    uint32_t lag;
    for (lag = PERIOD_MIN_LAG; lag <= PERIOD_MAX_LAG; ++lag) {
        const float at = storage.period_error[lag];
        if (at <= limit && at <= storage.period_error[lag - 1U] &&
            at <= storage.period_error[lag + 1U]) break;
    }
    if (lag > PERIOD_MAX_LAG) return 0.0;
    double period = (double)lag;
    const float left = storage.period_error[lag - 1U], at = storage.period_error[lag];
    const float right = storage.period_error[lag + 1U];
    const float denominator = left - 2.0f * at + right;
    if (denominator > 1.0e-6f) {
        const double offset = 0.5 * (double)(left - right) / (double)denominator;
        if (offset >= -0.5 && offset <= 0.5) period += offset;
    }
    if (crossing_period >= period * (1.0 - PERIOD_CONFIRM_RELATIVE) &&
        crossing_period <= period * (1.0 + PERIOD_CONFIRM_RELATIVE) &&
        storage.period_error[(uint32_t)(crossing_period + 0.5)] <= limit)
        period = crossing_period;
    return period >= 7.75 && period <= (double)PERIOD_MAX_LAG ? period : 0.0;
}

/** @brief 仅过期已确认周期；等待、失败和发送计划都不能延长最后可靠PCM证据。 @return 无返回值。 */
static void ExpirePeriod(void)
{
    if (state.phase_valid && (uint32_t)(state.now_ms - state.period_good_ms) >= WAVEFORM_PERIOD_HOLD_MS) {
        state.phase_valid = false;
        state.reason = WAVEFORM_REASON_PERIOD_EXPIRED;
        if (state.snapshot.period_valid) Invalidate();
    }
}

/**
 * @brief 消费当前窗口的一次周期判定，推进A的三次确认和相位网格。
 * @param period 本次检测的分数周期（帧），0表示无可靠候选。
 * @return 无返回值。
 * @note 使用建窗时的模型与PCM副本，证据年龄从建窗时刻计算；锁相后不随A每次移动重调。
 */
static void AcceptPeriod(double period)
{
    if (period <= 0.0) {
        state.confirmation_count = 0U;
        state.candidate_period_ns = 0.0;
        state.reason = state.phase_valid ? WAVEFORM_REASON_PERIOD_HELD : WAVEFORM_REASON_RAW;
        ExpirePeriod();
        return;
    }
    const uint64_t start_ns = state.period_search.start_ns;
    const double frames_per_ns = state.period_search.frames_per_ns;
    const double period_ns = period / frames_per_ns;
    if (state.confirmation_count >= WAVEFORM_PERIOD_CONFIRMATIONS ||
        state.confirmation_count == 0U ||
        period_ns < state.candidate_first_ns * (1.0 - PERIOD_CONFIRM_RELATIVE) ||
        period_ns > state.candidate_first_ns * (1.0 + PERIOD_CONFIRM_RELATIVE)) {
        state.candidate_first_ns = period_ns;
        state.confirmation_count = 1U;
    } else {
        state.confirmation_count++;
        if (state.confirmation_count == 2U) state.candidate_middle_ns = period_ns;
    }
    state.candidate_period_ns = period_ns;
    if (state.phase_valid && period_ns >= state.phase_period_ns * (1.0 - PERIOD_CONFIRM_RELATIVE) &&
        period_ns <= state.phase_period_ns * (1.0 + PERIOD_CONFIRM_RELATIVE)) {
        state.period_good_ms = state.period_ms;
        state.reason = WAVEFORM_REASON_PERIOD_LOCKED;
    } else state.reason = state.phase_valid ? WAVEFORM_REASON_PERIOD_HELD : WAVEFORM_REASON_ACQUIRING;
    if (state.confirmation_count == WAVEFORM_PERIOD_CONFIRMATIONS) {
        /* 三候选取中值只决定横轴周期元数据，不修改或平滑任何PCM样点。 */
        double a = state.candidate_first_ns, b = state.candidate_middle_ns;
        if (a > b) { const double swap = a; a = b; b = swap; }
        if (b > period_ns) b = period_ns;
        const double confirmed_ns = a > b ? a : b;
        if (state.phase_valid) {
            /* 锚定旧网格上靠近当前历史窗的一点，避免长基线换周期导致相位跳变。 */
            const double steps = (double)(int64_t)(((double)start_ns - state.phase_origin_ns) / state.phase_period_ns);
            state.phase_origin_ns += steps * state.phase_period_ns;
        } else {
            /* 首次使用A过零；无过零但差分重复性可信时使用该共同历史时刻为参考。 */
            state.phase_origin_ns = (double)start_ns;
            const uint32_t limit = (uint32_t)period + 2U;
            for (uint32_t i = 0U; i <= limit; ++i) {
                const double left = storage.period[i], right = storage.period[i + 1U];
                if (left <= 0.0 && right > 0.0) {
                    const double crossing_offset = state.period_search.start_frame_offset +
                        (double)i - left / (right - left);
                    state.phase_origin_ns = (double)start_ns +
                        crossing_offset / frames_per_ns;
                    break;
                }
            }
        }
        state.phase_period_ns = confirmed_ns;
        state.phase_valid = true;
        state.have_period_evidence = true;
        state.period_good_ms = state.period_ms;
        state.reason = WAVEFORM_REASON_PERIOD_LOCKED;
    }
    ExpirePeriod();
}

/**
 * @brief 按200ms间隔为A建窗，快速路径当轮判定，慢路径保留副本给分批入口。
 * @return 无返回值。已有搜索时不覆盖其PCM或候选时间。
 */
static void UpdatePeriod(void)
{
    if (!state.model_valid) { state.phase_valid = false; return; }
    if (state.period_search.next_lag != 0U || (state.have_period_check &&
        (uint32_t)(state.now_ms - state.period_ms) < PERIOD_REFRESH_MS)) {
        ExpirePeriod();
        return;
    }
    state.have_period_check = true;
    state.period_ms = state.refresh_ms;
    if (state.common_ns <= HISTORY_DELAY_NS) { ExpirePeriod(); return; }
    const uint64_t start_ns = state.common_ns - HISTORY_DELAY_NS;
    if (start_ns < state.domain.source_floor_ns) { ExpirePeriod(); return; }
    const double first = FrameAt(start_ns);
    if (first < 0.0 || first + (double)PERIOD_SCRATCH_FRAMES >= (double)state.end_frame) {
        ExpirePeriod();
        return;
    }
    const uint64_t start_frame = RoundFrame(first);
    if (!LoadPeriodWindow(start_frame) || !PeriodWindowHasSignal()) {
        AcceptPeriod(0.0);
        return;
    }
    state.period_search.start_ns = start_ns;
    state.period_search.start_frame_offset = (double)start_frame - first;
    state.period_search.frames_per_ns = state.frames_per_ns;
    state.period_search.crossing_period = ZeroCrossPeriod();
    const double quick = CheckCrossingPeriod(state.period_search.crossing_period);
    if (quick > 0.0) {
        AcceptPeriod(quick);
        return;
    }
    state.period_search.best = 2.0f;
    state.period_search.next_lag = PERIOD_MIN_LAG - 1U;
    ExpirePeriod();
}

/**
 * @brief 在前台推进最多64个滞后；完整扫描结束才提交一次候选。
 * @return 无返回值。
 * @note 每轮主循环在网络和测距处理后调用一次；失域、断流或模型失效已撤销旧搜索。
 *       与共同窗口刷新分离，不改写PCM，也不改变B对A窗口的跟随方式。
 */
void WaveformService_ProcessPeriod(void)
{
    uint32_t lag = state.period_search.next_lag;
    if (lag == 0U) return;
    if ((uint32_t)(state.now_ms - state.period_ms) >= WAVEFORM_PERIOD_HOLD_MS) {
        state.period_search.next_lag = 0U;
        AcceptPeriod(0.0);
        return;
    }
    for (uint32_t count = 0U; count < PERIOD_LAGS_PER_SLICE &&
         lag <= PERIOD_MAX_LAG + 1U; ++count, ++lag) {
        const float error = PeriodError(lag);
        storage.period_error[lag] = error;
        if (lag >= PERIOD_MIN_LAG && lag <= PERIOD_MAX_LAG &&
            error < state.period_search.best) state.period_search.best = error;
    }
    if (lag <= PERIOD_MAX_LAG + 1U) {
        state.period_search.next_lag = lag;
        return;
    }
    state.period_search.next_lag = 0U;
    AcceptPeriod(SelectScannedPeriod());
}

/**
 * @brief 发布固定窗口原始PCM快照，不做任何插值或滤波。
 * @param first 零基帧起点，必须已确认在环内。
 * @param count 实际帧数，1..WAVEFORM_POINT_COUNT。
 * @param plan 共同计划；NULL表示本地自由运行。
 * @param source WAVEFORM_SOURCE_COMMON或WAVEFORM_SOURCE_LOCAL。
 * @return 无返回值。
 */
static void RenderPcm(uint64_t first, uint32_t count, const WaveformPlan *plan, uint8_t source)
{
    WaveformSnapshot *out = &state.snapshot;
    for (uint32_t i = 0U; i < WAVEFORM_POINT_COUNT; ++i) out->points[i] = 0;
    for (uint32_t i = 0U; i < count; ++i) out->points[i] = Sample(first + i);
    out->valid = true;
    out->synced = source == WAVEFORM_SOURCE_COMMON;
    out->period_valid = plan != NULL && plan->mode == WAVEFORM_MODE_PERIOD;
    out->source = source;
    out->render_state = source;
    out->point_count = (uint16_t)count;
    out->period_milli_hz = plan != NULL ? plan->period_milli_hz : 0U;
    out->window_start_ns = plan != NULL ? plan->window_start_ns : 0U;
    out->window_span_ns = plan != NULL ? plan->window_span_ns :
        (state.model_valid && state.frames_per_ns > 0.0 ?
            (uint64_t)((double)count / state.frames_per_ns + 0.5) : WAVEFORM_WINDOW_SPAN_NS);
    out->plan_sequence = plan != NULL ? plan->sequence : 0U;
    out->frame_seq++;
}

/**
 * @brief 把共同计划窗口映射到本板帧并发布原始PCM。
 * @param plan 非NULL共同计划。
 * @return 窗口完全落在本板历史环内时true。
 * @note 只做时间映射，不按DMA编号取帧；两板各自映射到同一绝对时间区间。
 */
static bool RenderCommon(const WaveformPlan *plan)
{
    if (!state.model_valid || plan == NULL) return false;
    const double start = FrameAt(plan->window_start_ns);
    const double end = FrameAt(plan->window_start_ns + plan->window_span_ns);
    if (end <= start || start < 0.0) return false;
    const uint64_t first = RoundFrame(start);
    uint64_t last = RoundFrame(end);
    if (last <= first || first < state.first_frame || last > state.end_frame) return false;
    uint32_t count = (uint32_t)(last - first);
    if (count > WAVEFORM_POINT_COUNT) {
        /* 第一阶段名义960点；若本板帧率使窗口略长，则截取窗口前部完整960帧。 */
        count = WAVEFORM_POINT_COUNT;
    }
    RenderPcm(first, count, plan, WAVEFORM_SOURCE_COMMON);
    return true;
}

/**
 * @brief 显示本板最近一段原始PCM，作为未同步或无计划时的LOCAL回退。
 * @return 历史环至少有一帧时true。
 * @note 已知共同PERIOD周期时沿用该周期取约3周期窗口；否则按20ms取，模型无效时取960帧名义值。
 */
static bool RenderLocal(void)
{
    if (!state.have_ring || state.end_frame <= state.first_frame) return false;
    WaveformPlan local_plan;
    memset(&local_plan, 0, sizeof local_plan);
    uint64_t span_ns = WAVEFORM_WINDOW_SPAN_NS;
    if (state.local_period_ns > 0.0 &&
        (uint32_t)(state.now_ms - state.local_period_ms) < WAVEFORM_LOCAL_PERIOD_HOLD_MS) {
        double desired_ns = state.local_period_ns * (double)WAVEFORM_PERIOD_DISPLAY_CYCLES;
        if (desired_ns < (double)WAVEFORM_PERIOD_SPAN_MIN_NS)
            desired_ns = (double)WAVEFORM_PERIOD_SPAN_MIN_NS;
        if (desired_ns > (double)WAVEFORM_WINDOW_SPAN_NS)
            desired_ns = (double)WAVEFORM_WINDOW_SPAN_NS;
        span_ns = (uint64_t)(desired_ns + 0.5);
        local_plan.mode = WAVEFORM_MODE_PERIOD;
        local_plan.period_milli_hz = (uint32_t)(1.0e12 / state.local_period_ns + 0.5);
    }
    local_plan.window_span_ns = span_ns;
    const double frames_per_ns = state.model_valid && state.frames_per_ns > 0.0 ?
        state.frames_per_ns : 48000.0 / 1.0e9;
    uint32_t span = WAVEFORM_POINT_COUNT;
    const double exact = (double)span_ns * frames_per_ns;
    if (exact >= 2.0 && exact <= (double)WAVEFORM_POINT_COUNT)
        span = (uint32_t)(exact + 0.5);
    if (span < 2U) span = 2U;
    if (state.end_frame <= state.first_frame + (uint64_t)span) span =
        (uint32_t)(state.end_frame - state.first_frame);
    if (span == 0U) return false;
    uint64_t first = state.end_frame - span;
    if (first < state.first_frame) first = state.first_frame;
    const uint32_t count = (uint32_t)(state.end_frame - first);
    RenderPcm(first, count > WAVEFORM_POINT_COUNT ? WAVEFORM_POINT_COUNT : count,
        &local_plan, WAVEFORM_SOURCE_LOCAL);
    return true;
}

/** @brief 保存有限期LOCAL横轴尺度；仅PERIOD更新，RAW不清除已有横轴。 @param plan 非NULL已接受计划。 @return 无返回值。
 * @note B失域时沿用最近一次PERIOD约3周期窗口；只有从未取得周期或超过LOCAL期限才回退20ms。 */
static void RememberLocalPeriod(const WaveformPlan *plan)
{
    if (plan->mode != WAVEFORM_MODE_PERIOD || plan->period_milli_hz == 0U) return;
    state.local_period_ns = 1.0e12 / (double)plan->period_milli_hz;
    state.local_period_ms = state.role == BOARD_ROLE_A ? state.period_good_ms : state.now_ms;
}

/**
 * @brief A生成RAW20ms或约3周期共同窗口计划；窗口结束在历史延迟之后，刷新10Hz。
 * @return 无返回值。
 * @note A锁相后按整数周期网格生成PERIOD，否则RAW；B只跟随共同窗口。
 */
static void MakePlan(void)
{
    if (!state.model_valid || state.common_ns == 0U) return;
    if (state.common_ns <= HISTORY_DELAY_NS + WAVEFORM_WINDOW_SPAN_NS) return;
    UpdatePeriod();
    uint64_t span_ns = WAVEFORM_WINDOW_SPAN_NS;
    if (state.phase_valid && state.phase_period_ns > 0.0) {
        double desired_ns = state.phase_period_ns * (double)WAVEFORM_PERIOD_DISPLAY_CYCLES;
        if (desired_ns < (double)WAVEFORM_PERIOD_SPAN_MIN_NS)
            desired_ns = (double)WAVEFORM_PERIOD_SPAN_MIN_NS;
        if (desired_ns > (double)WAVEFORM_WINDOW_SPAN_NS)
            desired_ns = (double)WAVEFORM_WINDOW_SPAN_NS;
        span_ns = (uint64_t)(desired_ns + 0.5);
    }
    const uint64_t window_end_ns = state.common_ns - HISTORY_DELAY_NS;
    const uint64_t window_start_ns = window_end_ns - span_ns;
    if (window_start_ns < state.domain.source_floor_ns) return;
    WaveformPlan plan;
    memset(&plan, 0, sizeof plan);
    plan.domain = state.domain.domain;
    plan.authority_epoch = state.domain.authority_epoch;
    plan.sequence = ++state.sequence;
    plan.mode = WAVEFORM_MODE_RAW;
    plan.plan_common_ns = state.common_ns;
    plan.window_start_ns = window_end_ns - WAVEFORM_WINDOW_SPAN_NS;
    plan.window_span_ns = WAVEFORM_WINDOW_SPAN_NS;
    plan.display_deadline_ns = state.common_ns + DISPLAY_LEAD_NS;
    if (state.phase_valid && state.phase_period_ns > 0.0) {
        const double ratio = ((double)window_start_ns - state.phase_origin_ns) /
            state.phase_period_ns;
        double steps = ratio >= 0.0 ? (double)(uint64_t)ratio : -(double)(uint64_t)(-ratio);
        while (state.phase_origin_ns + steps * state.phase_period_ns >
               (double)window_start_ns + 0.5)
            steps -= 1.0;
        double grid_ns = state.phase_origin_ns + steps * state.phase_period_ns;
        uint64_t grid_start_ns = (uint64_t)(grid_ns + 0.5);
        /* WVP2发送整数ns；取整使窗末超前时只退一周期，避免混用浮点边界。 */
        if (grid_start_ns + span_ns > window_end_ns) {
            grid_ns -= state.phase_period_ns;
            grid_start_ns = (uint64_t)(grid_ns + 0.5);
        }
        const double grid_first = FrameAt(grid_start_ns);
        const double grid_last = FrameAt(grid_start_ns + span_ns);
        if (grid_start_ns >= state.domain.source_floor_ns &&
            grid_start_ns + span_ns <= window_end_ns && grid_first >= 0.0 &&
            grid_last > grid_first &&
            RoundFrame(grid_first) >= state.first_frame &&
            RoundFrame(grid_last) <= state.end_frame) {
            plan.window_start_ns = grid_start_ns;
            plan.window_span_ns = span_ns;
            plan.mode = WAVEFORM_MODE_PERIOD;
            plan.period_milli_hz = (uint32_t)(1.0e12 / state.phase_period_ns + 0.5);
        }
    }
    const double first = FrameAt(plan.window_start_ns);
    const double last = FrameAt(plan.window_start_ns + plan.window_span_ns);
    if (plan.window_start_ns < state.domain.source_floor_ns || first < 0.0 || last <= first ||
        RoundFrame(first) < state.first_frame || RoundFrame(last) > state.end_frame) {
        state.reason = WAVEFORM_REASON_HISTORY;
        return;
    }
    RememberLocalPeriod(&plan);
    state.plan = plan;
    state.pending = true;
    state.outgoing = true;
    state.plan_ms = state.refresh_ms;
}

/** @brief 更新资格并按共同deadline发布，失域退回本地真PCM。 @param role 本板角色。 @param healthy 共同读时健康。 @param session 当前会话副本可NULL。 @param common_ns 共同纳秒。 @param now_ms 本机毫秒。 @return 无返回值。 */
void WaveformService_Update(BoardRole role, bool healthy, const RangingSessionSnapshot *session,
                            uint64_t common_ns, uint32_t now_ms)
{
    if (!state.initialized) return;
    state.now_ms = now_ms;
    const bool qualified = healthy && session != NULL && session->established &&
        session->domain != 0U && session->authority_epoch != 0U && common_ns != 0U;
    const bool was_healthy = state.healthy;
    const bool domain_changed = qualified && state.domain.domain != 0U &&
        (state.domain.domain != session->domain ||
         state.domain.authority_epoch != session->authority_epoch || common_ns < state.common_ns);
    const bool qualification_changed = state.role != role || qualified != state.healthy ||
        (qualified && (state.domain.domain != session->domain ||
         state.domain.authority_epoch != session->authority_epoch || common_ns < state.common_ns));
    const bool hard_context = state.role != role || (session != NULL && state.domain.domain != 0U &&
        (state.domain.domain != session->domain ||
         (session->authority_epoch != 0U && state.domain.authority_epoch != session->authority_epoch))) ||
        (qualified && common_ns < state.common_ns);
    const bool changed = qualification_changed || hard_context;
    state.role = role;
    state.healthy = qualified;
    state.common_ns = common_ns;
    if (session != NULL) state.domain = *session;
    /* LOCAL横轴缓存独立于共同时间资格：失域后仍保留最近有效显示周期，
     * 由RenderLocal按WAVEFORM_LOCAL_PERIOD_HOLD_MS自行过期，避免数秒
     * 同步瞬态把1kHz原始PCM回退成20ms约20个周期。 */
    if (changed) {
        state.pending = false;
        state.outgoing = false;
        state.have_sequence = false;
        state.have_refresh = false;
        state.phase_valid = false;
        state.period_search.next_lag = 0U;
        state.period_ms = 0U;
        state.have_period_check = false;
        state.have_period_evidence = false;
        state.confirmation_count = 0U;
        state.candidate_period_ns = 0.0;
        state.reason = WAVEFORM_REASON_CONTEXT;
        /* 首次取得资格或上电后的第一次Update不得丢弃已经扇出的两个锚点；
         * 只有失去资格，或已有资格时换域/共同时间倒退才撤旧模型。 */
        if (!qualified || (was_healthy && domain_changed)) {
            state.model_valid = false;
            state.have_left = false;
        }
        Invalidate();
    }
    if (!state.have_ring || (uint32_t)(now_ms - state.last_block_ms) > STREAM_TIMEOUT_MS) {
        ClearHistory();
        return;
    }
    const bool model_fresh = state.model_valid &&
        (uint32_t)(now_ms - state.anchor_ms) <= (uint32_t)(MODEL_HOLD_NS / 1000000U);
    if (!model_fresh) state.period_search.next_lag = 0U;
    if (state.pending && (!qualified || !model_fresh ||
        (uint32_t)(now_ms - state.plan_ms) > PLAN_TIMEOUT_MS)) {
        state.pending = false;
        state.outgoing = false;
        state.have_refresh = false;
        state.reason = !model_fresh ? WAVEFORM_REASON_MODEL_STALE : WAVEFORM_REASON_PLAN_TIMEOUT;
        if (state.snapshot.synced) Invalidate();
    }
    if (state.pending && qualified && model_fresh &&
        common_ns >= state.plan.display_deadline_ns) {
        const bool rendered = (uint32_t)(now_ms - state.plan_ms) <= PLAN_TIMEOUT_MS &&
            RenderCommon(&state.plan);
        state.pending = false;
        if (!rendered) {
            state.reason = WAVEFORM_REASON_HISTORY;
            state.have_refresh = false;
            if (state.snapshot.synced) Invalidate();
        }
    }
    if (state.snapshot.synced && (!qualified || !model_fresh ||
        (uint32_t)(now_ms - state.plan_ms) > PLAN_TIMEOUT_MS)) {
        state.reason = !model_fresh ? WAVEFORM_REASON_MODEL_STALE : WAVEFORM_REASON_PLAN_TIMEOUT;
        Invalidate();
        state.have_refresh = false;
    }
    if (state.have_refresh && (uint32_t)(now_ms - state.refresh_ms) < REFRESH_MS) return;
    state.have_refresh = true;
    state.refresh_ms = now_ms;
    if (role == BOARD_ROLE_A && qualified && model_fresh) MakePlan();
    const bool waiting_common = (state.pending && qualified && model_fresh) ||
        (qualified && model_fresh && state.snapshot.synced &&
         (uint32_t)(now_ms - state.plan_ms) <= PLAN_TIMEOUT_MS);
    if (!waiting_common || !state.snapshot.valid) (void)RenderLocal();
}

/** @brief 获取CPU副本，不授予PCM指针。 @param out 可NULL输出。 @return 无返回值。 */
void WaveformService_GetSnapshot(WaveformSnapshot *out)
{
    if (out == NULL) return;
    *out = state.snapshot;
    out->candidate_milli_hz = state.candidate_period_ns > 0.0 ?
        (uint32_t)(1.0e12 / state.candidate_period_ns + 0.5) : 0U;
    out->period_age_ms = state.have_period_evidence ? (uint32_t)(state.now_ms - state.period_good_ms) :
        (state.local_period_ns > 0.0 ? (uint32_t)(state.now_ms - state.local_period_ms) : UINT32_MAX);
    out->confirmation_count = state.confirmation_count;
    out->reason = state.reason;
}

/** @brief 取待发计划一次。 @param out 非NULL调用方输出。 @return 有计划true。 */
bool WaveformService_TakePlan(WaveformPlan *out)
{
    if (out == NULL || !state.outgoing) return false;
    *out = state.plan;
    state.outgoing = false;
    return true;
}

/** @brief 验证wire公共字段一次。 @param plan 非NULL副本。 @return 固定范围有效true。 */
static bool ValidPlan(const WaveformPlan *plan)
{
    if (plan->domain == 0U || plan->authority_epoch == 0U ||
        plan->plan_common_ns == 0U || plan->window_start_ns == 0U ||
        plan->window_span_ns < 1000000U || plan->window_span_ns > 100000000U ||
        plan->display_deadline_ns <= plan->plan_common_ns ||
        plan->display_deadline_ns - plan->plan_common_ns > UINT64_C(500000000) ||
        plan->window_start_ns + plan->window_span_ns > plan->plan_common_ns ||
        plan->mode > WAVEFORM_MODE_PERIOD || plan->flags != 0U || plan->reserved != 0U)
        return false;
    if (plan->mode == WAVEFORM_MODE_RAW)
        return plan->period_milli_hz == 0U;
    return plan->period_milli_hz >= 100000U && plan->period_milli_hz <= 6000000U;
}

/** @brief B接收域和新序号均匹配的计划；允许计划在A主循环紧张时略晚到达。 */
bool WaveformService_OnPlan(const WaveformPlan *plan, uint32_t now_ms)
{
    if (plan == NULL || state.role != BOARD_ROLE_B || !state.healthy ||
        plan->domain != state.domain.domain || plan->authority_epoch != state.domain.authority_epoch ||
        plan->window_start_ns < state.domain.source_floor_ns ||
        plan->display_deadline_ns > state.common_ns + DISPLAY_LEAD_NS + UINT64_C(100000000) ||
        (state.have_sequence && (int32_t)(plan->sequence - state.received_sequence) <= 0)) {
        state.reason = WAVEFORM_REASON_PLAN_REJECTED;
        return false;
    }
    if (plan->display_deadline_ns <= state.common_ns &&
        state.common_ns - plan->display_deadline_ns >= PLAN_LATE_NS) {
        state.reason = WAVEFORM_REASON_PLAN_LATE;
        return false;
    }
    state.now_ms = now_ms;
    state.plan = *plan;
    RememberLocalPeriod(plan);
    state.candidate_period_ns = state.local_period_ns;
    state.reason = plan->mode == WAVEFORM_MODE_PERIOD ? WAVEFORM_REASON_PERIOD_LOCKED : WAVEFORM_REASON_RAW;
    state.pending = true;
    state.have_sequence = true;
    state.received_sequence = plan->sequence;
    state.plan_ms = now_ms;
    return true;
}

/** @brief 写大端整数，不依赖结构体布局。 @param data 可写字节。 @param value 输入数值。 @param count 字节数4或8。 @return 无返回值。 */
static void Put(uint8_t *data, uint64_t value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) data[count - 1U - i] = (uint8_t)(value >> (8U * i));
}
/** @brief 读固定宽度大端整数。 @param data 只读字节。 @param count 字节数4或8。 @return 无符号整数。 */
static uint64_t Get(const uint8_t *data, unsigned count)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value = (value << 8U) | data[i];
    return value;
}
/** @brief 编码固定56字节WVP2，逐字段写入。 @param plan 只读输入。 @param data 可写输出。 @param length 必须56。 @return 成功true。 */
bool WaveformPlanWire_Encode(const WaveformPlan *plan, uint8_t *data, uint16_t length)
{
    if (plan == NULL || data == NULL || length != WAVEFORM_PLAN_WIRE_BYTES || !ValidPlan(plan)) return false;
    memcpy(data, "WVP2", 4);
    Put(data + 4U, plan->domain, 4U);
    Put(data + 8U, plan->authority_epoch, 4U);
    Put(data + 12U, plan->sequence, 4U);
    data[16] = plan->mode;
    data[17] = plan->flags;
    Put(data + 18U, plan->reserved, 2U);
    Put(data + 20U, plan->plan_common_ns, 8U);
    Put(data + 28U, plan->window_start_ns, 8U);
    Put(data + 36U, plan->window_span_ns, 8U);
    Put(data + 44U, plan->period_milli_hz, 4U);
    Put(data + 48U, plan->display_deadline_ns, 8U);
    return true;
}
/** @brief 解码并拒绝非零保留字段。 @param data 输入字节。 @param length 必须56。 @param out 输出副本，失败不改。 @return 成功true。 */
bool WaveformPlanWire_Decode(const uint8_t *data, uint16_t length, WaveformPlan *out)
{
    if (data == NULL || out == NULL || length != WAVEFORM_PLAN_WIRE_BYTES ||
        memcmp(data, "WVP2", 4) != 0) return false;
    WaveformPlan plan;
    memset(&plan, 0, sizeof plan);
    plan.domain = (uint32_t)Get(data + 4U, 4U);
    plan.authority_epoch = (uint32_t)Get(data + 8U, 4U);
    plan.sequence = (uint32_t)Get(data + 12U, 4U);
    plan.mode = data[16];
    plan.flags = data[17];
    plan.reserved = (uint16_t)Get(data + 18U, 2U);
    plan.plan_common_ns = Get(data + 20U, 8U);
    plan.window_start_ns = Get(data + 28U, 8U);
    plan.window_span_ns = Get(data + 36U, 8U);
    plan.period_milli_hz = (uint32_t)Get(data + 44U, 4U);
    plan.display_deadline_ns = Get(data + 48U, 8U);
    if (!ValidPlan(&plan)) return false;
    *out = plan;
    return true;
}
