/**
 * @file        test_control.c
 * @brief       用虚拟时钟验证双窗口标定、候选锁定及系数保护。
 * @details     驱动真实控制器和系数模块，覆盖迟到播放、确认、超时、权限和会话变化。
 * @note        输入样本模拟测量接口已校验的结果，不读取实机。
 */
#include "Calibration/calibration_control.h"
#include <assert.h>
#include <stdio.h>

/** @brief 构造正常A板前台输入，默认未收到B的首个声音事件。 */
static CalibrationControlInput Input(void)
{
    return (CalibrationControlInput){
        .context={.authority=true,.domain_valid=true,.stream_valid=true,.clock_valid=true,
            .time_domain=7,.authority_epoch=2,.local_clock_epoch=3,.a_session=1},
        .mode_allowed=true,.now_ms=100,.common_ns=UINT64_C(10000000000),
        .audio_floor_frame=1000
    };
}

/** @brief 读取真实控制器对界面发布的只读副本。 */
static CalibrationControlSnapshot State(void)
{
    CalibrationControlSnapshot value;
    CalibrationControl_GetSnapshot(&value);
    return value;
}

/** @brief 将前台时钟推进给定毫秒，同时保留HAL计数回绕语义。 */
static void Advance(CalibrationControlInput *input, uint32_t ms)
{
    input->now_ms+=ms;
    input->common_ns+=(uint64_t)ms*1000000U;
}

/** @brief 排队并消费单条动作，返回系数版本是否改变。 */
static bool Apply(CalibrationAction action, const CalibrationControlInput *input)
{
    assert(CalibrationControl_Request(action));
    return CalibrationControl_Process(input);
}

/** @brief 构造由当前权威上下文采纳的原始温补样本。 */
static MeasurementCalibrationSample Sample(const CalibrationControlInput *input, int32_t raw_mm)
{
    return (MeasurementCalibrationSample){.raw_mm=raw_mm,.measurement_id=19,
        .state_revision=9,.time_domain=input->context.time_domain,
        .authority_clock_epoch=input->context.authority_epoch,.source_common_ns=input->common_ns,
        .first_source_common_ns=input->common_ns};
}

/** @brief 验证失败路径保持已保存系数、开关及版本。 */
static void ExpectUnchanged(const CalibrationSnapshot *before)
{
    const CalibrationSnapshot after=State().calibration;
    assert(after.calibrated==before->calibrated && after.enabled==before->enabled &&
           after.offset_mm==before->offset_mm && after.version==before->version);
}

/** @brief 从空闲和无样本开始，不允许重复启动或直接确认。 */
static void Start(const CalibrationControlInput *input)
{
    assert(!CalibrationControl_Request(CALIBRATION_ACTION_CONFIRM));
    assert(CalibrationControl_Request(CALIBRATION_ACTION_START));
    assert(!CalibrationControl_Request(CALIBRATION_ACTION_START));
    assert(!CalibrationControl_Process(input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_SOUND);
    assert(State().remaining_ms==SBCG_CALIBRATION_CAPTURE_MS);
}

/** @brief 验证排队前旧声排除、迟到采样及超过普通新鲜期后的有效确认。 */
static void TestDelayedCaptureAndConfirmation(void)
{
    Calibration_Init();CalibrationControl_Init();
    CalibrationControlInput input=Input();
    assert(CalibrationControl_Request(CALIBRATION_ACTION_START));
    Advance(&input,900); /* 计时从前台真正处理START起算。 */
    assert(!CalibrationControl_Process(&input));
    assert(State().remaining_ms==SBCG_CALIBRATION_CAPTURE_MS);
    MeasurementCalibrationSample sample=Sample(&input,490);
    input.sample=&sample;
    input.context.b_session=2;
    Advance(&input,5000);
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_SOUND); /* 边界上的旧声不接收。 */
    sample.source_common_ns+=1000000U; /* 另一块板在启动后才收到同一个声音。 */
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_SOUND && !State().heard_chirp);
    Advance(&input,5000);
    input.chirp_session=1;
    input.chirp_onset_frame=2000;
    sample=Sample(&input,1600);
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_SOUND && State().heard_chirp);
    sample=Sample(&input,501);
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_CONFIRM);
    assert(State().remaining_ms==SBCG_CALIBRATION_CONFIRM_MS);
    const uint64_t locked_source=State().candidate.source_common_ns;
    Advance(&input,4000);
    sample=Sample(&input,475);
    ++sample.measurement_id;
    assert(!CalibrationControl_Process(&input));
    assert(State().candidate.raw_mm==501 && State().candidate.source_common_ns==locked_source);
    Advance(&input,6000);
    input.sample=NULL;
    assert(Apply(CALIBRATION_ACTION_CONFIRM,&input));
    assert(State().phase==CALIBRATION_PHASE_IDLE && State().result==CALIBRATION_CONTROL_APPLIED);
    assert(State().calibration.calibrated && State().calibration.enabled);
    assert(State().calibration.offset_mm==-1 && State().applied_offset_mm==-1);
    assert(State().calibration.version==1);
}

