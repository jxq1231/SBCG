/**
 * @file        sound_detector.c
 * @brief       以短窗去直流能量定位候选声音帧并阻止持续声音重复触发。
 * @details     保留逐帧参考并增加完整64点批量统计；共用完整窗的布防、确认、静默和冷却判断。
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

/**
 * @brief 合并完整64点窗的统计和状态写回；不满足资格时仅回退一次原Push。
 * @param detector 非NULL且已配置的独占流状态；拒绝分支不读取PCM。
 * @param session 当前会话，0通过原Push保留清历史行为。
 * @param first_frame 当前首点绝对帧，快速路径要求first_frame+63可表示。
 * @param samples 调用期间借用至少max_frames个连续PCM16点，返回后不保留。
 * @param max_frames 可用点数1..64；新参数非法不改变检测状态。
 * @param consumed 非NULL输出，入口为0；接受一点或完整窗时为1或64。
 * @param out 非NULL候选输出，入口清零，仅EVENT填写。
 * @return 与原Push一致的结果，DISCONTINUITY已消费一点，INVALID不消费。
 * @note 仅调用方串行上下文；不读时钟或访问硬件。输出与状态/PCM不得重叠。
 *       64点的和绝对值<=2^21、平方和<=2^36，整数方差中间量<=2^42。
 */
SoundDetectorResult SoundDetector_PushBatch(SoundDetector *detector,uint32_t session,
    uint64_t first_frame,const int16_t *samples,uint16_t max_frames,
    uint16_t *consumed,SoundCandidate *out)
{
    if(consumed!=NULL){*consumed=0U;}
    if(out!=NULL){*out=(SoundCandidate){0};}
    if(consumed==NULL || out==NULL || samples==NULL || max_frames==0U ||
       max_frames>SOUND_DETECTOR_BATCH_MAX_FRAMES){return SOUND_DETECTOR_INVALID;}

    /* 原实现能在读样本前拒绝的分支，必须先执行，不能先求samples[0]。 */
    if(detector==NULL || !detector->configured || session==0U ||
       (detector->have_input && session==detector->session && detector->last_frame==UINT64_MAX))
    {return SoundDetector_Push(detector,session,first_frame,0,out);}

    const bool continuous=!detector->have_input ||
        (session==detector->session && detector->last_frame!=UINT64_MAX &&
         first_frame==detector->last_frame+1U);
    if(max_frames<SOUND_DETECTOR_BATCH_MAX_FRAMES || detector->used!=0U ||
       detector->config.window_frames!=SOUND_DETECTOR_BATCH_MAX_FRAMES || !continuous ||
       first_frame>UINT64_MAX-(SOUND_DETECTOR_BATCH_MAX_FRAMES-1U))
    {
        const SoundDetectorResult result=SoundDetector_Push(detector,session,first_frame,samples[0],out);
        if(result!=SOUND_DETECTOR_INVALID){*consumed=1U;}
        return result;
    }

    int32_t sum=0;uint64_t squares=0U;uint32_t peak=0U;bool clipped=false;
    for(uint16_t i=0U;i<SOUND_DETECTOR_BATCH_MAX_FRAMES;++i)
    {
        const int32_t sample=samples[i];
        sum+=sample;squares+=(uint32_t)(sample*sample);
        const uint32_t magnitude=(uint32_t)(sample<0 ? -sample:sample);
        if(magnitude>peak){peak=magnitude;}
        clipped=clipped || sample==INT16_MIN || sample==INT16_MAX;
    }
    detector->session=session;
    detector->last_frame=first_frame+SOUND_DETECTOR_BATCH_MAX_FRAMES-1U;
    detector->window_first=first_frame;detector->have_input=true;
    detector->sum=sum;detector->sum_squares=squares;detector->peak_abs=peak;detector->clipped=clipped;
    detector->used=SOUND_DETECTOR_BATCH_MAX_FRAMES;
    const bool event=CompleteWindow(detector,detector->last_frame,out);
    detector->used=0U;detector->sum=0;detector->sum_squares=0U;
    detector->peak_abs=0U;detector->clipped=false;
    *consumed=SOUND_DETECTOR_BATCH_MAX_FRAMES;
    return event ? SOUND_DETECTOR_EVENT:SOUND_DETECTOR_NONE;
}
