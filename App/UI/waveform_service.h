/**
 * @file waveform_service.h
 * @brief 真实PCM历史波形、共同20ms窗口计划及有界界面快照。
 * @details 第一阶段固定20ms窗口、10Hz计划；A指定共同窗口，B严格采用该窗，
 *          两板分别用本地AudioTimeMap把绝对时间窗映射到自己的DMA帧。
 *          未同步或无计划时仍显示本板最近20ms原始PCM（LOCAL），无数据时置无效。
 * @note Init仅在SDRAM成功准备后调用；未Init时处理入口不读写外部数组。
 *       所有接口串行前台调用，不访问外设或保留DMA指针；不修改原始PCM。
 */
#ifndef SBCG_WAVEFORM_SERVICE_H
#define SBCG_WAVEFORM_SERVICE_H
#include "BSP/bsp_audio.h"
#include "Audio/audio_time_anchor.h"
#include "Network/ranging_session.h"
/** @brief 第一阶段20ms窗口在48kHz下的名义采样点数；实际点数按AudioTimeMap求取。 */
#define WAVEFORM_POINT_COUNT 960U
/** @brief WVP2逐字段大端序列化长度；禁止直接发送C结构体。 */
#define WAVEFORM_PLAN_WIRE_BYTES 56U
/** @brief CPU历史环长度；20ms窗口、350ms历史延迟和B本地回退均在此环内。 */
#define WAVEFORM_RING_FRAMES 32768U
/** @brief 第一阶段固定显示窗口；接口保留为参数，后续环境模式可用100ms。 */
#define WAVEFORM_WINDOW_SPAN_NS UINT64_C(20000000)
/** @brief 计划生成周期；LCD刷新不得改变音频采样节拍。 */
#define WAVEFORM_REFRESH_MS 100U
/** @brief 计划模式：RAW为固定共同窗口，PERIOD为A一次性相位参考网格。 */
#define WAVEFORM_MODE_RAW 0U
#define WAVEFORM_MODE_PERIOD 1U
/** @brief 快照来源：无数据、共同计划、本地回退。 */
#define WAVEFORM_SOURCE_NONE 0U
#define WAVEFORM_SOURCE_COMMON 1U
#define WAVEFORM_SOURCE_LOCAL 2U

/**
 * @brief 共同计划CPU副本；时间均为PTP共同域纳秒，序号按32位回绕比较。
 * @note WVP2只逐字段发送domain/epoch/sequence/mode/plan_common/window/deadline，
 *       B不使用A的DMA帧编号，也不携带PCM。
 */
typedef struct {
    uint32_t domain;
    uint32_t authority_epoch;
    uint32_t sequence;
    uint8_t mode;
    uint8_t flags;
    uint16_t reserved;
    uint64_t plan_common_ns;
    uint64_t window_start_ns;
    uint64_t window_span_ns;
    uint32_t period_milli_hz;
    uint64_t display_deadline_ns;
} WaveformPlan;

/**
 * @brief 界面副本；points为原始PCM16，不做滤波/平滑/插值/相位补偿。
 * @note valid=false时禁止绘制旧数据；source区分共同窗口与本地回退；
 *       point_count为本次实际帧数（约959/960，随本板帧率变化）。
 */
typedef struct {
    bool valid;
    bool synced;
    bool period_valid;
    uint8_t source;
    uint8_t render_state;
    uint16_t point_count;
    uint32_t frame_seq;
    uint32_t plan_sequence;
    uint32_t period_milli_hz;
    uint64_t window_start_ns;
    uint64_t window_span_ns;
    int16_t points[WAVEFORM_POINT_COUNT];
} WaveformSnapshot;

/** @brief SDRAM就绪后清元数据；不读取NOLOAD数组。 @return 无返回值。 */
void WaveformService_Init(void);
/** @brief PTP跳变或连续调频动作前撤销旧计划与SYNCED资格，保留PCM供LOCAL。 @return 无返回值。 */
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
/** @brief B消费已通过Wire_Decode及端点验证的报文，必须先Update。 @param plan 非NULL合法CPU副本。 @param now_ms 本机毫秒；重复包不续期。 @return 当前域且新序号为true。 */
bool WaveformService_OnPlan(const WaveformPlan *plan, uint32_t now_ms);
/** @brief 编码WVP2固定56字节大端，不发送结构体。 @param plan 非NULL输入。 @param data 非NULL输出。 @param length 必须56。 @return 字段有效且编码成功为true。 */
bool WaveformPlanWire_Encode(const WaveformPlan *plan, uint8_t *data, uint16_t length);
/** @brief 校验WVP2 magic及字段并解码。 @param data 非NULL输入。 @param length 必须56。 @param out 非NULL输出，失败不改。 @return 成功true。 */
bool WaveformPlanWire_Decode(const uint8_t *data, uint16_t length, WaveformPlan *out);
#endif
