/**
 * @file        sound_detector.h
 * @brief       声明PCM16候选声音帧定位、质量和去重的纯C接口。
 * @details     调用方持有状态，可逐帧或按完整64点批量输入；输出仅为能量候选，不代表声学到达精度。
 * @note        无HAL/时间戳/日志依赖；不认领DMA或保留样本指针，应用接入及物理验收是后续增量。
 */
#ifndef SBCG_SOUND_DETECTOR_H
#define SBCG_SOUND_DETECTOR_H
#include <stdbool.h>
#include <stdint.h>

/** @brief 批量入口单次最多借用64个单声道PCM16点；未满足快速资格时只消费一个点。 */
#define SOUND_DETECTOR_BATCH_MAX_FRAMES 64U

/** @brief 可复制配置；阈值单位PCM16幅值，窗口/冷却单位双声道采样组中的帧数，不隐含采样率。 */
typedef struct {
    uint16_t window_frames, on_rms_pcm, off_rms_pcm;
    uint16_t on_windows, quiet_windows;
    uint32_t refractory_frames;
} SoundDetectorConfig;
/** @brief 能量候选；帧号为输入session内坐标，mean_square_ac为连续确认窗最大去直流均方值（PCM16幅值平方），peak_abs为原始幅值最大绝对值，非PTP时间。 */
typedef struct {
    uint32_t session;
    uint64_t onset_frame, confirmed_frame;
    uint32_t mean_square_ac, peak_abs;
    bool clipped;
    /** @brief 服务层波形分类副本；核心检测器不填写，初值为0=UNKNOWN。 */
    uint8_t event_class;
    uint8_t quality;
} SoundCandidate;
/** @brief 单帧处理结果；DISCONTINUITY已清历史并消费当前样本，INVALID按公开接口约定拒绝。 */
typedef enum {
    SOUND_DETECTOR_NONE=0, SOUND_DETECTOR_EVENT,
    SOUND_DETECTOR_DISCONTINUITY, SOUND_DETECTOR_INVALID
} SoundDetectorResult;
/**
 * @brief 调用方独占的流状态，可整体复制以暂存计算；内部字段不可由业务代码单独修改。
 * @note 不与IRQ共享，不包含外部指针；重置不修改已复制的配置。
 */
typedef struct {
    SoundDetectorConfig config;
    bool configured, have_input, armed, have_event;
    uint32_t session;
    uint64_t last_frame, window_first, onset_frame, last_event_frame;
    int64_t sum;
    uint64_t sum_squares;
    uint16_t used, on_count, quiet_count;
    uint32_t peak_abs, run_peak, run_mean_square;
    bool clipped, run_clipped;
} SoundDetector;
/**
 * @brief 校验并复制参数，建立须先安静的初始流状态。
 * @param detector 调用方独占的可写状态，不可NULL，失败清零。
 * @param config 只读配置，不可NULL；可指向detector.config，函数先复制再重置。
 * @return true表示参数有效；false表示空指针或范围非法。
 * @note 窗口2..256、开阈值1..32768、关阈值小于开阈值，确认/安静窗数至少1；不触及硬件。
 */
bool SoundDetector_Init(SoundDetector *detector,const SoundDetectorConfig *config);
/**
 * @brief 清除窗、候选及去重历史，保留有效配置并重新等待安静。
 * @param detector 调用方拥有的非NULL状态；零初始化或Init失败的未配置状态不改变；不可传内容未定义的自动变量。
 * @return true表示已重置，false表示NULL或未配置。
 * @note 仅串行调用；可用于显式开始新流，事件编号由未来应用层管理。
 */
bool SoundDetector_Reset(SoundDetector *detector);
/**
 * @brief 输入一帧中选定声道的PCM16样本，推进窗口和候选状态。
 * @param detector 已初始化且由调用方独占的可写状态，不可NULL。
 * @param session 非零的采集会话；变化使本次返回DISCONTINUITY并重新等待安静。
 * @param frame_index 当前双声道帧的绝对uint64索引，正常须连续+1，不是int16元素索引。
 * @param sample 选定声道的PCM16值；不保留其来源指针。
 * @param out 非NULL调用方输出；无事件/错误时清零，NULL不消费状态。
 * @return NONE无候选、EVENT输出有效、DISCONTINUITY清历史后已消费本帧、INVALID拒绝。
 * @note session0清历史；同session在UINT64_MAX之后拒绝直到Reset或新session。缺口/倒序/重复同样清历史。
 *       onset为首高能量窗起点，confirmed为末确认窗末帧；不是精定位时间。调用方负责DMA读数据最终有效性。
 */
SoundDetectorResult SoundDetector_Push(SoundDetector *detector,uint32_t session,
    uint64_t frame_index,int16_t sample,SoundCandidate *out);
/**
 * @brief 在完整连续64点窗上批量推进，否则消费一点并沿用原逐帧行为。
 * @param detector 调用方独占的状态；NULL或未配置时拒绝且不读取PCM。
 * @param session 会话编号；0保留原清历史语义，返回INVALID且不读取PCM。
 * @param first_frame 首输入点的绝对帧，非DMA元素偏移；快速路径末帧不得溢出。
 * @param samples 至少max_frames点的连续只读PCM，仅本调用借用，不保存指针；NULL拒绝。
 * @param max_frames 可借用点数1..64；越界时拒绝且不改检测状态。
 * @param consumed 非NULL独占输出，入口清0；成功为1或64，INVALID为0。
 * @param out 非NULL独占候选输出，入口清零；仅EVENT时填写。
 * @return NONE无候选、EVENT一个有效候选、DISCONTINUITY已消费一点并清旧流、INVALID拒绝。
 * @note 输出、状态、输入的存储不得重叠。新指针/长度非法优先拒绝且不改状态。
 *       完整64点、空窗口、配置窗64及首次/同会话连续输入走快速统计；其他情况只消费一点。
 *       同会话last_frame已为UINT64_MAX时在读PCM前拒绝。调用方须按consumed推进并防止坐标回绕。
 *       无HAL、IRQ、DMA或日志访问；本接口不直接接管板端采集。
 */
SoundDetectorResult SoundDetector_PushBatch(SoundDetector *detector,uint32_t session,
    uint64_t first_frame,const int16_t *samples,uint16_t max_frames,
    uint16_t *consumed,SoundCandidate *out);
#endif
