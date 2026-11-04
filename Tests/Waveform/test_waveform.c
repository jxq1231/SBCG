/** @file test_waveform.c @brief 验证真实PCM交接、共同20ms窗口、LOCAL回退与WVP2线格式。 */
#include "UI/waveform_service.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS];
static uint32_t noise_seed = 1U;
static double tone_period_frames = 96.0;
static RangingSessionSnapshot domain = {true, 7, 9, 1};
static uint64_t next_frame;
static uint32_t next_anchor_ms;
static int32_t sample_bias;
static bool force_silence;

/** @brief PERIOD按3个周期显示时的期望窗口；与实现同样的1ms..20ms限幅。 */
static uint64_t ExpectedPeriodSpan(uint32_t mhz)
{
    double span = 1.0e12 / (double)mhz * (double)WAVEFORM_PERIOD_DISPLAY_CYCLES;
    if (span < (double)WAVEFORM_PERIOD_SPAN_MIN_NS) span = (double)WAVEFORM_PERIOD_SPAN_MIN_NS;
    if (span > (double)WAVEFORM_WINDOW_SPAN_NS) span = (double)WAVEFORM_WINDOW_SPAN_NS;
    return (uint64_t)(span + 0.5);
}

static uint64_t SpanDiff(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }

/** @brief 提交一个连续DMA半块；phase<0表示确定性噪声。 */
static void Feed(uint64_t frame, double phase, bool accepted, uint32_t ms)
{
    BSP_AudioBlock block = {0};
    for (unsigned i = 0U; i < BSP_AUDIO_HALF_FRAMES; ++i) {
        noise_seed = noise_seed * 1664525U + 1013904223U;
        pcm[2U * i] = force_silence ? 0 : (phase < 0.0 ? (int16_t)(noise_seed >> 16) :
            (int16_t)(sample_bias + 12000.0 * sin((double)(frame + i) *
                6.283185307179586 / tone_period_frames + phase)));
        pcm[2U * i + 1U] = 0;
    }
    block.samples = pcm;
    block.frame_count = BSP_AUDIO_HALF_FRAMES;
    block.first_frame = frame;
    block.session = 1U;
    block.index_valid = true;
    block.contiguous = frame != 0U;
    WaveformService_StageBlock(&block);
    memset(pcm, 0, sizeof pcm);
    WaveformService_CompleteBlock(accepted, ms);
}

/** @brief 两个整秒锚点，PCM零基帧n对应NDTR进度n+1。 */
static void Anchors(void)
{
    AudioTimeAnchorObservation anchor = {0};
    anchor.anchor.valid = true;
    anchor.anchor.session = 1U;
    anchor.sequence = 1U;
    anchor.captured_ms = 1000U;
    anchor.anchor.frame_lo = anchor.anchor.frame_hi = 48000U;
    anchor.anchor.sec = 11U;
    WaveformService_ObserveAnchor(&anchor);
    anchor.sequence = 2U;
    anchor.captured_ms = 2000U;
    anchor.anchor.frame_lo = anchor.anchor.frame_hi = 96000U;
    anchor.anchor.sec = 12U;
    WaveformService_ObserveAnchor(&anchor);
}

/** @brief 65536..114688帧历史，跨越两个8192帧半块。 */
static void History(double phase)
{
    for (uint64_t frame = 65536U; frame < 114688U; frame += BSP_AUDIO_HALF_FRAMES)
        Feed(frame, phase, true, (uint32_t)(frame / 48U));
    next_frame = 114688U;
    next_anchor_ms = 3000U;
}

/** @brief 连续推进实际PCM历史与每秒锚点；不冻结历史来伪造迟包可用性。 @param ms 当前毫秒。 @param phase 新半块正弦相位，负数为噪声。 @return 无返回值。 */
static void Progress(uint32_t ms, double phase)
{
    while (next_frame + BSP_AUDIO_HALF_FRAMES <= (uint64_t)ms * 48U) {
        Feed(next_frame, phase, true, (uint32_t)((next_frame + BSP_AUDIO_HALF_FRAMES) / 48U));
        next_frame += BSP_AUDIO_HALF_FRAMES;
    }
    while (next_anchor_ms <= ms) {
        AudioTimeAnchorObservation anchor = {0};
        anchor.anchor.valid = true;
        anchor.anchor.session = 1U;
        anchor.anchor.frame_lo = anchor.anchor.frame_hi = (uint64_t)next_anchor_ms * 48U;
        anchor.anchor.sec = 10U + next_anchor_ms / 1000U;
        anchor.captured_ms = next_anchor_ms;
        WaveformService_ObserveAnchor(&anchor);
        next_anchor_ms += 1000U;
    }
}

