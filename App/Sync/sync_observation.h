/**
 * @file        sync_observation.h
 * @brief       排队并限速计算已配对四时刻交换的观测结果。
 * @details     对外定义本模块的接口与回调约定。复制上游提供的序号、UID、会话和四时刻到4项CPU队列；每1000毫秒至多处理一项，年龄达到2000毫秒按状态6过期。
 * @note        NO_SYS前台串行使用；PC时刻为软件时间、板端为硬件时间，日志SYNC=0，不表示同步锁定。
 */

#ifndef SBCG_SYNC_OBSERVATION_H
#define SBCG_SYNC_OBSERVATION_H

#include "sync_estimator.h"
#include <stdbool.h>
#include <stdint.h>

#define SYNC_OBSERVATION_CAPACITY 4U /**< CPU观测队列的记录容量。 */
#define SYNC_OBSERVATION_TTL_MS 2000U /**< 从本地请求创建起算的有效期，单位毫秒。 */
#define SYNC_OBSERVATION_WINDOW_NS UINT64_C(800000000) /**< 每个时钟域内交换历时上限，单位纳秒。 */

/** @brief 从UDP时间交换复制得到的观测输入；不持有外部缓冲或DMA所有权。 */
typedef struct
{
    uint32_t sequence; /**< 请求序号，供日志及上游配对识别。 */
    uint32_t client_tick_ms; /**< PC请求携带的毫秒计数低32位。 */
    uint32_t uid_words[3]; /**< 板端UID三个32位字，已从线上的大端序解码。 */
    uint32_t session_hi; /**< 64位会话标识的高32位。 */
    uint32_t session_lo; /**< 64位会话标识的低32位。 */
    uint32_t created_ms; /**< 本地请求记录创建的板端毫秒计数，用于TTL。 */
    SyncExchange exchange; /**< PC软件t1/t4与板端硬件t2/t3，单位纳秒。 */
} SyncObservationInput;

/**
 * @brief   向应用提交一行观测日志的回调类型。
 * @param   line 只读日志字符缓冲，不可为NULL；仅本次调用有效，需同步消费或复制。
 * @param   length 有效字符数，单位字节，不包含结尾零字符。
 * @return  无返回值。
 * @note    模块保存函数指针供前台使用；不得保留调用方局部缓冲指针。
 */
typedef void (*SyncObservation_Log)(const char *line, int length);

/* All calls belong to the serialized main-loop context, never an ISR.
   Init clears the queue, counters and cooldowns; NULL log disables admission. */
/**
 * @brief   清空观测队列、计数与节流状态并保存日志接口。
 * @param   now_ms 初始化毫秒计数；首项结果允许立即处理。
 * @param   log 日志回调，可为NULL以停用入队和处理；后续使用期间须有效。
 * @return  无返回值。
 * @note    NO_SYS串行前台调用；不改变MAC PTP或系统时钟。
 */
void SyncObservation_Init(uint32_t now_ms, SyncObservation_Log log);
/* Copies the complete input without calculation, formatting or logging.
   False means NULL input, no initialized logger, or full queue. Only full
   queue rejection increments QUEUE_FULL. Upstream validates exchange keys. */
/**
 * @brief   将上游已配对的完整交换复制到CPU观测队列。
 * @param   input 只读完整交换，可为NULL；成功复制内容，不保存指针或任何pbuf引用。
 * @return  true表示复制入队；false表示未初始化、无日志、输入为空或队列已满。
 * @note    在串行前台/RAW回调调用；不计算或输出日志，只有满队列拒绝会增加QUEUE_FULL。
 */
bool SyncObservation_Submit(const SyncObservationInput *input);
/* At most one dequeue per call and one result per 1000 ms. Age >= TTL is
   STATUS=6 (queue expired), not an estimator result. First result may be
   immediate. Stats have an independent 1000 ms period. No clock correction. */
/**
 * @brief   限速取出一项交换，检查年龄后计算并输出结果与统计。
 * @param   now_ms 前台当前毫秒计数，按uint32_t差值处理节流及年龄回绕。
 * @return  无返回值。
 * @note    至多每1000毫秒出队一项；年龄达到TTL时STATUS=6且不调用估计器，统计有独立一秒节流。
 */
void SyncObservation_Process(uint32_t now_ms);

/**
 * @brief   清除尚未计算的旧时间域配对，保留日志与累计统计。
 * @return  无返回值。
 * @note    仅串行前台，校时或频率变化边界调用；不重启外设，不重置输出序号/累计计数。
 */
void SyncObservation_NotifyClockChange(void);

#endif