/** @brief 验证没有标定音、已识别无合格测量、取消和两个截止的独立行为。 */
static void TestFailuresPreserveCalibration(void)
{
    Calibration_Init();CalibrationControl_Init();
    assert(Calibration_SetFromMeasurement(480));
    assert(Calibration_SetEnabled(false));
    CalibrationControlInput input=Input();
    Start(&input);
    const CalibrationSnapshot before=State().calibration;
    Advance(&input,SBCG_CALIBRATION_CAPTURE_MS);
    assert(!CalibrationControl_Process(&input));
    assert(State().result==CALIBRATION_CONTROL_NO_SOUND);
    ExpectUnchanged(&before);

    Start(&input);
    input.chirp_session=1;
    input.chirp_onset_frame=1001;
    Advance(&input,SBCG_CALIBRATION_CAPTURE_MS-1U);
    assert(!CalibrationControl_Process(&input));
    assert(State().heard_chirp && State().remaining_ms==1U);
    Advance(&input,1U);
    input.context.b_session=2;
    MeasurementCalibrationSample sample=Sample(&input,495);
    input.sample=&sample; /* 刚到截止的样本仍不得锁定。 */
    assert(!CalibrationControl_Process(&input));
    assert(State().result==CALIBRATION_CONTROL_NO_QUALIFIED_SAMPLE);
    ExpectUnchanged(&before);

    input.sample=NULL;
    Start(&input);
    assert(!Apply(CALIBRATION_ACTION_CANCEL,&input));
    assert(State().result==CALIBRATION_CONTROL_CANCELLED);
    ExpectUnchanged(&before);

    Start(&input);
    Advance(&input,5000);
    sample=Sample(&input,495);
    input.sample=&sample;
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_CONFIRM);
    assert(!Apply(CALIBRATION_ACTION_CANCEL,&input));
    assert(State().result==CALIBRATION_CONTROL_CANCELLED);
    ExpectUnchanged(&before);

    Start(&input);
    Advance(&input,5000);
    sample=Sample(&input,495);
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_CONFIRM);
    Advance(&input,SBCG_CALIBRATION_CONFIRM_MS);
    assert(!Apply(CALIBRATION_ACTION_CONFIRM,&input));
    assert(State().result==CALIBRATION_CONTROL_CONFIRM_TIMEOUT);
    ExpectUnchanged(&before);
}