/** @brief 驱动A一次计划，时钟保持与原fixture相同的100ms前台读时余量。 @param ms 当前毫秒。 @param phase 新PCM相位/噪声选择。 @return 本次计划。 */
static WaveformPlan PlanAt(uint32_t ms, double phase)
{
    Progress(ms, phase);
    WaveformService_Update(BOARD_ROLE_A, true, &domain,
        UINT64_C(9900000000) + (uint64_t)ms * UINT64_C(1000000), ms);
    WaveformPlan plan;
    assert(WaveformService_TakePlan(&plan));
    return plan;
}

/** @brief 用三个分离的可靠候选建立初始周期；前两次必须保持RAW。 @param plan 非NULL输出第三次计划。 @return 无返回值。 */
static void ConfirmPeriod(WaveformPlan *plan)
{
    *plan = PlanAt(2400U, 0.0);
    assert(plan->mode == WAVEFORM_MODE_RAW);
    *plan = PlanAt(2600U, 0.0);
    assert(plan->mode == WAVEFORM_MODE_RAW);
    *plan = PlanAt(2800U, 0.0);
    if (plan->mode != WAVEFORM_MODE_PERIOD) {
        WaveformSnapshot diagnostic; WaveformService_GetSnapshot(&diagnostic);
        fprintf(stderr, "confirm failed: frames=%.6f bias=%ld candidate=%lu count=%u reason=%u\n",
            tone_period_frames, (long)sample_bias, (unsigned long)diagnostic.candidate_milli_hz,
            (unsigned)diagnostic.confirmation_count, (unsigned)diagnostic.reason);
    }
    assert(plan->mode == WAVEFORM_MODE_PERIOD);
}

/** @brief 验证Init前不访问数组、断流失效、无模型时LOCAL仍发布原始PCM。 */
static void CheckInitAndLocal(void)
{
    WaveformSnapshot snapshot;
    WaveformPlan plan;
    BSP_AudioBlock before_init = {0};
    before_init.samples = (const volatile int16_t *)(uintptr_t)1;
    before_init.frame_count = BSP_AUDIO_HALF_FRAMES;
    before_init.index_valid = true;
    before_init.session = 1U;
    WaveformService_StageBlock(&before_init);
    WaveformService_CompleteBlock(true, 10U);
    WaveformService_Update(BOARD_ROLE_A, true, &domain, UINT64_C(12300000000), 20U);
    WaveformService_GetSnapshot(&snapshot);
    assert(!snapshot.valid && snapshot.frame_seq == 0U);
    assert(!WaveformService_TakePlan(&plan));

    WaveformService_Init();
    Feed(0U, -1.0, false, 0U);
    WaveformService_Update(BOARD_ROLE_A, false, NULL, 0U, 200U);
    WaveformService_GetSnapshot(&snapshot);
    assert(!snapshot.valid);
    Feed(BSP_AUDIO_HALF_FRAMES, -1.0, true, 210U);
    WaveformService_Update(BOARD_ROLE_A, false, NULL, 0U, 410U);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.valid && !snapshot.synced && snapshot.source == WAVEFORM_SOURCE_LOCAL);
    assert(snapshot.point_count > 0U);
}

