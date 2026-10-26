/**
 * @file sound_event_time.h
 * @brief 已验证候选的传输完成坐标到本地PTP模型时间查询接口。
 * @details 固定4锚点/4候选；FIRST+1为传输组完成坐标，非声学到达时刻。
 * @note 串行前台；复制元数据，不访问PCM/HAL，不配置外设，LOCAL=1/SYNC=0。
 */
#ifndef SBCG_SOUND_EVENT_TIME_H
#define SBCG_SOUND_EVENT_TIME_H
#include "Detector/sound_detector.h"
#include "Audio/audio_time_anchor.h"
#include "BSP/bsp_audio.h"
#define SOUND_TIME_CAPACITY 4U
#define SOUND_TIME_EVENT_CAPACITY 4U
#define SOUND_TOA_Q16_ONE 65536U
typedef enum { SOUND_TIME_CLOCK, SOUND_TIME_STREAM } SoundTimeReset;
/** @brief 窗口TOA状态；MISSING/INVALID不得产生模型时间，VALID表示坐标已量化。 */
typedef enum { SOUND_TOA_MISSING = 0, SOUND_TOA_VALID, SOUND_TOA_INVALID } SoundToaStatus;
/**
 * @brief 检测器暂存的窗口内TOA副本。
 * @note frame为估计前沿所在的零基绝对PCM帧floor；fraction_q16为1/65536帧的分数；
 *       本结构不含PCM指针。送到事件服务后由该服务一次性加1转换为NDTR模型进度。
 */
typedef struct
{
    SoundToaStatus status;
    uint64_t frame;
    uint16_t fraction_q16;
} SoundToaPosition;
/** @brief 成功TOA事件的标量副本，供后续网络发布；不含PCM指针。 */
typedef struct
{
    uint32_t sequence;
    uint32_t session;
    uint64_t time_ns;
    uint64_t frame;
    uint16_t fraction_q16;
    uint32_t mean_square_ac;
    uint32_t peak_abs;
    uint8_t event_class;   /**< EventClass副本；UNKNOWN/CLAP由上层决定是否发布。 */
    uint8_t quality;
} SoundEventTimeEvent;
/** @brief uint32饱和累计；pending为当前固定队列长度，无DMA所有权。 */
typedef struct {
    uint32_t queued,ok,rejected,dropped;
    uint32_t toa_queued,toa_ok,toa_rejected,toa_missing,toa_invalid;
    uint8_t pending;
} SoundEventTimeSnapshot;
/** @brief 前台初始化并清队列；log可NULL停用，回调须返回前消费/复制临时字节；无返回值，不读硬件。 */
void SoundEventTime_Init(void (*log)(const char *,int));
/**
 * @brief 复制已验证且已输出的候选到有界队列。
 * @param now_ms 归还后的当前HAL毫秒，支持无符号回绕。
 * @param sequence SOUND_CAND序号，非零严格递增且本启动内不复用。
 * @param candidate 非NULL只读候选，返回后无需保留；FIRST不可UINT64_MAX。
 * @return true为已排队；false为参数/序号/容量拒绝，计dropped。
 * @note 无PCM访问和日志；FIRST+1为模型坐标，confirmed仅用于元数据校验。
 */
bool SoundEventTime_Submit(uint32_t now_ms,uint32_t sequence,const SoundCandidate *candidate);
/**
 * @brief 复制带固定窗口TOA的已验证候选到有界队列。
 * @param now_ms 归还后的当前HAL毫秒，支持无符号回绕。
 * @param sequence SOUND_CAND序号，非零严格递增且本启动内不复用。
 * @param candidate 非NULL只读候选，返回后无需保留；FIRST不可UINT64_MAX。
 * @param position 非NULL只读TOA副本；VALID坐标溢出时降级为INVALID，MISSING/INVALID原样报告。
 * @return true为已排队；false为参数/序号/容量拒绝，计dropped。
 * @note 旧$SOUND_TIME仍按FIRST+1输出；新增$SOUND_TOA_TIME只在VALID时输出模型，
 *       缺失/非法时不回退到FIRST+1，也不访问PCM。
 */
bool SoundEventTime_SubmitToa(uint32_t now_ms,uint32_t sequence,
                              const SoundCandidate *candidate,
                              const SoundToaPosition *position);
/**
 * @brief 接受由唯一锚点消费者分发的观测副本。
 * @param now_ms 取副本后的HAL毫秒。
 * @param observation 非NULL只读副本，不保留指针；失败/过期/缺序/会话边界使旧配对失效。
 * @return 无返回值，不输出日志；时间/帧单调且年龄不超过250ms才保存。
 */
void SoundEventTime_Observe(uint32_t now_ms,const AudioTimeAnchorObservation *observation);
/**
 * @brief 在新时间域或流边界撤销旧配对。
 * @param reason CLOCK为任何校时/调频，STREAM为确认历史/块连续性失效。
 * @return 无返回值；仅将排队结果标拒绝并清锚点，不打印、不重启外设。
 */
void SoundEventTime_Invalidate(SoundTimeReset reason);
/**
 * @brief 按新鲜流快照与已有锚点查询队首；250ms至多一结果，1000ms至多一状态。
 * @param now_ms 快照之后刷新取得的HAL毫秒；候选等待2500ms超时。
 * @param snapshot 非NULL当前采集快照，仅调用期间有效，异常撤销旧模型。
 * @return 无返回值；成功区间只覆盖线性传输进度模型，失败无时间字段。
 */
void SoundEventTime_Process(uint32_t now_ms,const BSP_AudioSnapshot *snapshot);
/** @brief 复制软件状态到非NULL out；NULL不操作，无返回值，不授予PCM读取权。 */
void SoundEventTime_GetSnapshot(SoundEventTimeSnapshot *out);
/**
 * @brief 取走一个成功窗口TOA事件副本，供上层发布/配对。
 * @param event 非NULL可写输出；无事件时清零并返回false。
 * @return true表示取到成功事件；失败/旧$SOUND_TIME路径不产生事件。
 * @note 仅前台单消费者；队列固定4条，失效/初始化清空；不授予PCM读取权。
 */
bool SoundEventTime_TakeEvent(SoundEventTimeEvent *event);
#endif
