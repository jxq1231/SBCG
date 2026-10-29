/**
 * @file        chirp_service.h
 * @brief       声明音频归还验证后的有界扫频候选搜索与诊断接口。
 * @details     唯一认领内复制声道，End后分批评分；诊断包含完整搜索原始峰、窗口分类、丢弃原因和前台耗时。
 * @note        串行前台，无新IRQ/DMA，不调用Begin/End，不保存DMA指针。
 */
#ifndef SBCG_CHIRP_SERVICE_H
#define SBCG_CHIRP_SERVICE_H
#include "BSP/bsp_audio.h"
/** @brief 每次前台调用最多推进的扫描帧数；按64点批量推进，不代表已经测得的执行毫秒数。 */
#define CHIRP_SERVICE_SCAN_BUDGET BSP_AUDIO_HALF_FRAMES
#define CHIRP_SERVICE_SCORE_BUDGET 8U
#define CHIRP_SERVICE_HISTORY_FRAMES 3072U
#define CHIRP_SERVICE_SEARCH_BEFORE 192U
#define CHIRP_SERVICE_SEARCH_AFTER 64U
/** @brief 活动搜索的样本缓存点数：2400点模板加256点搜索跨度，仅CPU访问。 */
#define CHIRP_SERVICE_JOB_SAMPLES 2656U
/**
 * @brief 候选附近PCM捕获的定界容量与左右扩展，单位双声道帧。
 * @note  左扩展2048+模板2400+右扩展2048=6496帧；候选早于2048时左端贴流起点，
 *        跨度上限2048+6496-1=8543，因此容量取8544保证单次复制即可装入。
 */
#define CHIRP_CAPTURE_FRAMES 8544U
#define CHIRP_CAPTURE_LEFT 2048U
#define CHIRP_CAPTURE_RIGHT 2048U
/**
 * @brief 一次完整搜索任务候选附近PCM切片的零拷贝租借描述。
 * @note  samples指向服务内部定界缓冲，仅回调期间有效；frames为实际复制帧数，可能因
 *        流起点或数据未到被裁剪（left/right_clamped置位）；hint/raw_first为绝对帧坐标，
 *        tail_topup表示完成时补过右尾部（含在frames内）。
 */
typedef struct {
    const int16_t *samples;
    uint16_t frames;
    uint32_t session;
    uint32_t id;
    uint64_t first_frame, hint_frame, raw_first_frame;
    bool left_clamped, right_clamped, tail_topup;
} ChirpCaptureSlice;
/** @brief 候选PCM切片消费回调；返回前须复制/发送，函数内部不保留指针。 */
typedef void (*ChirpCaptureSink)(const ChirpCaptureSlice *slice);
/** @brief 饱和累计计数；last/max_scan和scores为每次Process帧/窗口数量，耗时单位HAL毫秒。max_complete_ms/max_drop_ms为任务从关联块completed_ms到完成/丢弃时刻（实时读取HAL tick）的饱和最大毫秒差，分开报告。 */
typedef struct {
    uint32_t valid_blocks,rejected_blocks,candidates,matches,dropped,skipped_blocks,errors,evaluations;
    uint32_t max_copy_ms,max_work_ms;
    uint32_t max_scan_ms,max_score_ms,max_process_gap_ms;
    uint32_t drop_expired,drop_history,drop_full,drop_range,drop_reset,completed_jobs;
    uint32_t max_complete_ms,max_drop_ms;
    /** @brief SEARCH报告交给日志回调的次数及待发报告因覆盖/清历史丢失的次数，分别饱和；不代表PC确认。 */
    uint32_t report_emitted,report_dropped;
    /** @brief 候选PCM切片交接给sink的次数、其中被裁剪(左或右)的次数、补过右尾部的次数及未交接(无sink/无内容)次数。 */
    uint32_t capture_ok,capture_clamped,capture_tail,capture_miss;
    uint16_t last_scanned,last_scores,max_scanned,max_scores;
    uint8_t pending;
    bool configured,staging;
} ChirpServiceSnapshot;
/**
 * @brief 初始化软件服务和冻结模板，清所有历史及计数。
 * @param now_ms 当前HAL毫秒，用于日志节流。
 * @param channel 交错PCM中选定声道0/1。
 * @param log 可空日志回调，返回前消费/复制文本；NULL使服务停用。
 * @return true为已配置；false为停用或配置错误。
 * @note 不启动采集、不改硬件；失败后Stage不访问PCM。
 */
bool ChirpService_Init(uint32_t now_ms,uint8_t channel,void (*log)(const char *,int));
/**
 * @brief 在原唯一Begin内复制声道到CPU缓冲，不做相关或发布。
 * @param now_ms 当前HAL毫秒，校验块年龄。
 * @param block 非NULL原Begin描述符，仅本调用访问PCM；之后必须Complete。
 * @return true为复制完成；false为元数据/生命周期拒绝。
 * @note 不保存samples指针，不调用Begin/End，不能把true当成数据有效。
 */
bool ChirpService_Stage(uint32_t now_ms,const BSP_AudioBlock *block);
/**
 * @brief 用真实BSP End结果提交或丢弃已复制块。
 * @param accepted 紧邻BSP_Audio_EndBlock的真实返回值。
 * @return true为验证提交，false清历史并拒绝。
 * @note 不再访问DMA/描述符；失败丢弃未发布的旧候选与搜索。
 */
bool ChirpService_Complete(bool accepted);
/**
 * @brief 在已归还数据上推进一次预算内扫描/搜索，并最多发送一行。
 * @param now_ms 最新HAL毫秒，处理流健康、候选有效期和日志节流。
 * @param snapshot 非NULL最新只读采集快照，仅本调用借用。
 * @return 无返回值。
 * @note staging不工作；DHT起始低电平期间调用方须不调度；相关与日志均在前台。
 */
void ChirpService_Process(uint32_t now_ms,const BSP_AudioSnapshot *snapshot);
/**
 * @brief 复制服务诊断状态，不授予PCM访问权。
 * @param out 非NULL调用方输出；NULL不操作。
 * @return 无返回值。
 * @note 毫秒计数粒度不能证明微秒级实时预算；没有物理识别概率。
 */
void ChirpService_GetSnapshot(ChirpServiceSnapshot *out);
/**
 * @brief 设置候选PCM切片消费回调；在下一个任务缓存时生效。
 * @param sink 可为NULL清除；回调仅前台、每次完整任务以零拷贝交接一次，返回前须消费切片。
 * @return 无返回值。
 * @note 须在ChirpService_Init之后调用；sink不得修改服务状态或重入ChirpService。
 */
void ChirpService_SetCaptureSink(ChirpCaptureSink sink);
#endif