/** @brief 验证A固定20ms RAW计划、WVP2 56B、B本地20ms原始回退与B跟随共同窗口。 */
static void CheckRawWindow(void)
{
    WaveformSnapshot a, b;
    WaveformPlan plan, decoded;
    uint8_t wire[WAVEFORM_PLAN_WIRE_BYTES];

    WaveformService_Init();
    History(-1.0);
    Anchors();
    WaveformService_Update(BOARD_ROLE_A, true, &domain, UINT64_C(12300000000), 2400U);
    assert(WaveformService_TakePlan(&plan));
    assert(plan.mode == WAVEFORM_MODE_RAW);
    assert(plan.period_milli_hz == 0U);
    assert(plan.window_span_ns == WAVEFORM_WINDOW_SPAN_NS);
    assert(plan.plan_common_ns - plan.window_start_ns >= WAVEFORM_WINDOW_SPAN_NS);
    assert(plan.display_deadline_ns - plan.plan_common_ns == UINT64_C(100000000));
    assert(WaveformPlanWire_Encode(&plan, wire, sizeof wire));
    assert(WaveformPlanWire_Decode(wire, sizeof wire, &decoded));
    assert(decoded.sequence == plan.sequence && decoded.window_start_ns == plan.window_start_ns);
    assert(decoded.window_span_ns == plan.window_span_ns && decoded.mode == plan.mode);
    wire[0] = 0U;
    assert(!WaveformPlanWire_Decode(wire, sizeof wire, &decoded));

    WaveformService_Update(BOARD_ROLE_A, true, &domain, plan.display_deadline_ns, 2500U);
    WaveformService_GetSnapshot(&a);
    assert(a.valid && a.synced && a.source == WAVEFORM_SOURCE_COMMON);
    assert(a.plan_sequence == plan.sequence);
    assert(a.window_start_ns == plan.window_start_ns);
    assert(a.window_span_ns == WAVEFORM_WINDOW_SPAN_NS);
    assert(a.point_count >= WAVEFORM_POINT_COUNT - 2U && a.point_count <= WAVEFORM_POINT_COUNT);
    assert(a.points[0] != 0 || a.points[10] != 0);

    /* B无计划时仍显示本板最近原始PCM，且不画零线。 */
    WaveformService_Init();
    History(1.5707963267948966);
    Anchors();
    WaveformService_Update(BOARD_ROLE_B, true, &domain, UINT64_C(12300000000), 2400U);
    WaveformService_GetSnapshot(&b);
    assert(b.valid && !b.synced && b.source == WAVEFORM_SOURCE_LOCAL);
    assert(b.window_span_ns == WAVEFORM_WINDOW_SPAN_NS || b.window_span_ns > 0U);
    assert(b.points[0] != 0 || b.points[10] != 0);
    assert(WaveformService_OnPlan(&plan, 2400U));
    WaveformService_Update(BOARD_ROLE_B, true, &domain, plan.display_deadline_ns, 2500U);
    WaveformService_GetSnapshot(&b);
    assert(b.valid && b.synced && b.source == WAVEFORM_SOURCE_COMMON);
    assert(b.window_start_ns == a.window_start_ns && b.window_span_ns == a.window_span_ns);
    assert(memcmp(a.points, b.points, sizeof a.points) != 0);

    /* 旧序号/换域不得刷新已收计划；超时退为LOCAL真PCM。 */
    WaveformPlan old = plan;
    old.sequence++;
    RangingSessionSnapshot changed = domain;
    changed.authority_epoch++;
    WaveformService_Update(BOARD_ROLE_B, true, &changed, UINT64_C(12310000000), 2600U);
    assert(!WaveformService_OnPlan(&plan, 2600U));
    Feed(114688U, 1.5707963267948966, true, 3000U);
    WaveformService_Update(BOARD_ROLE_B, true, &changed, UINT64_C(12320000000), 3200U);
    WaveformService_GetSnapshot(&b);
    assert(b.valid && !b.synced && b.source == WAVEFORM_SOURCE_LOCAL);
}

