/**
 * @file        ranging_session.h
 * @brief       RSC2有界三次握手及权威代际协调接口。
 * @details     A请求重同步，B提供共同时间挑战，A分配代际，B确认后A才允许发布。
 * @note        纯C前台实例；不读写PTP、RNG或Flash。双方同时掉电会丢失代际水位，
 *              调用方必须保证旧UDP寿命有界且启动排空超过该界限；不提供永久启动身份。
 */
#ifndef SBCG_RANGING_SESSION_H
#define SBCG_RANGING_SESSION_H

#include "BSP/board_identity.h"
#include <stdbool.h>
#include <stdint.h>

#define RANGING_SESSION_WIRE_BYTES 24U
#define RANGING_SESSION_RETRY_MS 500U
#define RANGING_SESSION_CHALLENGE_MS 1500U
#define RANGING_SESSION_FUTURE_NS UINT64_C(2000000)

/** @brief SOLICIT仅A发送；CHALLENGE/CONFIRM仅B发送；OFFER仅A发送。 */
typedef enum {
    RANGING_SESSION_SOLICIT=1,
    RANGING_SESSION_CHALLENGE=2,
    RANGING_SESSION_OFFER=3,
    RANGING_SESSION_CONFIRM=4
} RangingSessionMessageType;

/** @brief CPU报文副本；challenge_ns为B创建挑战的共同时间，不是永久随机数。 */
typedef struct {
    uint8_t type;
    uint32_t domain;
    uint32_t authority_epoch;
    uint64_t challenge_ns;
} RangingSessionMessage;

/** @brief 资格副本；仅established=true允许该epoch结果，源必须不早于source_floor_ns。 */
typedef struct {
    bool established;
    uint32_t domain;
    uint32_t authority_epoch;
    uint64_t source_floor_ns;
} RangingSessionSnapshot;

/** @brief 固定大小前台状态；初始化后只通过公开函数修改，不包含网络/DMA指针。 */
typedef struct {
    BoardRole role;
    uint32_t domain, clock_epoch, retained_epoch, authority_epoch;
    uint32_t request_epoch, challenge_ms, last_tx_ms;
    uint64_t now_common_ns, last_common_ns, challenge_ns, last_challenge_ns;
    uint8_t phase;
    bool healthy, established, changed, have_clock, have_tx;
} RangingSession;

/** @brief 初始化实例。 @param self 可NULL；非NULL清全部启动状态。 @param role A/B，否则永久不放行。 @return 无返回值。 */
void RangingSession_Init(RangingSession *self,BoardRole role);

/**
 * @brief 由独立健康协调者更新共同域与本地读时资格。
 * @param self 非NULL可写实例；NULL不操作。
 * @param domain_valid 已确认共同域资格；false立即撤销。
 * @param domain 共同域TOKEN，0不合法。
 * @param local_clock_epoch 本机时钟代际，0不合法，不与对端数字比较。
 * @param common_read_ok 本次真实共同读时是否成功。
 * @param now_common_ns 本次共同时间纳秒；0、倒退或代际变化立即撤销。
 * @param now_ms 本机毫秒，用于有限握手寿命和重试回绕。
 * @return 无返回值；恢复时不恢复旧值，代际水位保留至Init。
 */
void RangingSession_Update(RangingSession *self,bool domain_valid,uint32_t domain,
                           uint32_t local_clock_epoch,bool common_read_ok,
                           uint64_t now_common_ns,uint32_t now_ms);

/** @brief 推进握手超时。 @param self 可NULL实例。 @param now_ms 当前本机毫秒。 @return 无返回值；不会读取硬件。 */
void RangingSession_Tick(RangingSession *self,uint32_t now_ms);

/**
 * @brief 取本轮待发送报文并立即记录尝试，发送失败也不取消500ms退避。
 * @param self 非NULL实例。
 * @param now_ms 当前本机毫秒，允许回绕。
 * @param out 非NULL调用方输出，失败保持不变，不保留指针。
 * @return true表示输出一次报文；不等于网络发送成功。
 */
bool RangingSession_TakeOutgoing(RangingSession *self,uint32_t now_ms,
                                 RangingSessionMessage *out);

/**
 * @brief 消费已核对固定UDP端点的RSC2副本。
 * @param self 非NULL实例，须已Update最新健康和读时。
 * @param message 非NULL只读报文，不保存指针。
 * @param now_ms 本机接收毫秒，不用于延长挑战寿命。
 * @return true为匹配当前握手或幂等重传；false不建立会话。
 */
bool RangingSession_OnMessage(RangingSession *self,const RangingSessionMessage *message,
                              uint32_t now_ms);

/** @brief 复制资格。 @param self 可NULL只读实例。 @param out 非NULL输出；self为空时清零。 @return 无返回值。 */
void RangingSession_Get(const RangingSession *self,RangingSessionSnapshot *out);

/** @brief 消费上下文改变通知。 @param self 可NULL实例。 @return true表示必须清旧展示/样本/待ACK；每次改变只取一次。 */
bool RangingSession_TakeContextChanged(RangingSession *self);

/** @brief 编码固定24字节大端RSC2。 @param message 非NULL合法字段。 @param data 至少length字节输出。 @param length 必须24。 @return 成功true；失败保持输出。 */
bool RangingSessionWire_Encode(const RangingSessionMessage *message,uint8_t *data,uint16_t length);

/** @brief 解码RSC2并核对magic/version/reserved。 @param data 非NULL输入。 @param length 必须24。 @param out 非NULL输出，失败不改。 @return 成功true。 */
bool RangingSessionWire_Decode(const uint8_t *data,uint16_t length,RangingSessionMessage *out);

#endif
