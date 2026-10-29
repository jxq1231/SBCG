/**
 * @file        chirp_matcher.h
 * @brief       声明PCM16流式模板相关的纯C接口、事件与调用方持有的状态。
 * @details     固定上限环形缓冲跨调用保留样本，发布模板窗口帧坐标和数字匹配质量。
 * @note        不依赖HAL、DMA或时间模型；直接相关为正确性基线，尚未证明板端实时性能。
 */
#ifndef SBCG_CHIRP_MATCHER_H
#define SBCG_CHIRP_MATCHER_H
#include <stdbool.h>
#include <stdint.h>

/** @brief 模板长度上限，单位单声道样本/双声道采样帧；状态不分配堆内存。 */
#define CHIRP_MATCHER_MAX_FRAMES 2400U
/** @brief RMS单位PCM16幅值；评分0..1；观察与去重长度均为采样帧，不隐含采样率。 */
typedef struct {
    uint16_t min_rms_pcm;
    float min_correlation;
    uint16_t peak_search_frames;
    uint32_t refractory_frames;
} ChirpMatcherConfig;
/** @brief 有效匹配窗口；first/last为模板两端，confirmed为观察期结束帧；不是物理到达精度承诺。 */
typedef struct {
    uint32_t session;
    uint64_t first_frame, last_frame, confirmed_frame;
    float correlation;
    uint16_t rms_pcm;
    bool inverted, clipped;
} ChirpMatch;
/** @brief DISCONTINUITY已清历史并消费当前样本；INVALID按Push契约拒绝。 */
typedef enum {
    CHIRP_MATCH_NONE=0, CHIRP_MATCH_EVENT, CHIRP_MATCH_DISCONTINUITY, CHIRP_MATCH_INVALID
} ChirpMatcherResult;
/** @brief 调用方独占的实验配置；64 PCM、0.8、48帧观察、24000帧去重，未实测调参。 */
extern const ChirpMatcherConfig ChirpMatcher_DefaultConfig;
/**
 * @brief 调用方独占状态，内部字段不得由业务代码单独更改。
 * @note template_pcm借用外部只读存储；复制状态仍借用同一模板，但不借用输入PCM。
 */
typedef struct {
    const int16_t *template_pcm;
    ChirpMatcherConfig config;
    int16_t ring[CHIRP_MATCHER_MAX_FRAMES];
    uint16_t template_frames, used, next, clipped_samples, remaining;
    int64_t template_sum, sample_sum;
    uint64_t template_variance, sample_squares;
    double best_score;
    ChirpMatch best;
    uint64_t previous_frame, previous_event_first;
    uint32_t session;
    bool configured, have_input, pending, have_event;
} ChirpMatcher;
/**
 * @brief 校验模板、配置并清空调用方状态。
 * @param matcher 非NULL可写独占状态；失败清零。
 * @param template_pcm 非NULL只读模板，16..2400点，整个使用期有效且不与matcher存储重叠。
 * @param template_frames 模板单声道点数；常量模板方差不得为零。
 * @param config 非NULL只读配置，可为matcher.config；Init先复制再清状态。
 * @return true为配置有效；false为参数非法、非有限评分或零方差模板。
 * @note 无硬件访问；阈值评分(0,1]，RMS<=32768，观察帧数1..2400；模板不得与状态重叠。
 */
bool ChirpMatcher_Init(ChirpMatcher *matcher,const int16_t *template_pcm,
                       uint16_t template_frames,const ChirpMatcherConfig *config);
/**
 * @brief 清流窗口、待定峰与去重历史，保留已校验配置和模板借用。
 * @param matcher 非NULL且由调用方独占的已配置状态；不接受内容未定义的自动变量。
 * @return true为已清流；false为NULL或未配置，状态不变。
 * @note 不发布待定事件，须重新累计完整模板和观察尾部。
 */
bool ChirpMatcher_Reset(ChirpMatcher *matcher);
/**
 * @brief 输入一帧选定声道样本，推进相关窗口及有界峰观察。
 * @param matcher 非NULL已初始化独占状态；必须串行调用。
 * @param session 非零会话标识；0清流并拒绝当前样本。
 * @param frame_index 本会话绝对uint64帧号；连续流必须逐次+1，不得回绕。
 * @param sample PCM16单声道样本；函数复制数值，不保留输入指针。
 * @param out 非NULL独占输出，不与matcher或模板重叠；无事件时清零，NULL不消费状态。
 * @return EVENT输出有效；NONE无匹配；DISCONTINUITY清历史后消费当前帧；INVALID拒绝。
 * @note 同session在UINT64_MAX后不再消费；新session/Reset可恢复。每次相关O(模板长)，
 *       使用64位累加/double开方，不适合未经耗时验收直接放入DMA认领区或ISR。
 */
ChirpMatcherResult ChirpMatcher_Push(ChirpMatcher *matcher,uint32_t session,
                                   uint64_t frame_index,int16_t sample,ChirpMatch *out);
#endif