/** @brief 验证500Hz周期信号进入PERIOD模式、周期字段与约3周期窗口。 */
static void CheckPeriodicPlan(void)
{
    WaveformSnapshot snapshot;
    WaveformPlan plan;
    WaveformService_Init();
    History(0.0);
    Anchors();
    ConfirmPeriod(&plan);
    assert(plan.mode == WAVEFORM_MODE_PERIOD);
    assert(plan.period_milli_hz >= 490000U && plan.period_milli_hz <= 510000U);
    assert(SpanDiff(plan.window_span_ns, ExpectedPeriodSpan(plan.period_milli_hz)) <= 2000U);
    assert(plan.window_span_ns < WAVEFORM_WINDOW_SPAN_NS);
    assert(plan.window_start_ns + plan.window_span_ns <= plan.plan_common_ns - UINT64_C(350000000));
    WaveformService_Update(BOARD_ROLE_A, true, &domain, plan.display_deadline_ns, 2900U);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.valid && snapshot.synced && snapshot.period_valid);
    assert(snapshot.period_milli_hz == plan.period_milli_hz);
    assert(snapshot.points[0] != 0 || snapshot.points[10] != 0);

    /* 相位网格跨计划只按整数周期推进；新输入块不重置参考。 */
    WaveformPlan next = PlanAt(3000U, 0.0);
    assert(next.mode == WAVEFORM_MODE_PERIOD);
    const double period_ns = 1.0e12 / (double)next.period_milli_hz;
    const double delta = (double)next.window_start_ns - (double)plan.window_start_ns;
    const double cycles = delta / period_ns;
    assert(fabs(cycles - floor(cycles + 0.5)) * period_ns < 2000.0);
}

/** @brief 验证非整数帧周期1882.35Hz仍进入PERIOD且周期估计误差在2%内。 */
static void CheckFractionalPlan(void)
{
    WaveformPlan plan;
    tone_period_frames = 48000.0 / 1882.35;
    WaveformService_Init();
    History(0.0);
    Anchors();
    ConfirmPeriod(&plan);
    assert(plan.mode == WAVEFORM_MODE_PERIOD);
    assert(plan.period_milli_hz >= 1840000U && plan.period_milli_hz <= 1920000U);
    assert(SpanDiff(plan.window_span_ns, ExpectedPeriodSpan(plan.period_milli_hz)) <= 2000U);
    tone_period_frames = 96.0;
}

/** @brief 验证10Hz计划节流、计划截止调度和NotfiyClockChange撤销旧共同显示。 */
static void CheckScheduling(void)
{
    WaveformSnapshot snapshot;
    WaveformPlan first, second;
    WaveformService_Init();
    History(0.0);
    Anchors();
    WaveformService_Update(BOARD_ROLE_A, true, &domain, UINT64_C(12300000000), 2400U);
    assert(WaveformService_TakePlan(&first));
    WaveformService_Update(BOARD_ROLE_A, true, &domain, UINT64_C(12305000000), 2450U);
    assert(!WaveformService_TakePlan(&second)); /* 未到100ms */
    WaveformService_Update(BOARD_ROLE_A, true, &domain, UINT64_C(12310000000), 2500U);
    assert(WaveformService_TakePlan(&second));
    assert(second.sequence > first.sequence);
    WaveformService_Update(BOARD_ROLE_A, true, &domain, second.display_deadline_ns, 2500U);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.synced && snapshot.plan_sequence == second.sequence);

    WaveformService_NotifyClockChange();
    WaveformService_Update(BOARD_ROLE_A, true, &domain, UINT64_C(12610000000), 2710U);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.valid && !snapshot.synced && snapshot.source == WAVEFORM_SOURCE_LOCAL);
    assert(!WaveformService_TakePlan(&second));
}

/** @brief 验证Wire长度、保留字段和非法计划入口。 */
static void CheckWireAndLimits(void)
{
    WaveformPlan plan = {0}, decoded;
    uint8_t wire[WAVEFORM_PLAN_WIRE_BYTES + 1U];
    plan.domain = 7U;
    plan.authority_epoch = 9U;
    plan.sequence = 1U;
    plan.mode = WAVEFORM_MODE_RAW;
    plan.plan_common_ns = UINT64_C(12300000000);
    plan.window_start_ns = UINT64_C(12280000000);
    plan.window_span_ns = WAVEFORM_WINDOW_SPAN_NS;
    plan.display_deadline_ns = UINT64_C(12400000000);
    assert(WaveformPlanWire_Encode(&plan, wire, WAVEFORM_PLAN_WIRE_BYTES));
    for (unsigned length = 0U; length < WAVEFORM_PLAN_WIRE_BYTES; ++length)
        assert(!WaveformPlanWire_Decode(wire, (uint16_t)length, &decoded));
    wire[18] = 1U; /* reserved */
    assert(!WaveformPlanWire_Decode(wire, WAVEFORM_PLAN_WIRE_BYTES, &decoded));
    wire[18] = 0U;
    wire[16] = WAVEFORM_MODE_PERIOD; /* PERIOD但period=0 */
    assert(!WaveformPlanWire_Decode(wire, WAVEFORM_PLAN_WIRE_BYTES, &decoded));
    BSP_AudioBlock oversized = {0};
    oversized.samples = pcm;
    oversized.session = 1U;
    oversized.index_valid = true;
    oversized.frame_count = BSP_AUDIO_HALF_FRAMES + 1U;
    WaveformService_StageBlock(&oversized);
    WaveformService_CompleteBlock(true, 2501U);
}