/** @brief 验证两侧会话、时钟代次、模式和A写权限，以及HAL计数回绕。 */
static void TestContextAndExistingActions(void)
{
    Calibration_Init();CalibrationControl_Init();
    assert(Calibration_SetFromMeasurement(480));
    CalibrationControlInput input=Input();
    input.now_ms=UINT32_MAX-2000U;
    Start(&input);
    const CalibrationSnapshot before=State().calibration;
    Advance(&input,5000);
    input.context.b_session=2;
    MeasurementCalibrationSample sample=Sample(&input,495);
    input.sample=&sample;
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_CONFIRM);
    ++input.context.local_clock_epoch;
    assert(!Apply(CALIBRATION_ACTION_CONFIRM,&input));
    assert(State().result==CALIBRATION_CONTROL_CLOCK_LOST);
    ExpectUnchanged(&before);

    Start(&input);
    Advance(&input,5000);
    sample=Sample(&input,495);
    assert(!CalibrationControl_Process(&input));
    ++input.context.time_domain;
    input.context.b_session=0U; /* 重新握手同时清掉对端会话。 */
    assert(!Apply(CALIBRATION_ACTION_CONFIRM,&input));
    assert(State().result==CALIBRATION_CONTROL_DOMAIN_LOST);
    ExpectUnchanged(&before);

    input.context.b_session=2U;
    Start(&input);
    Advance(&input,5000);
    sample=Sample(&input,495);
    assert(!CalibrationControl_Process(&input));
    ++input.context.b_session;
    assert(!Apply(CALIBRATION_ACTION_CONFIRM,&input));
    assert(State().result==CALIBRATION_CONTROL_AUDIO_LOST);
    ExpectUnchanged(&before);
    --input.context.b_session;

    /* 确认窗内权威代次变化：候选失效，不改写系数。 */
    Start(&input);
    Advance(&input,5000);
    sample=Sample(&input,495);
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_CONFIRM);
    ++input.context.authority_epoch;
    assert(!Apply(CALIBRATION_ACTION_CONFIRM,&input));
    assert(State().result==CALIBRATION_CONTROL_DOMAIN_LOST);
    ExpectUnchanged(&before);

    /* 确认窗内本机音频会话变化：候选失效。 */
    Start(&input);
    Advance(&input,5000);
    sample=Sample(&input,495);
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_CONFIRM);
    ++input.context.a_session;
    assert(!Apply(CALIBRATION_ACTION_CONFIRM,&input));
    assert(State().result==CALIBRATION_CONTROL_AUDIO_LOST);
    ExpectUnchanged(&before);
    --input.context.a_session;

    Start(&input);
    input.mode_allowed=false;
    assert(!CalibrationControl_Process(&input));
    assert(State().result==CALIBRATION_CONTROL_MODE_REJECT);
    ExpectUnchanged(&before);

    input.mode_allowed=true;
    input.context.authority=false;
    input.applied_offset_mm=23;
    input.applied_version=7;
    for(CalibrationAction a=CALIBRATION_ACTION_START;a<=CALIBRATION_ACTION_CLEAR;a=(CalibrationAction)(a+1))
    {
        assert(!Apply(a,&input));
        assert(State().result==CALIBRATION_CONTROL_A_ONLY);
        assert(State().applied_offset_mm==23 && State().applied_version==7);
        ExpectUnchanged(&before);
    }
    input.context.authority=true;
    assert(Apply(CALIBRATION_ACTION_DISABLE,&input));
    assert(!State().calibration.enabled && State().calibration.offset_mm==20);
    assert(!Apply(CALIBRATION_ACTION_DISABLE,&input));
    assert(Apply(CALIBRATION_ACTION_ENABLE,&input));
    assert(Apply(CALIBRATION_ACTION_CLEAR,&input));
    assert(!State().calibration.calibrated && !State().calibration.enabled);
    assert(!Apply(CALIBRATION_ACTION_ENABLE,&input));
    assert(State().result==CALIBRATION_CONTROL_NO_SAVED);
}

/**
 * @brief 验证静音轮：不播放时不锁定、不应用，且采集窗内身份变化按原因退出。
 * @return 无返回值；断言失败即中止。
 * @note 静音环境只走 NO_CHIRP/取消/超时路径，系数、开关和版本必须保持原值。
 */
