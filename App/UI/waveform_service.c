/**
 * @file waveform_service.c
 * @brief 复制已验证PCM到CPU历史环，以A计划绘制两板相同的共同模型时间窗。
 * @details 单前台运行；Stage只复制左声道，Complete通过才发布，UI不持有DMA指针。
 *          两锚点经AudioTimeMap校验后用中点仿射模型逆算帧，最多外推1.5秒，
 *          仅用于波形观察，不输出声学测量精度。零基PCM帧n映射NDTR进度n+1。
 * @note .WaveformState为CPU独占SDRAM NOLOAD；Init先清元数据，数组写后才读。
 */
#include "waveform_service.h"
#include <stddef.h>
#include <string.h>

#define REFRESH_MS 200U
#define STREAM_TIMEOUT_MS 650U
#define PLAN_TIMEOUT_MS 650U
#define HISTORY_DELAY_NS UINT64_C(350000000)
#define DISPLAY_LEAD_NS UINT64_C(100000000)
#define MODEL_HOLD_NS UINT64_C(1500000000)
#define FIXED_SPAN_NS 10000000U
#define NS_PER_SEC UINT64_C(1000000000)

/** @brief 仅CPU访问的大数组；DMA从不写此区，因而无需增加缓存维护。 */
typedef struct {
    int16_t ring[WAVEFORM_RING_FRAMES];
    int16_t stage[BSP_AUDIO_HALF_FRAMES];
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
    BoardRole role;
    uint32_t stage_session, stage_count, session, last_block_ms, refresh_ms;
    uint32_t sequence, received_sequence, plan_ms, anchor_ms;
    uint64_t stage_first, first_frame, end_frame, common_ns;
    RangingSessionSnapshot domain;
    AudioTimeMapAnchor left, right;
    double model_frame, model_ns, frames_per_ns;
    WaveformPlan plan;
    WaveformSnapshot snapshot;
} state;

/** @brief 发布不可用状态，撤掉旧点资格并增加界面代数；不读数组。 @return 无返回值。 */
static void Invalidate(void)
{
    if (state.snapshot.valid || state.snapshot.synced) {
        uint32_t seq = state.snapshot.frame_seq + 1U;
        memset(&state.snapshot, 0, sizeof state.snapshot);
        state.snapshot.frame_seq = seq;
    }
}