/** @brief 验证B失去共同计划后LOCAL沿用最近共同周期，显示约3个周期而非固定20ms。 */
static void CheckLocalPeriodSpan(void)
{
    WaveformSnapshot snapshot;
    WaveformPlan plan;
    WaveformService_Init();
    History(0.0);
    Anchors();
    ConfirmPeriod(&plan);
    assert(plan.mode == WAVEFORM_MODE_PERIOD);

    WaveformService_Init();
    History(1.5707963267948966);
    Anchors();
    Progress(2800U, 1.5707963267948966);
    WaveformService_Update(BOARD_ROLE_B, true, &domain, UINT64_C(12700000000), 2800U);
    assert(WaveformService_OnPlan(&plan, 2800U));
    WaveformService_Update(BOARD_ROLE_B, true, &domain, plan.display_deadline_ns, 2900U);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.synced && snapshot.source == WAVEFORM_SOURCE_COMMON);

    Progress(3100U, 1.5707963267948966);
    RangingSessionSnapshot changed = domain;
    changed.authority_epoch++;
    WaveformService_Update(BOARD_ROLE_B, false, &changed, 0U, 3100U);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.valid && !snapshot.synced && snapshot.source == WAVEFORM_SOURCE_LOCAL);
    assert(snapshot.period_valid && snapshot.period_milli_hz == plan.period_milli_hz);
    assert(SpanDiff(snapshot.window_span_ns, ExpectedPeriodSpan(plan.period_milli_hz)) <= 2000U);
    assert(snapshot.window_span_ns < WAVEFORM_WINDOW_SPAN_NS);
    Progress(3400U, 1.5707963267948966);
    WaveformService_Update(BOARD_ROLE_B, false, NULL, 0U, 3400U);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.valid && snapshot.period_valid && !snapshot.synced);
    assert(SpanDiff(snapshot.window_span_ns, ExpectedPeriodSpan(plan.period_milli_hz)) <= 2000U);
    Progress(10800U, 1.5707963267948966);
    WaveformService_Update(BOARD_ROLE_B, false, NULL, 0U, 10800U);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.valid && !snapshot.period_valid && !snapshot.synced);
    assert(snapshot.window_span_ns == WAVEFORM_WINDOW_SPAN_NS);
}

/** @brief DC使过零不可用时，2/4/5倍整数滞后仍不得替代真实基周期。 */
static void CheckShortestPeriod(void)
{
    const double periods[] = {48.5, 48.25, 48.2};
    for (unsigned i = 0U; i < sizeof periods / sizeof periods[0]; ++i) {
        tone_period_frames = periods[i];
        sample_bias = 15000;
        WaveformService_Init(); History(0.0); Anchors();
        WaveformPlan plan; ConfirmPeriod(&plan);
        const double expected_mhz = 48000000.0 / periods[i];
        assert(fabs((double)plan.period_milli_hz - expected_mhz) < expected_mhz * 0.005);
    }
    sample_bias = 0; tone_period_frames = 96.0;
}

