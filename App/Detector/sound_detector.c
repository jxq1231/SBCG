/**
 * @file        sound_detector.c
 * @brief       以短窗去直流能量定位候选声音帧并阻止持续声音重复触发。
 * @details     逐帧累计整数和/平方和；完整窗推进安静布防、连续高能量确认及静默/冷却再布防。
 * @note        纯C、调用方独占状态；不读取HAL时钟、不借用DMA、不输出日志，不是标准音频分类或声学精定位。
 */
#include "sound_detector.h"
#include <stddef.h>

#define DETECTOR_MIN_WINDOW_FRAMES 2U
#define DETECTOR_MAX_WINDOW_FRAMES 256U
#define DETECTOR_MAX_RMS_PCM 32768U

/**
 * @brief 清除流历史但保留复制的有效配置。
 * @param detector 调用方独占的非NULL状态，须已初始化。
 * @return 无返回值。
 * @note 用于显式Reset及输入不连续，不保存外部样本指针。
 */
static void ClearStream(SoundDetector *detector)
{
    const SoundDetectorConfig config=detector->config;
    *detector=(SoundDetector){0};
    detector->config=config;
    detector->configured=true;
}

/**
 * @brief 校验并复制配置，初始化等待安静的状态。
 * @param detector 非NULL可写输出，失败清零。
 * @param config 非NULL只读配置，允许指向detector.config；返回后无需保留。
 * @return true为合法配置，false为空指针或参数越界。
 * @note 先复制输入再清状态，避免配置与状态别名；不接入硬件。
 */
bool SoundDetector_Init(SoundDetector *detector,const SoundDetectorConfig *config)
{
    if (detector==NULL) { return false; }
    SoundDetectorConfig copy={0};
    if (config!=NULL) { copy=*config; }
    *detector=(SoundDetector){0};
    if (config==NULL || copy.window_frames<DETECTOR_MIN_WINDOW_FRAMES ||
        copy.window_frames>DETECTOR_MAX_WINDOW_FRAMES || copy.on_rms_pcm==0U ||
        copy.on_rms_pcm>DETECTOR_MAX_RMS_PCM || copy.off_rms_pcm>=copy.on_rms_pcm ||
        copy.on_windows==0U || copy.quiet_windows==0U)
    { return false; }
    detector->config=copy;
    detector->configured=true;
    return true;
}

/**
 * @brief 清除窗及事件历史并重新等待安静。
 * @param detector 已初始化且独占的可写状态，不可NULL。
 * @return true表示已清除；未配置（零初始化或Init失败）或NULL为false且不写。
 * @note 配置保持；调用方可据此显式开始新流，不影响外设或其它模块。
 */
bool SoundDetector_Reset(SoundDetector *detector)
{
    if (detector==NULL || !detector->configured) { return false; }
    ClearStream(detector);
    return true;
}

/**
 * @brief 只清除连续高能量确认过程。
 * @param detector 非NULL状态，调用方持有。
 * @return 无返回值。
 * @note 不改变布防、安静及最后事件坐标；中间能量窗也会取消连续确认。
 */
static void ClearConfirmation(SoundDetector *detector)
{
    detector->on_count=0U;
    detector->run_peak=detector->run_mean_square=0U;
    detector->run_clipped=false;
}

/**
 * @brief 完成一窗能量判断及资格/去重状态推进。
 * @param detector 非NULL独占状态，used已等于window_frames。
 * @param frame_index 本窗末帧的会话内坐标。
 * @param out 非NULL候选输出，仅确认事件时填写。
 * @return true表示本窗形成一个候选，false表示无事件。
 * @note E=N*Q-S*S，N<=256时所有中间量不超过2^46；不先除法，阈值等号按精确有理数比较。
 */