static void TestSilentRoundAndListeningIdentity(void)
{
    Calibration_Init();CalibrationControl_Init();
    assert(Calibration_SetFromMeasurement(480));
    CalibrationControlInput input=Input();
    assert(!CalibrationControl_Process(&input)); /* 先刷新控制器副本，再记录基线。 */
    const CalibrationSnapshot before=State().calibration;

    /* 没有本机音频会话时不得开轮，立即报告音频会话丢失。 */
    input.context.a_session=0U;
    assert(CalibrationControl_Request(CALIBRATION_ACTION_START));
    assert(!CalibrationControl_Process(&input));
    assert(State().result==CALIBRATION_CONTROL_AUDIO_LOST);
    assert(State().phase==CALIBRATION_PHASE_IDLE);
    ExpectUnchanged(&before);

    /* 静音轮：倒计时按前台处理START起算，截止报告未听到标定音。 */
    input=Input();
    assert(CalibrationControl_Request(CALIBRATION_ACTION_START));
    Advance(&input,1000U); /* 进入等待前的耗时不计入采集窗。 */
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_SOUND && !State().heard_chirp);
    assert(State().remaining_ms==SBCG_CALIBRATION_CAPTURE_MS);
    Advance(&input,1000U);
    assert(!CalibrationControl_Process(&input));
    assert(State().remaining_ms==SBCG_CALIBRATION_CAPTURE_MS-1000U);
    Advance(&input,SBCG_CALIBRATION_CAPTURE_MS-1000U);
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_IDLE);
    assert(State().result==CALIBRATION_CONTROL_NO_SOUND && !State().heard_chirp);
    ExpectUnchanged(&before);

    /* 采集窗内共同域变化：退出本轮，报告域丢失。 */
    input=Input();
    Start(&input);
    Advance(&input,3000U);
    ++input.context.time_domain;
    assert(!CalibrationControl_Process(&input));
    assert(State().result==CALIBRATION_CONTROL_DOMAIN_LOST);
    assert(State().phase==CALIBRATION_PHASE_IDLE);
    ExpectUnchanged(&before);

    /* 采集窗内权威代次变化：退出本轮。 */
    input=Input();
    Start(&input);
    Advance(&input,3000U);
    ++input.context.authority_epoch;
    assert(!CalibrationControl_Process(&input));
    assert(State().result==CALIBRATION_CONTROL_DOMAIN_LOST);
    ExpectUnchanged(&before);

    /* 采集窗内音频会话变化（例如DMA重启）：退出本轮。 */
    input=Input();
    Start(&input);
    Advance(&input,3000U);
    ++input.context.a_session;
    assert(!CalibrationControl_Process(&input));
    assert(State().result==CALIBRATION_CONTROL_AUDIO_LOST);
    ExpectUnchanged(&before);

    /* 域短暂失效只暂停捕获；以同一标识恢复继续本轮，标识变化才退出。 */
    input=Input();
    Start(&input);
    Advance(&input,3000U);
    input.context.domain_valid=false;
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_SOUND);
    input.context.domain_valid=true;
    Advance(&input,1000U);
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_SOUND);
    ++input.context.time_domain;
    assert(!CalibrationControl_Process(&input));
    assert(State().result==CALIBRATION_CONTROL_DOMAIN_LOST);
    ExpectUnchanged(&before);

    /* 启动时域尚未建立允许先等待，首个有效域锁存后再次变化才退出。 */
    input=Input();
    input.context.domain_valid=false;
    input.context.time_domain=0U;
    Start(&input);
    Advance(&input,2000U);
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_SOUND);
    input.context.domain_valid=true;
    input.context.time_domain=7U;
    Advance(&input,1000U);
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_SOUND);
    ++input.context.time_domain;
    assert(!CalibrationControl_Process(&input));
    assert(State().result==CALIBRATION_CONTROL_DOMAIN_LOST);
    ExpectUnchanged(&before);
}

/**
 * @brief 验证启动前产生的旧样本即使仍新鲜也不得成为候选。
 * @return 无返回值；断言失败即中止。
 */
static void TestPreStartSampleRejected(void)
{
    Calibration_Init();CalibrationControl_Init();
    CalibrationControlInput input=Input();
    input.context.b_session=2U;
    MeasurementCalibrationSample old=Sample(&input,495);
    assert(CalibrationControl_Request(CALIBRATION_ACTION_START));
    Advance(&input,3000U);
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_SOUND);
    input.sample=&old;
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_SOUND);
    assert(State().candidate.measurement_id==0U);
    const CalibrationSnapshot before=State().calibration;
    /* 同一配对在启动后才产生较晚到达时刻：视为本轮新样本。 */
    old.source_common_ns=input.common_ns;
    old.first_source_common_ns=input.common_ns+1U;
    assert(!CalibrationControl_Process(&input));
    assert(State().phase==CALIBRATION_PHASE_WAIT_CONFIRM);
    ExpectUnchanged(&before);
}

/** @brief 运行全部确定性状态机测试，成功返回零。 */
int main(void)
{
    TestDelayedCaptureAndConfirmation();
    TestFailuresPreserveCalibration();
    TestContextAndExistingActions();
    TestSilentRoundAndListeningIdentity();
    TestPreStartSampleRejected();
    puts("PASS calibration control: delayed capture, frozen review, deadlines, context, silent round, listening identity and preserved coefficients");
    return 0;
}