/** @brief 失败及等待不续租A周期证据；600ms后计划转RAW，但LOCAL显示横轴仍保留最近周期。 */
static void CheckPeriodHold(void)
{
    tone_period_frames = 48.0;
    WaveformService_Init(); History(0.0); Anchors();
    WaveformPlan plan; ConfirmPeriod(&plan);
    force_silence = true;
    plan = PlanAt(3000U, 0.0); /* 此时分析的历史仍是切换前正弦。 */
    assert(plan.mode == WAVEFORM_MODE_PERIOD);
    plan = PlanAt(3200U, 0.0); assert(plan.mode == WAVEFORM_MODE_PERIOD);
    plan = PlanAt(3300U, 0.0); assert(plan.mode == WAVEFORM_MODE_PERIOD);
    WaveformSnapshot snapshot; WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.period_age_ms == 300U);
    plan = PlanAt(3400U, 0.0); assert(plan.mode == WAVEFORM_MODE_PERIOD);
    plan = PlanAt(3500U, 0.0); assert(plan.mode == WAVEFORM_MODE_PERIOD);
    plan = PlanAt(3600U, 0.0); assert(plan.mode == WAVEFORM_MODE_RAW);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.period_age_ms == 600U);
    assert(snapshot.period_valid);
    assert(snapshot.period_milli_hz != 0U);
    assert(SpanDiff(snapshot.window_span_ns, ExpectedPeriodSpan(snapshot.period_milli_hz)) <= 2000U);
    assert(snapshot.confirmation_count == 0U);
    force_silence = false; tone_period_frames = 96.0;
}

/** @brief 同频相位改变不重取A过零；持续1%真实频变须经过三个新候选更新。 */
static void CheckPhaseAndFrequencyChange(void)
{
    tone_period_frames = 48.0;
    WaveformService_Init(); History(0.0); Anchors();
    WaveformPlan original; ConfirmPeriod(&original);
    WaveformPlan plan = original;
    for (uint32_t ms = 3000U; ms <= 3800U; ms += 200U) {
        plan = PlanAt(ms, 1.5707963267948966);
        assert(plan.mode == WAVEFORM_MODE_PERIOD);
        const double cycles = (double)(plan.window_start_ns - original.window_start_ns) / 1000000.0;
        assert(fabs(cycles - floor(cycles + 0.5)) < 0.002);
    }
    tone_period_frames = 48000.0 / 1010.0;
    bool revised = false;
    for (uint32_t ms = 4000U; ms <= 4800U; ms += 200U) {
        plan = PlanAt(ms, 1.5707963267948966);
        WaveformSnapshot diagnostic; WaveformService_GetSnapshot(&diagnostic);
        if (!revised && diagnostic.candidate_milli_hz > 1007000U) {
            if (diagnostic.confirmation_count < WAVEFORM_PERIOD_CONFIRMATIONS)
                assert(plan.mode == WAVEFORM_MODE_RAW || plan.period_milli_hz < 1003000U);
            else {
                assert(plan.period_milli_hz > 1007000U);
                revised = true;
            }
        }
    }
    assert(revised);
    assert(plan.mode == WAVEFORM_MODE_PERIOD);
    assert(plan.period_milli_hz > 1007000U && plan.period_milli_hz < 1013000U);
    tone_period_frames = 96.0;
}

/** @brief 噪声和尖峰必须逐样本保真，不能靠显示算法把真实瞬态抹去。 */
static void CheckRawFidelity(void)
{
    int16_t expected[WAVEFORM_POINT_COUNT];
    for (uint32_t i = 0; i < BSP_AUDIO_HALF_FRAMES; ++i) {
        noise_seed = noise_seed * 1664525U + 1013904223U;
        pcm[2U * i] = i % 97U == 0U ? INT16_MAX : (int16_t)(noise_seed >> 16U);
        pcm[2U * i + 1U] = -123;
        if (i >= BSP_AUDIO_HALF_FRAMES - WAVEFORM_POINT_COUNT)
            expected[i - (BSP_AUDIO_HALF_FRAMES - WAVEFORM_POINT_COUNT)] = pcm[2U * i];
    }
    BSP_AudioBlock block = {.samples=pcm,.frame_count=BSP_AUDIO_HALF_FRAMES,
        .first_frame=0U,.session=1U,.index_valid=true};
    WaveformService_Init(); WaveformService_StageBlock(&block);
    memset(pcm, 0, sizeof pcm); WaveformService_CompleteBlock(true, 100U);
    WaveformService_Update(BOARD_ROLE_B, false, NULL, 0U, 100U);
    WaveformSnapshot snapshot; WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.valid && !snapshot.synced && snapshot.point_count == WAVEFORM_POINT_COUNT);
    assert(memcmp(expected, snapshot.points, sizeof expected) == 0);
}