static bool CompleteWindow(SoundDetector *detector,uint64_t frame_index,SoundCandidate *out)
{
    const uint64_t count=detector->config.window_frames;
    const uint64_t denominator=count*count;
    const uint64_t energy=count*detector->sum_squares-(uint64_t)(detector->sum*detector->sum);
    const uint64_t on=detector->config.on_rms_pcm;
    const uint64_t off=detector->config.off_rms_pcm;
    if (!detector->armed)
    {
        if (energy<=off*off*denominator)
        {
            if (detector->quiet_count<detector->config.quiet_windows) { ++detector->quiet_count; }
            const bool elapsed=!detector->have_event ||
                frame_index-detector->last_event_frame>=detector->config.refractory_frames;
            if (detector->quiet_count==detector->config.quiet_windows && elapsed)
            { detector->armed=true; detector->quiet_count=0U; }
        }
        else { detector->quiet_count=0U; }
        ClearConfirmation(detector);
        return false;
    }
    if (energy<on*on*denominator) { ClearConfirmation(detector); return false; }
    if (detector->on_count==0U) { detector->onset_frame=detector->window_first; }
    ++detector->on_count;
    const uint32_t mean_square=(uint32_t)(energy/denominator);
    if (mean_square>detector->run_mean_square) { detector->run_mean_square=mean_square; }
    if (detector->peak_abs>detector->run_peak) { detector->run_peak=detector->peak_abs; }
    detector->run_clipped=detector->run_clipped || detector->clipped;
    if (detector->on_count!=detector->config.on_windows) { return false; }
    *out=(SoundCandidate){
        .session=detector->session,.onset_frame=detector->onset_frame,.confirmed_frame=frame_index,
        .mean_square_ac=detector->run_mean_square,.peak_abs=detector->run_peak,
        .clipped=detector->run_clipped};
    detector->armed=false;
    detector->have_event=true;
    detector->last_event_frame=frame_index;
    detector->quiet_count=0U;
    ClearConfirmation(detector);
    return true;
}

/**
 * @brief 消费单声道PCM16样本并维护帧连续性与完整窗。
 * @param detector 已初始化的调用方独占状态，不可NULL。
 * @param session 非零采集会话；变化重置历史。
 * @param frame_index 当前会话帧号，不是DMA元素偏移，正常要求严格加1。
 * @param sample 当前选定声道的PCM16值，按值消费。
 * @param out 非NULL候选输出，入口先清零；NULL时不改变状态。
 * @return NONE/EVENT/DISCONTINUITY/INVALID，详见公开声明。
 * @note 首帧建立窗口原点；跨调用保留未满窗。UINT64末端不加1，避免回绕复用，非法session清历史。
 */
SoundDetectorResult SoundDetector_Push(SoundDetector *detector,uint32_t session,
    uint64_t frame_index,int16_t sample,SoundCandidate *out)
{
    if (out==NULL) { return SOUND_DETECTOR_INVALID; }
    *out=(SoundCandidate){0};
    if (detector==NULL || !detector->configured) { return SOUND_DETECTOR_INVALID; }
    if (session==0U) { ClearStream(detector); return SOUND_DETECTOR_INVALID; }
    if (detector->have_input && session==detector->session && detector->last_frame==UINT64_MAX)
    { return SOUND_DETECTOR_INVALID; }
    bool discontinuity=false;
    if (detector->have_input &&
        (session!=detector->session || frame_index!=detector->last_frame+1U))
    { ClearStream(detector); discontinuity=true; }
    detector->session=session;
    detector->last_frame=frame_index;
    detector->have_input=true;
    if (detector->used==0U) { detector->window_first=frame_index; }
    const int64_t widened=sample;
    detector->sum+=widened;
    detector->sum_squares+=(uint64_t)(widened*widened);
    const uint32_t magnitude=(uint32_t)(sample<0 ? -(int32_t)sample : sample);
    if (magnitude>detector->peak_abs) { detector->peak_abs=magnitude; }
    if (sample==INT16_MIN || sample==INT16_MAX) { detector->clipped=true; }
    ++detector->used;
    bool event=false;
    if (detector->used==detector->config.window_frames)
    {
        event=CompleteWindow(detector,frame_index,out);
        detector->used=0U;
        detector->sum=0;
        detector->sum_squares=0U;
        detector->peak_abs=0U;
        detector->clipped=false;
    }
    return event ? SOUND_DETECTOR_EVENT : discontinuity ? SOUND_DETECTOR_DISCONTINUITY : SOUND_DETECTOR_NONE;
}
