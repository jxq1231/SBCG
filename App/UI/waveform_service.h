/**
 * @file waveform_service.h
 * @brief 真实PCM历史波形、共同窗口计划及有界界面快照。
 * @details 单前台实例；A指定共同历史窗，B严格采用该窗，不独立触发相位。
 * @note Init仅在SDRAM成功准备后调用；未Init时处理入口不读写外部数组，标准音业务可继续。
 *       所有接口串行前台调用，不访问外设或保留DMA指针。
 */
#ifndef SBCG_WAVEFORM_SERVICE_H
#define SBCG_WAVEFORM_SERVICE_H
#include "BSP/bsp_audio.h"
#include "Audio/audio_time_anchor.h"
#include "Network/ranging_session.h"
#define WAVEFORM_POINT_COUNT 256U
#define WAVEFORM_PLAN_WIRE_BYTES 44U
#define WAVEFORM_RING_FRAMES 32768U

/** @brief 共同计划CPU副本；时间为PTP共同域纳秒，span为纳秒，序号按32位串行比较。 */
typedef struct {
    uint32_t domain, authority_epoch, sequence;
    uint64_t display_deadline_ns, window_start_ns;
    uint32_t window_span_ns, period_milli_hz;
} WaveformPlan;
/** @brief 界面副本；points为PCM16定幅，valid=false禁止绘制旧数据；synced仅表示共同模型窗。 */
typedef struct {
    bool valid, synced, period_valid;
    uint32_t frame_seq, plan_sequence, period_milli_hz;
    uint64_t window_start_ns;
    uint32_t window_span_ns;
    uint16_t point_count;
    int16_t points[WAVEFORM_POINT_COUNT];
} WaveformSnapshot;
/** @brief SDRAM就绪后清元数据；不读取NOLOAD数组。 @return 无返回值。 */
void WaveformService_Init(void);
/** @brief PTP跳变或连续调频动作前撤销旧斜率、锚点、计划及SYNCED资格，保留PCM供LOCAL；恢复共同显示须重新收集两个边界后的锚点。未Init不操作。 @return 无返回值。 @note 由时钟所有者在硬件动作前串行调用，隐式划分rate_revision，不改变采集或测量策略。 */
void WaveformService_NotifyClockChange(void);
/** @brief 更新前台健康及节流，驱动计划/快照。 @param role 本板角色。 @param healthy 共同时钟健康。 @param session 当前资格副本可NULL。 @param common_ns 已读取共同纳秒，无效时0。 @param now_ms HAL毫秒，可回绕。 @return 无返回值。 */
void WaveformService_Update(BoardRole role, bool healthy, const RangingSessionSnapshot *session,
                            uint64_t common_ns, uint32_t now_ms);
/** @brief 在唯一Begin/End借用内复制左声道。 @param block 非NULL只读借用块，仅调用期间访问；不自行Begin。 @return 无返回值。 */
void WaveformService_StageBlock(const BSP_AudioBlock *block);
/** @brief 原有End结束后提交或丢弃stage。 @param accepted 必须为真实End结果。 @param now_ms 当前HAL毫秒。 @return 无返回值。 */
void WaveformService_CompleteBlock(bool accepted, uint32_t now_ms);
/** @brief 扇出既有锚点副本，不消费锚点单槽。 @param observation 非NULL只读观测；失效清模型。 @return 无返回值。 */
void WaveformService_ObserveAnchor(const AudioTimeAnchorObservation *observation);
/** @brief 复制CPU快照。 @param out 可NULL，非空写完整副本。 @return 无返回值。 */
void WaveformService_GetSnapshot(WaveformSnapshot *out);
/** @brief 取一次待发A计划。 @param out 非NULL输出，无数据不修改。 @return 成功true；发送失败不重试旧计划。 */
bool WaveformService_TakePlan(WaveformPlan *out);
/** @brief B消费已通过Wire_Decode及端点验证的报文，必须先Update。 @param plan 非NULL合法CPU副本；字段结构不重复检查，仅核对当前上下文。 @param now_ms 本机毫秒；重复包不续期。 @return 当前域且新序号为true。 */
bool WaveformService_OnPlan(const WaveformPlan *plan, uint32_t now_ms);
/** @brief 编码WVP1固定44字节大端；不发送结构体。 @param plan 非NULL输入。 @param data 非NULL输出。 @param length 必须44。 @return 字段有效且编码成功为true。 */
bool WaveformPlanWire_Encode(const WaveformPlan *plan, uint8_t *data, uint16_t length);
/** @brief 校验WVP1 magic及字段并解码。 @param data 非NULL输入。 @param length 必须44。 @param out 非NULL输出，失败不改。 @return 成功true。 */
bool WaveformPlanWire_Decode(const uint8_t *data, uint16_t length, WaveformPlan *out);
#endif