/** @brief RAW保留B最近横轴；未同步期间换域也保留最近LOCAL横轴，迟包不能越过已覆盖的真实历史。 */
static void CheckCacheContextAndLateHistory(void)
{
    WaveformPlan plan;
    WaveformService_Init(); History(0.0); Anchors(); ConfirmPeriod(&plan);
    WaveformService_Init(); History(0.0); Anchors(); Progress(2800U, 0.0);
    WaveformService_Update(BOARD_ROLE_B, true, &domain, UINT64_C(12700000000), 2800U);
    assert(WaveformService_OnPlan(&plan, 2800U));
    WaveformPlan raw = plan; raw.sequence++; raw.mode = WAVEFORM_MODE_RAW;
    raw.period_milli_hz = 0U; raw.window_span_ns = WAVEFORM_WINDOW_SPAN_NS;
    raw.window_start_ns = raw.plan_common_ns - UINT64_C(370000000);
    assert(WaveformService_OnPlan(&raw, 2800U));
    WaveformService_NotifyClockChange();
    WaveformService_Update(BOARD_ROLE_B, true, &domain, UINT64_C(12800000000), 2900U);
    WaveformSnapshot snapshot; WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.valid && snapshot.period_valid && !snapshot.synced);
    assert(SpanDiff(snapshot.window_span_ns, ExpectedPeriodSpan(plan.period_milli_hz)) <= 2000U);

    WaveformService_Init(); History(0.0); Anchors(); Progress(2800U, 0.0);
    WaveformService_Update(BOARD_ROLE_B, true, &domain, UINT64_C(12700000000), 2800U);
    assert(WaveformService_OnPlan(&plan, 2800U));
    WaveformService_Update(BOARD_ROLE_B, false, NULL, 0U, 2900U);
    RangingSessionSnapshot changed = domain; changed.domain++; changed.established = false;
    WaveformService_Update(BOARD_ROLE_B, false, &changed, 0U, 2901U);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.valid && snapshot.period_valid && !snapshot.synced);
    assert(SpanDiff(snapshot.window_span_ns, ExpectedPeriodSpan(plan.period_milli_hz)) <= 2000U);

    WaveformService_Init(); History(0.0); Anchors(); Progress(3414U, 0.0);
    /* 最早窗已被新半块覆盖，即使deadline只迟249ms，也不得标共同窗口。 */
    plan.mode = WAVEFORM_MODE_RAW; plan.period_milli_hz = 0U;
    plan.plan_common_ns = UINT64_C(13065000000);
    plan.display_deadline_ns = UINT64_C(13165000000);
    plan.window_start_ns = UINT64_C(12695000000); plan.window_span_ns = WAVEFORM_WINDOW_SPAN_NS;
    WaveformService_Update(BOARD_ROLE_B, true, &domain, UINT64_C(13414000000), 3414U);
    assert(WaveformService_OnPlan(&plan, 3414U));
    WaveformService_Update(BOARD_ROLE_B, true, &domain, UINT64_C(13414000000), 3414U);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.valid && !snapshot.synced && snapshot.source == WAVEFORM_SOURCE_LOCAL);
    assert(snapshot.reason == WAVEFORM_REASON_HISTORY);
}

/** @brief 单个理想周期窗口不得立即建立PERIOD资格，需三个分离的200ms候选。 */
static void CheckThreeConfirmations(void)
{
    WaveformPlan plan;
    WaveformService_Init();
    History(0.0);
    Anchors();
    WaveformService_Update(BOARD_ROLE_A, true, &domain, UINT64_C(12300000000), 2400U);
    assert(WaveformService_TakePlan(&plan));
    assert(plan.mode == WAVEFORM_MODE_RAW);
}

int main(void)
{
    CheckInitAndLocal();
    CheckThreeConfirmations();
    CheckRawWindow();
    CheckPeriodicPlan();
    CheckFractionalPlan();
    CheckLocalPeriodSpan();
    CheckShortestPeriod();
    CheckPeriodHold();
    CheckPhaseAndFrequencyChange();
    CheckRawFidelity();
    CheckCacheContextAndLateHistory();
    CheckScheduling();
    CheckWireAndLimits();
    puts("waveform: PASS");
    return 0;
}
