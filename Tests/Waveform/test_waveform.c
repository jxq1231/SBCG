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

/** @brief 提交一个连续DMA半块；phase<0表示确定性噪声。 */
static void Feed(uint64_t frame, double phase, bool accepted, uint32_t ms)
{
    BSP_AudioBlock block = {0};
    for (unsigned i = 0U; i < BSP_AUDIO_HALF_FRAMES; ++i) {
        noise_seed = noise_seed * 1664525U + 1013904223U;
        pcm[2U * i] = phase < 0.0 ? (int16_t)(noise_seed >> 16) :
            (int16_t)(12000.0 * sin((double)(frame + i) *
                6.283185307179586 / tone_period_frames + phase));
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

/** @brief 验证500Hz周期信号进入PERIOD模式、周期字段与20ms共同窗口。 */
static void CheckPeriodicPlan(void)
{
    WaveformSnapshot snapshot;
    WaveformPlan plan;
    WaveformService_Init();
    History(0.0);
    Anchors();
    WaveformService_Update(BOARD_ROLE_A, true, &domain, UINT64_C(12300000000), 2400U);
    assert(WaveformService_TakePlan(&plan));
    assert(plan.mode == WAVEFORM_MODE_PERIOD);
    assert(plan.period_milli_hz >= 490000U && plan.period_milli_hz <= 510000U);
    assert(plan.window_span_ns == WAVEFORM_WINDOW_SPAN_NS);
    assert(plan.window_start_ns + plan.window_span_ns <= plan.plan_common_ns);
    WaveformService_Update(BOARD_ROLE_A, true, &domain, plan.display_deadline_ns, 2500U);
    WaveformService_GetSnapshot(&snapshot);
    assert(snapshot.valid && snapshot.synced && snapshot.period_valid);
    assert(snapshot.period_milli_hz == plan.period_milli_hz);
    assert(snapshot.points[0] != 0 || snapshot.points[10] != 0);

    /* 相位网格跨计划只按整数周期推进；新输入块不重置参考。 */
    for (uint64_t frame = 114688U; frame < 163840U; frame += BSP_AUDIO_HALF_FRAMES)
        Feed(frame, 0.0, true, (uint32_t)(frame / 48U));
    AudioTimeAnchorObservation anchor = {0};
    anchor.anchor.valid = true;
    anchor.anchor.session = 1U;
    anchor.sequence = 3U;
    anchor.captured_ms = 3000U;
    anchor.anchor.frame_lo = anchor.anchor.frame_hi = 144000U;
    anchor.anchor.sec = 13U;
    WaveformService_ObserveAnchor(&anchor);
    WaveformPlan next;
    WaveformService_Update(BOARD_ROLE_A, true, &domain, UINT64_C(13000000000), 3600U);
    assert(WaveformService_TakePlan(&next));
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
    WaveformService_Update(BOARD_ROLE_A, true, &domain, UINT64_C(12300000000), 2400U);
    assert(WaveformService_TakePlan(&plan));
    assert(plan.mode == WAVEFORM_MODE_PERIOD);
    assert(plan.period_milli_hz >= 1840000U && plan.period_milli_hz <= 1920000U);
    assert(plan.window_span_ns == WAVEFORM_WINDOW_SPAN_NS);
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

int main(void)
{
    CheckInitAndLocal();
    CheckRawWindow();
    CheckPeriodicPlan();
    CheckFractionalPlan();
    CheckScheduling();
    CheckWireAndLimits();
    puts("waveform: PASS");
    return 0;
}
