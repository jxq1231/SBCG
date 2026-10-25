/**
 * @file        sound_detector_service.h
 * @brief       声明唯一PCM借用内的暂存检测与归还验证后的候选发布接口。
 * @details     工作副本、4条暂存和4条已验证队列；只输出本地帧候选，不查询PTP事件时间。
 * @note        串行前台；调用者管理Begin/End，服务不认领DMA、不保存样本指针或修改外设。
 */
#ifndef SBCG_SOUND_DETECTOR_SERVICE_H
#define SBCG_SOUND_DETECTOR_SERVICE_H
#include "Detector/sound_detector.h"
#include "BSP/bsp_audio.h"
#define SOUND_DETECTOR_QUEUE_CAPACITY 4U
extern const SoundDetectorConfig SoundDetectorService_DefaultConfig;
/** @brief 服务计数饱和递增，pending为当前已验证队列条数，armed仅是能量检测布防。
 *  max_stage_ms为Stage内8192帧逐帧暂算循环的最大HAL毫秒（不含入口校验/工作副本拷贝）；
 *  max_process_ms为Process入口守卫后到返回的最大HAL毫秒（包含日志格式化/发送）。 */
typedef struct {
    uint32_t valid_blocks,rejected_blocks,resets,candidates,dropped,errors;
    uint32_t max_stage_ms,max_process_ms;
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
 * @note 不启动采集；无动态内存，重初始化清所有软件状态，不触碰PPS/PTP。
 */
bool SoundDetectorService_Init(uint32_t now_ms,const SoundDetectorConfig *config,
    uint8_t channel,void (*log)(const char *,int));
/**
 * @brief 在唯一Begin成功后借用块数据，暂算整个半块候选。
 * @param now_ms 当前HAL毫秒，用于块年龄<250ms校验。
 * @param block 只读Begin描述符及借用PCM，仅本调用内访问，须在End前调用。
 * @return true表示已完成暂存，false为无效元数据/生命周期；任何结果之后均须Complete。
 * @note 不打印、不发布、不调用Begin/End；8192帧可产生多个候选，超4条暂存损失待End成功才计数。
 */
bool SoundDetectorService_Stage(uint32_t now_ms,const BSP_AudioBlock *block);
/**
 * @brief 接收唯一BSP End返回结果，提交或丢弃暂存。
 * @param accepted 必须为刚完成BSP_Audio_EndBlock的真实结果。
 * @return true表示本块检测已验证提交，false表示拒绝并清除不能信任的历史。
 * @note 不再访问PCM或描述符；失败清旧确认历史及队列，尚未验证的候选不计为有效候选。
 */
bool SoundDetectorService_Complete(bool accepted);
/**
 * @brief 在归还后检查采集健康，清理过期队列并限速输出。
 * @param now_ms 当前HAL毫秒计数。
 * @param snapshot 调用方刚取得的只读BSP快照，不可NULL，仅调用期间使用。
 * @return 无返回值。
 * @note staging期间整次不输出；每250ms最多一候选、每1000ms最多一状态，SYNC固定0。
 */
void SoundDetectorService_Process(uint32_t now_ms,const BSP_AudioSnapshot *snapshot);
/**
 * @brief 复制服务状态，不授予PCM访问权。
 * @param out 调用方非NULL可写输出；NULL不操作。
 * @return 无返回值。
 * @note 仅串行前台；软件计数不是物理音频或PTP精度证据。
 */
void SoundDetectorService_GetSnapshot(SoundDetectorServiceSnapshot *out);
#endif
