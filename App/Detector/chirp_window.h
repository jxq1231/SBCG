/**
 * @file        chirp_window.h
 * @brief       声明预计算模板统计及连续PCM窗口的无状态相关评分。
 * @details     为有界候选搜索提供单窗口计算，不维护流式环形缓冲或发布事件。
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
#endif
