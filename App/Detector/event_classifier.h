/**
 * @file        event_classifier.h
 * @brief       标准chirp与单次击掌的波形/包络/频带代理分类（纯C）。
 * @details     从候选窗后一段PCM提取峰值、包络活动长度、前后半段过零率变化和高频能量比；
 *              chirp期望较长且过零率沿时间上升，clap期望短促/无明显上升。
 * @note        这是实验性信号分类，不是模板相关识别或物理声学精度；不改变0.80门限。
 */
#ifndef SBCG_EVENT_CLASSIFIER_H
#define SBCG_EVENT_CLASSIFIER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    EVENT_CLASS_UNKNOWN = 0,
    EVENT_CLASS_CHIRP,
    EVENT_CLASS_CLAP
} EventClass;

typedef struct
{
    bool valid;
    EventClass type;
    uint8_t quality;          /**< 0..100，仅分类质量，不是识别概率。 */
    uint16_t active_frames;   /**< 超过阈值的首末样本跨度。 */
    uint16_t zcr_early;       /**< 活动段前1/4过零率，Q10/1000。 */
    uint16_t zcr_late;        /**< 活动段后1/4过零率，Q10/1000。 */
    uint16_t high_ratio;      /**< 一阶差分能量/总能量，Q10/1000。 */
} EventClassResult;

/**
 * @brief 对候选后的PCM窗口分类；samples为选定声道连续样本。
 * @param samples 非NULL只读PCM16，不可保留。
 * @param frames 可用样本数，至少480；最多使用2400。
 * @param result 非NULL输出；失败清零。
 * @return true表示完成分类（可能UNKNOWN）；false为参数/静音/样本不足。
 */
bool EventClassifier_Classify(const int16_t *samples, uint16_t frames,
                               EventClassResult *result);

/**
 * @brief 对交织PCM中指定声道分类；samples指向该声道首样本，stride为帧间样本数。
 * @param samples 非NULL只读PCM16首样本，不保留。
 * @param stride 帧间样本数（单声道1，立体声2）。
 * @param frames 可用帧数，至少480；最多使用2400。
 * @param result 非NULL输出；失败清零。
 * @return 同EventClassifier_Classify。
 */
bool EventClassifier_ClassifyStrided(const int16_t *samples, uint16_t stride,
                                      uint16_t frames, EventClassResult *result);

#endif
