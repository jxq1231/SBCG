/**
 * @file        chirp_window.h
 * @brief       声明预计算模板统计、无状态相关评分及单窗口失败诊断。
 * @details     为有界搜索提供匹配输出和独立门限前相关度，不维护流式历史或发布事件。
 * @note        调用方保证输入已复制且经归还验证；不访问DMA、IRQ、日志或PTP。
 */
#ifndef SBCG_CHIRP_WINDOW_H
#define SBCG_CHIRP_WINDOW_H
#include "chirp_matcher.h"
/** @brief 借用整个使用期保持只读的模板，统计单位按PCM16，frames为单声道点数。 */
typedef struct {
    const int16_t *samples;
    int64_t sum;
    uint64_t variance;
    uint16_t frames;
} ChirpWindowTemplate;
/** @brief 单窗口拒绝分类；零方差相关不可定义，LOW_RMS仍有有效原始相关度。 */
typedef enum {
    CHIRP_WINDOW_INVALID=0,
    CHIRP_WINDOW_ZERO_VARIANCE,
    CHIRP_WINDOW_LOW_RMS,
    CHIRP_WINDOW_LOW_CORR,
    CHIRP_WINDOW_MATCH
} ChirpWindowStatus;
/**
 * @brief 调用方独占诊断输出；不借用PCM，数值可复制到任务摘要。
 * @note correlation为门限前绝对归一相关度0..1，仅correlation_valid为true时有定义；
 *       正方差时rms_pcm为去直流RMS的整数截断值，inverted标识去直流相关分子为负；
 *       clipped记录PCM16端点样本。非法参数全部清零并标记INVALID；零方差仅保留clipped。
 */
typedef struct {
    ChirpWindowStatus status;
    double correlation;
    bool correlation_valid,inverted,clipped;
    uint16_t rms_pcm;
} ChirpWindowDiagnostic;
/**
 * @brief 校验模板并计算去直流统计。
 * @param out 非NULL调用方独占输出；失败清零，不得与samples重叠。
 * @param samples 非NULL只读模板，整个使用期有效且不变。
 * @param frames 点数16..2400，方差不得为零。
 * @return true为有效模板；false为参数或方差非法。
 * @note 无硬件/堆分配；不复制模板。
 */
bool ChirpWindow_Init(ChirpWindowTemplate *out,const int16_t *samples,uint16_t frames);
/**
 * @brief 计算一个已完整取得的PCM窗口，符合门限时输出匹配窗口元数据。
 * @param reference 非NULL已初始化的只读模板统计，不得由业务代码改写。
 * @param samples 非NULL连续只读PCM，至少reference.frames点，仅本次调用借用。
 * @param session 非零采集会话。
 * @param first_frame 窗口绝对起点，不得使末帧溢出uint64。
 * @param config 非NULL有效评分配置；本函数只使用RMS和相关门限。
 * @param out 非NULL不重叠输出，无匹配/失败清零；confirmed_frame固定0，调用方另行确认搜索完成。
 * @return true为符合门限；false为无匹配或非法参数，不发布事件。
 * @note 最多2400次点积项，整型64位统计、double开方；调用方负责次数预算。
 */
bool ChirpWindow_Score(const ChirpWindowTemplate *reference,const int16_t *samples,
    uint32_t session,uint64_t first_frame,const ChirpMatcherConfig *config,ChirpMatch *out);
/**
 * @brief 计算单窗匹配并可选保留未通过门限的诊断，返回规则与Score一致。
 * @param reference 非NULL已初始化只读模板统计，整个使用期借用其模板。
 * @param samples 非NULL连续只读PCM，至少reference.frames点，仅本次调用借用。
 * @param session 非零采集会话，不修改外部流状态。
 * @param first_frame 窗口绝对起点，允许0，末帧不得溢出uint64。
 * @param config 非NULL有效门限，只使用RMS及相关度字段。
 * @param out 非NULL独占匹配输出，无匹配或非法时清零，confirmed_frame固定0。
 * @param diag 可为NULL的独占诊断输出；非法参数时独立清零并标记INVALID。
 * @return true为同时通过原RMS与相关门限；false不产生匹配输出，diag仍可解释失败。
 * @note 两输出均不得与彼此、输入PCM、模板或配置重叠；调用方负责所有权及评分次数预算。
 *       单趟累计sum/squares/dot；diag非NULL时正方差低RMS窗也计算原始相关与RMS，
 *       diag为NULL的低RMS路径不执行开方；所有运算仅在前台已验证CPU副本上进行。
 */
bool ChirpWindow_ScoreWithDiagnostic(const ChirpWindowTemplate *reference,const int16_t *samples,
    uint32_t session,uint64_t first_frame,const ChirpMatcherConfig *config,ChirpMatch *out,
    ChirpWindowDiagnostic *diag);
#endif