/** @brief 清理PCM连续性，防止断流/End失败后显示拼接历史。 @return 无返回值。 */
static void ClearHistory(void)
{
    state.have_ring = false;
    state.pending = false;
    state.outgoing = false;
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
        Invalidate();
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
        Invalidate();
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

/** @brief 计算周期候选的归一化差分能量。 @param start 已确认在环内的起点。 @param lag 周期帧数。 @return 0为高度重复，1附近为不相关。 */
static float PeriodError(uint64_t start, uint32_t lag)
{
    float error = 0.0f, energy = 0.0f, sum_a = 0.0f, sum_b = 0.0f;
    for (uint32_t i = 0U; i < 512U; i += 4U) {
        float a = Sample(start + i), b = Sample(start + i + lag);
        float difference = a - b;
        error += difference * difference;
        energy += a * a + b * b;
        sum_a += a;
        sum_b += b;
    }
    energy -= (sum_a * sum_a + sum_b * sum_b) / 128.0f;
    return energy > 128.0f * 200.0f * 200.0f ? error / energy : 2.0f;
}

/** @brief A从历史PCM寻找首个强自相关周期，不使用B相位。 @param start 窗口附近帧。 @return 24..960帧周期，0为无可靠周期。 */
static uint32_t FindPeriod(uint64_t start)
{
    if (start + 1473U >= state.end_frame) return 0U;
    /* 先看到真实短周期再判断范围，避免把4kHz的二倍周期误报为2kHz。 */
    float previous = PeriodError(start, 1U), current = PeriodError(start, 2U);
    for (uint32_t lag = 2U; lag <= 960U; ++lag) {
        float next = PeriodError(start, lag + 1U);
        if (current < 0.04f && current < previous && current <= next)
            return lag >= 24U ? lag : 0U;
        previous = current;
        current = next;
    }
    return 0U;
}

/** @brief 复制有界CPU窗口；仅调用方确认环及模型资格后执行。 @param first 起始帧，可带小数。 @param span 帧跨度。 @param plan 共同计划，NULL表示本地自由运行。 @return 整窗在环内才true。 */
static bool Render(double first, double span, const WaveformPlan *plan)
{
    if (first < (double)state.first_frame || span < 1.0 ||
        first + span >= (double)state.end_frame) return false;
    WaveformSnapshot *out = &state.snapshot;
    for (uint32_t i = 0U; i < WAVEFORM_POINT_COUNT; ++i) {
        double position = first + span * i / (WAVEFORM_POINT_COUNT - 1U);
        uint64_t frame = (uint64_t)position;
        out->points[i] = Sample(frame);
    }
    out->valid = true;
    out->synced = plan != NULL;
    out->period_valid = plan != NULL && plan->period_milli_hz != 0U;
    out->period_milli_hz = plan != NULL ? plan->period_milli_hz : 0U;
    out->window_start_ns = plan != NULL ? plan->window_start_ns : 0U;
    out->window_span_ns = plan != NULL ? plan->window_span_ns : FIXED_SPAN_NS;
    out->plan_sequence = plan != NULL ? plan->sequence : 0U;
    out->point_count = WAVEFORM_POINT_COUNT;
    out->frame_seq++;
    return true;
}

/** @brief A生成200ms节流共同窗及未来显示deadline，稳定周期时按A过零对齐。 @return 无返回值。 */
static void MakePlan(void)
{
    if (state.common_ns <= HISTORY_DELAY_NS) return;
    uint64_t start_ns = state.common_ns - HISTORY_DELAY_NS;
    if (start_ns < state.domain.source_floor_ns) return;
    double first = FrameAt(start_ns);
    if (first < (double)state.first_frame || first + 1500.0 >= (double)state.end_frame) return;
    uint32_t period = FindPeriod((uint64_t)first);
    WaveformPlan plan = {0};
    plan.domain = state.domain.domain;
    plan.authority_epoch = state.domain.authority_epoch;
    plan.sequence = ++state.sequence;
    plan.display_deadline_ns = state.common_ns + DISPLAY_LEAD_NS;
    plan.window_start_ns = start_ns;
    plan.window_span_ns = FIXED_SPAN_NS;
    if (period != 0U) {
        uint64_t start_frame = (uint64_t)first;
        for (uint32_t i = 0U; i < period; ++i) {
            if (Sample(start_frame + i) <= 0 && Sample(start_frame + i + 1U) > 0) {
                /* 仅A寻找参考上升过零；B直接逆算相同共同起点，保留声学相移。 */
                double delta_ns = ((double)(start_frame + i + 1U) - first) / state.frames_per_ns;
                plan.window_start_ns += (uint64_t)(delta_ns + 0.5);
                break;
            }
        }
        plan.window_span_ns = (uint32_t)(3.0 * period / state.frames_per_ns);
        plan.period_milli_hz = (uint32_t)(state.frames_per_ns * 1.0e12 / period + 0.5);
    }
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
    bool qualified = healthy && session != NULL && session->established &&
        session->domain != 0U && session->authority_epoch != 0U && common_ns != 0U;
    bool changed = state.role != role || qualified != state.healthy ||
        (qualified && (state.domain.domain != session->domain ||
         state.domain.authority_epoch != session->authority_epoch || common_ns < state.common_ns));
    if (changed) {
        state.pending = false;
        state.outgoing = false;
        state.have_sequence = false;
        state.have_refresh = false;
        Invalidate();
        /* 失域/校时后必须重新建立两个锚点，旧模型不能恢复共同显示资格。 */
        if (state.healthy) {
            state.model_valid = false;
            state.have_left = false;
        }
    }
    state.role = role;
    state.healthy = qualified;
    state.common_ns = common_ns;
    if (session != NULL) state.domain = *session;
    if (!state.have_ring || (uint32_t)(now_ms - state.last_block_ms) > STREAM_TIMEOUT_MS) {
        ClearHistory();
        return;
    }
    bool model_fresh = state.model_valid &&
        (uint32_t)(now_ms - state.anchor_ms) <= MODEL_HOLD_NS / 1000000U &&
        (double)common_ns - state.model_ns <= (double)MODEL_HOLD_NS;
    if (state.snapshot.synced && (uint32_t)(now_ms - state.plan_ms) > PLAN_TIMEOUT_MS)
        Invalidate();
    if (qualified && model_fresh && state.pending && common_ns >= state.plan.display_deadline_ns) {
        if ((uint32_t)(now_ms - state.plan_ms) <= PLAN_TIMEOUT_MS &&
            Render(FrameAt(state.plan.window_start_ns),
                state.plan.window_span_ns * state.frames_per_ns, &state.plan)) {
            state.pending = false;
        } else {
            state.pending = false;
            Invalidate();
        }
    }
    if (state.have_refresh && (uint32_t)(now_ms - state.refresh_ms) < REFRESH_MS) return;
    state.have_refresh = true;
    state.refresh_ms = now_ms;
    if (qualified && model_fresh && role == BOARD_ROLE_A) MakePlan();
    if (!qualified || !model_fresh || role != BOARD_ROLE_A) {
        bool common_display = qualified && model_fresh &&
            (state.pending || (state.snapshot.synced && (uint32_t)(now_ms - state.plan_ms) <= PLAN_TIMEOUT_MS));
        if (!common_display) {
            double span = 480.0;
            if (state.end_frame - state.first_frame > 481U)
                (void)Render((double)state.end_frame - span - 1.0, span, NULL);
            else Invalidate();
        }
    }
}

/** @brief 获取CPU副本，不授予PCM指针。 @param out 可NULL输出。 @return 无返回值。 */
void WaveformService_GetSnapshot(WaveformSnapshot *out)
{
    if (out != NULL) *out = state.snapshot;
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
    return plan->domain != 0U && plan->authority_epoch != 0U &&
        plan->window_start_ns != 0U && plan->window_span_ns >= 1000000U &&
        plan->window_span_ns <= 100000000U &&
        plan->window_start_ns < plan->display_deadline_ns &&
        plan->display_deadline_ns - plan->window_start_ns <= NS_PER_SEC &&
        plan->window_span_ns < plan->display_deadline_ns - plan->window_start_ns &&
        (plan->period_milli_hz == 0U || (plan->period_milli_hz >= 40000U && plan->period_milli_hz <= 2100000U));
}

/** @brief B接收域和新序号均匹配的计划，重复包不修改寿命。 @param plan 只读副本。 @param now_ms 接收毫秒。 @return 接受新计划true。 */
bool WaveformService_OnPlan(const WaveformPlan *plan, uint32_t now_ms)
{
    if (plan == NULL || state.role != BOARD_ROLE_B || !state.healthy ||
        plan->domain != state.domain.domain || plan->authority_epoch != state.domain.authority_epoch ||
        plan->window_start_ns < state.domain.source_floor_ns ||
        plan->display_deadline_ns <= state.common_ns ||
        plan->display_deadline_ns - state.common_ns > DISPLAY_LEAD_NS + UINT64_C(100000000) ||
        (state.have_sequence && (int32_t)(plan->sequence - state.received_sequence) <= 0)) return false;
    state.plan = *plan;
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
/** @brief 编码固定44字节WVP1。 @param plan 只读输入。 @param data 可写输出。 @param length 必须44。 @return 成功true。 */
bool WaveformPlanWire_Encode(const WaveformPlan *plan, uint8_t *data, uint16_t length)
{
    if (plan == NULL || data == NULL || length != WAVEFORM_PLAN_WIRE_BYTES || !ValidPlan(plan)) return false;
    memcpy(data, "WVP1", 4); Put(data+4, plan->domain, 4); Put(data+8, plan->authority_epoch, 4);
    Put(data+12, plan->sequence, 4); Put(data+16, plan->display_deadline_ns, 8);
    Put(data+24, plan->window_start_ns, 8); Put(data+32, plan->window_span_ns, 4);
    Put(data+36, plan->period_milli_hz, 4); Put(data+40, 0, 4);
    return true;
}
/** @brief 解码并拒绝非零保留字段。 @param data 输入字节。 @param length 必须44。 @param out 输出副本，失败不改。 @return 成功true。 */
bool WaveformPlanWire_Decode(const uint8_t *data, uint16_t length, WaveformPlan *out)
{
    if (data == NULL || out == NULL || length != WAVEFORM_PLAN_WIRE_BYTES ||
        memcmp(data, "WVP1", 4) != 0 || Get(data+40, 4) != 0U) return false;
    WaveformPlan plan;
    plan.domain = (uint32_t)Get(data+4,4); plan.authority_epoch = (uint32_t)Get(data+8,4);
    plan.sequence = (uint32_t)Get(data+12,4); plan.display_deadline_ns = Get(data+16,8);
    plan.window_start_ns = Get(data+24,8); plan.window_span_ns = (uint32_t)Get(data+32,4);
    plan.period_milli_hz = (uint32_t)Get(data+36,4);
    if (!ValidPlan(&plan)) return false;
    *out = plan;
    return true;
}
