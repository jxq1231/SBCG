/**
 * @file        sound_detector_service.h
 * @brief       声明唯一PCM借用内的暂存检测与归还验证后的候选发布接口。
 * @details     工作副本、4条暂存和4条已验证队列；完整2400帧分类后才发布本地候选。
 *              提供已归还验证的扫频分类观测，供标定独立判断是否检出声音。
 * @note        串行前台；双2400帧CPU自有NOLOAD缓存仅在End成功后提交，不保存DMA指针或修改外设。
 */
#ifndef SBCG_SOUND_DETECTOR_SERVICE_H
#define SBCG_SOUND_DETECTOR_SERVICE_H
#include "Detector/sound_detector.h"
#include "BSP/bsp_audio.h"
#define SOUND_DETECTOR_QUEUE_CAPACITY 4U
#define SOUND_DETECTOR_CLASSIFY_FRAMES 2400U
#define SOUND_DETECTOR_TOA_LEFT_MARGIN 64U
#if defined(SBCG_MODE_TEST_CONTROL) && defined(SBCG_USE_ADVANCED_UI)
/** @brief 已归还的候选诊断回调；event仅本次借用，toa_first为估算窗首帧。 */
typedef void (*SoundDetectorTraceSink)(uint32_t sequence, const SoundCandidate *event,
                                       uint64_t toa_first);
/** @brief 注册前台诊断回调；NULL关闭。 @return 无返回值。 */
void SoundDetectorService_SetTraceSink(SoundDetectorTraceSink sink);
#endif
/** @brief 最近一次已发布候选的B侧早/晚TOA偏移，供上层事件发布复制。 */
typedef struct {
    uint8_t count;
    int32_t offset_q16[4];
} SoundDetectorServiceToaCandidates;
/**
 * @brief 取指定候选序号的TOA候选偏移；只读，不访问PCM。
 * @param sequence 非零候选序号。
 * @param out 非NULL输出；找到时复制，未找到count=0。
 * @return true表示命中最近发布缓存；false为参数非法或未找到。
 */
bool SoundDetectorService_GetToaCandidates(uint32_t sequence,
    SoundDetectorServiceToaCandidates *out);
extern const SoundDetectorConfig SoundDetectorService_DefaultConfig;
/** @brief 服务计数饱和递增，pending含已验证且等待完整分类窗的队列条目，armed仅是能量检测布防。
 *  max_stage_ms为Stage内8192帧批量/逐帧回退暂算循环的最大HAL毫秒（不含入口校验/工作副本拷贝）；
 *  max_process_ms为Process入口守卫后到返回的最大HAL毫秒（包含日志格式化/发送）。 */
typedef struct {
    uint32_t valid_blocks,rejected_blocks,resets,candidates,dropped,errors;
    uint32_t max_stage_ms,max_process_ms;
    /** @brief 子帧前沿过阈估算成功/失败次数；失败不改变候选内容，仅TOA_US为NA。 */
    uint32_t toa_ok,toa_miss;
    /** @brief 局部估算成功但窗口位置无法量化为Q16/绝对帧溢出的次数。 */
    uint32_t toa_invalid;
    uint8_t pending,channel;
    bool configured,armed,staging;
} SoundDetectorServiceSnapshot;
/**
 * @brief 初始化实验检测服务和限速日志。
 * @param now_ms 当前HAL毫秒，支持无符号回绕。
 * @param config 非NULL只读配置，复制使用；不需在返回后保留。
 * @param channel 选定交错PCM声道0或1。
 * @param log 可空日志回调，返回前消费或复制文本；NULL停用服务。
 * @return true表示配置合法且已启用，false表示停用或参数非法。
 * @note 不启动采集；无动态内存，重初始化清软件元数据，不读取未写满的NOLOAD样本，不触碰PPS/PTP。
 */
bool SoundDetectorService_Init(uint32_t now_ms,const SoundDetectorConfig *config,
    uint8_t channel,void (*log)(const char *,int));
/**
 * @brief 在唯一Begin成功后借用块数据，暂算整个半块候选。
 * @param now_ms 当前HAL毫秒，用于块年龄<250ms校验。
 * @param block 只读Begin描述符及借用PCM，仅本调用内访问，须在End前调用。
 * @return true表示已完成暂存，false为无效元数据/生命周期；任何结果之后均须Complete。
 * @note 不打印、不发布、不调用Begin/End；优先使用64点批量入口，未满足快速资格时按原逐帧回退；
 *       8192帧可产生多个候选，超4条暂存损失待End成功才计数；完整2400帧分类可借已提交尾部，
 *       块尾不足则等待下一个连续块，不截短分类；TOA同样用尾部+后继块拼完整256帧窗。
 */
bool SoundDetectorService_Stage(uint32_t now_ms,const BSP_AudioBlock *block);
/**
 * @brief 接收唯一BSP End返回结果，提交或丢弃暂存。
 * @param accepted 必须为刚完成BSP_Audio_EndBlock的真实结果。
 * @return true表示本块检测已验证提交，false表示拒绝并清除不能信任的历史。
 * @note 不再访问借用PCM或描述符；成功交换CPU尾部缓存并提交分类更新，失败清历史/缓存/队列，
 *       尚未验证的候选不计为有效候选。
 */
bool SoundDetectorService_Complete(bool accepted);
/** @brief 模式切换后丢弃分类历史与候选；须先归还PCM。 @return 无返回值。 */
void SoundDetectorService_DiscardPending(void);
/**
 * @brief 在归还后检查采集健康，清理过期队列并限速输出。
 * @param now_ms 当前HAL毫秒计数。
 * @param snapshot 调用方刚取得的只读BSP快照，不可NULL，仅调用期间使用。
 * @return 无返回值。
 * @note staging期间整次不输出；未完成分类的队头不越序，原完成时间满1000ms计丢弃；
 *       每250ms最多一候选、每1000ms最多一状态，SYNC固定0。
 */
void SoundDetectorService_Process(uint32_t now_ms,const BSP_AudioSnapshot *snapshot);
/**
 * @brief 复制服务状态，不授予PCM访问权。
 * @param out 调用方非NULL可写输出；NULL不操作。
 * @return 无返回值。
 * @note 仅串行前台；软件计数不是物理音频或PTP精度证据。
 */
void SoundDetectorService_GetSnapshot(SoundDetectorServiceSnapshot *out);
/**
 * @brief 复制最近一次已验证扫频的分类位置，不依赖TOA或测距结果。
 * @param session 可空会话输出；未观测到或清除模式历史后为0。
 * @param onset_frame 可空起始帧输出，单位双声道帧。
 * @return 无返回值；仅由串行前台调用。
 */
void SoundDetectorService_GetLastChirp(uint32_t *session,uint64_t *onset_frame);
#endif
