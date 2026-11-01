/**
 * @file        clap_ranging.h
 * @brief       独立单次击掌测距、源期限及A到B可靠结果交换接口。
 * @details     A使用同域CLAP配对、当前温度与启用标定生成结果；B仅接收A的结果。
 *              共用调用方已建立的RSC2资格，不进入标准音tracker或序列门控。
 * @note        全部接口由前台串行调用；不访问硬件、Flash、网络或DMA，不保存传入指针。
 */
#ifndef SBCG_CLAP_RANGING_H
#define SBCG_CLAP_RANGING_H

#include "Network/ranging_session.h"
#include "Measure/sound_pair.h"
#include "Temperature/temperature_service.h"
#include "Calibration/calibration.h"

#define CLAP_RANGING_FRESH_NS UINT64_C(2500000000)
#define CLAP_RANGING_DISPLAY_NS UINT64_C(6500000000)
#define CLAP_RANGING_RETRY_MS 500U
#define CLAP_RANGING_MAX_DISTANCE_MM 10000U

/** @brief 按值保存的A权威结果；source为两端源时刻较大值，delta为tB-tA纳秒。 */
typedef struct {
    uint32_t domain, authority_epoch, id;
    uint32_t a_session, a_sequence, b_session, b_sequence;
    uint64_t source_common_ns;
    int64_t delta_ns;
    int32_t distance_mm;
    uint32_t sound_speed_mm_s, temperature_version;
    int32_t temperature_milli_c;
    uint16_t calibration_version;
    int16_t calibration_offset_mm; /**< 本次实际应用偏移；停用时为0。 */
    uint8_t direction, quality;
} ClapResultMessage;

/** @brief 精确ACK键；source_common_ns是本结果源键，不是ACK接收时刻。 */
typedef struct {
    uint32_t domain, authority_epoch, id;
    uint64_t source_common_ns;
} ClapAckMessage;

/** @brief 展示副本；has_value=false时不得使用result中的旧数字。 */
typedef struct {
    bool has_value, fresh;
    uint32_t source_age_ms;
    ClapResultMessage result;
} ClapRangingSnapshot;

/** @brief 调用方持有的前台实例；只能通过公开函数更新。 */
typedef struct {
    BoardRole role;
    bool healthy, have_clock, pending_tx, have_tx, pending_ack, have_source;
    uint64_t now_common_ns, last_source_ns;
    uint32_t next_id, last_id, last_tx_ms;
    RangingSessionSnapshot session;
    TemperatureSnapshot temperature;
    CalibrationSnapshot calibration;
    ClapRangingSnapshot snapshot;
    ClapAckMessage ack;
} ClapRanging;

/** @brief 清空实例及启动水位。 @param self 非NULL调用方存储。 @return 无返回值；仅启动时调用。 */
void ClapRanging_Init(ClapRanging *self);

/**
 * @brief 更新角色、当前RSC2资格、真实读时以及A当前补偿参数，推进源期限。
 * @param self 非NULL可写实例。
 * @param role 当前板身份，A计算、B接收，未知身份撤值。
 * @param healthy 调用方已确认共同域/音频流/读时健康；false立即撤值并清待发送。
 * @param session 当前RSC2快照，可NULL表示无资格；仅established且非零域/代际有效。
 * @param now_common_ns 实际读到的共同时间纳秒；0或倒退立即撤值，不能用源时刻代替。
 * @param now_ms 本机毫秒，供调用节奏保持一致，不决定源有效期。
 * @param temperature A当前温度副本；A无有效温度撤值，B可NULL且不本地重算。
 * @param calibration A当前标定副本；NULL表示未标定，B忽略。
 * @return 无返回值；A补偿参数改变撤旧值，等待下次真实击掌，不延长旧源寿命。
 */
void ClapRanging_Update(ClapRanging *self, BoardRole role, bool healthy,
                        const RangingSessionSnapshot *session,
                        uint64_t now_common_ns, uint32_t now_ms,
                        const TemperatureSnapshot *temperature,
                        const CalibrationSnapshot *calibration);

/** @brief A发布一条真实CLAP结果。 @param self 已Update实例。 @param pair 非NULL同域配对。 @return 成功true；重复/旧源/非CLAP/失域拒绝，不刷新寿命。 */
bool ClapRanging_PublishPair(ClapRanging *self, const SoundPairResult *pair);

/** @brief B消费已解码且固定端点已核对的CLP1。 @param self 已Update实例。 @param message 非NULL合法wire结果。 @return 新值或精确重复true；重复仅排队ACK，不刷新源寿命。 */
bool ClapRanging_OnMessage(ClapRanging *self, const ClapResultMessage *message);

/** @brief A消费CLA1。 @param self 已Update实例。 @param ack 非NULL精确结果键。 @return 完整键匹配并停止重试时true。 */
bool ClapRanging_OnAck(ClapRanging *self, const ClapAckMessage *ack);

/** @brief 取A待发结果并记录尝试。 @param self 已Update实例。 @param now_ms 本机毫秒，支持回绕。 @param out 非NULL输出，失败不改。 @return true需尝试一次发送；失败也须等待500ms。 */
bool ClapRanging_TakeTx(ClapRanging *self, uint32_t now_ms, ClapResultMessage *out);

/** @brief 取B待发ACK并消费。 @param self 已Update实例。 @param out 非NULL输出。 @return true有ACK；发送失败由下次结果重传再次触发。 */
bool ClapRanging_TakeAck(ClapRanging *self, ClapAckMessage *out);

/** @brief 复制展示状态。 @param self 非NULL只读实例。 @param out 非NULL调用方存储。 @return 无返回值；调用方须先Update真实共同时间。 */
void ClapRanging_GetSnapshot(const ClapRanging *self, ClapRangingSnapshot *out);

#endif
