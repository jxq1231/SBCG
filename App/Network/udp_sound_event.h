/**
 * @file        udp_sound_event.h
 * @brief       两板声音事件UDP发布、接收与配对结果输出（仅NO_SYS前台）。
 * @details     按版本解码声音事件，校验来源、事件侧和当前有效域；撤域或换域清旧队列。
 *              可选模式观测使用同一端口收发固定对端MDP1控制消息。
 * @note        事件时间必须是本地PTP模型时间；共同时间域由上层同步状态提供，调用方不得伪造。
 */
#ifndef SBCG_UDP_SOUND_EVENT_H
#define SBCG_UDP_SOUND_EVENT_H

#include "Measure/sound_pair.h"
#include "Measure/measurement_result.h"
#include "Detector/chirp_service.h"
#include "Network/domain_wire.h"
#include "Network/measurement_ack.h"
#include "Network/temperature_wire.h"
#include "Network/ranging_status_wire.h"
#include "Network/ranging_session.h"
#include "BSP/board_identity.h"
#ifdef SBCG_USE_MODE_OBSERVATION
#include "Network/mode_wire.h"
#endif
#include <stdbool.h>
#include <stdint.h>

typedef void (*UDPSoundEvent_Log)(const char *line, int length);
#ifdef SBCG_USE_ADVANCED_UI
/** @brief 固定对端辅助短包回调；仅CLP1/CLA1/WVP2，data只在本次回调有效。 */
typedef void (*UDPSoundEvent_AuxiliarySink)(const uint8_t *data,uint16_t length);
/** @brief 安装辅助短包消费者。@param sink 可NULL停用消费。@return 无返回值。 */
void UDPSoundEvent_SetAuxiliarySink(UDPSoundEvent_AuxiliarySink sink);
/** @brief 发送已编码辅助短包。@param data 本次借用字节。@param length 编码器给定长度。@return 已发送true，否则false。 */
bool UDPSoundEvent_SendAuxiliary(const uint8_t *data,uint16_t length);
#endif
/**
 * @brief   保存一次捕获切片中用于跨板互相关的波形储备。
 * @param   slice 非NULL只读捕获切片，仅本调用借用；模块内部复制固定长度PCM。
 * @return  无返回值；未配置或切片越界时跳过，事件发布自动回退候选齿选择。
 * @note    由应用在ChirpService捕获回调中调用；每事件覆盖上一份储备，不保存指针。
 */
void UDPSoundEvent_OnCapture(const ChirpCaptureSlice *slice);
/** @brief 配对结果回调；仅前台Process调用，不保留指针。 */
typedef void (*UDPSoundEvent_ResultSink)(const SoundPairResult *result);
/** @brief 安装配对结果消费者；NULL仅恢复日志输出。 */
void UDPSoundEvent_SetResultSink(UDPSoundEvent_ResultSink sink);
/** @brief 对端有效事件回调；用于B采用A域令牌，不替代配对。 */
typedef void (*UDPSoundEvent_EventSink)(const SoundEventRecord *event);
/** @brief 安装对端事件消费者；NULL不回调。 */
void UDPSoundEvent_SetEventSink(UDPSoundEvent_EventSink sink);
/** @brief 共同域REQ/ACK消费者；NULL不回调。 */
typedef void (*UDPSoundEvent_DomainSink)(const DomainMessage *message);
/** @brief 安装共同域报文消费者。 */
void UDPSoundEvent_SetDomainSink(UDPSoundEvent_DomainSink sink);
/** @brief MACK确认消费者。 */
typedef void (*UDPSoundEvent_AckSink)(const MeasurementAck *ack);
/** @brief 安装MACK消费者。 */
void UDPSoundEvent_SetAckSink(UDPSoundEvent_AckSink sink);
/** @brief B向A发送MACK；非B/未配置/链路不可用返回false。 */
bool UDPSoundEvent_SendMeasurementAck(uint32_t now_ms,uint32_t measurement_id,
                                      uint32_t time_domain);
/** @brief A发送DOMAIN_REQ；非A/未配置/链路不可用返回false。 */
bool UDPSoundEvent_SendDomainRequest(uint32_t now_ms,uint32_t token);
/** @brief B发送DOMAIN_ACK；非B/未配置/链路不可用返回false。 */
bool UDPSoundEvent_SendDomainAck(uint32_t now_ms,uint32_t token);
/** @brief B板接收A板权威结果的回调；NULL仅丢弃。 */
typedef void (*UDPSoundEvent_MeasurementSink)(const MeasurementResultSnapshot *result);
/** @brief 安装权威结果消费者；A板发送、B板接收。 */
void UDPSoundEvent_SetMeasurementSink(UDPSoundEvent_MeasurementSink sink);
/** @brief A板向B板发送权威结果；仅A角色/已绑定/有效结果。 */
bool UDPSoundEvent_SendResult(uint32_t now_ms,const MeasurementResultSnapshot *result);
/** @brief B板接收共享温度的消费者；消息仅本次回调有效。 */
typedef void (*UDPSoundEvent_TemperatureSink)(const TemperatureWireMessage *message);
/** @brief 安装共享温度消费者；NULL丢弃。 */
void UDPSoundEvent_SetTemperatureSink(UDPSoundEvent_TemperatureSink sink);
/** @brief A板发送当前有效温度；仅A角色/已绑定/链路可用/字段有效。 */
bool UDPSoundEvent_SendTemperature(uint32_t now_ms,const TemperatureSnapshot *temperature);

/** @brief B板接收A的RST2状态；只在前台回调，不保留输入指针。 */
typedef void (*UDPSoundEvent_RangingStatusSink)(const RangingStatusMessage *message);
/** @brief 安装RST2消费者；A角色安装后也不接收。 */
void UDPSoundEvent_SetRangingStatusSink(UDPSoundEvent_RangingStatusSink sink);
/** @brief A板接收B的RAK2确认。 */
typedef void (*UDPSoundEvent_RangingAckSink)(const RangingStatusAck *ack);
/** @brief 安装RAK2消费者。 */
void UDPSoundEvent_SetRangingAckSink(UDPSoundEvent_RangingAckSink sink);
/** @brief A发送RST2；仅A/已绑定/链路可用。 */
bool UDPSoundEvent_SendRangingStatus(uint32_t now_ms,const RangingStatusMessage *message);
/** @brief B发送RAK2；仅B/已绑定/链路可用。 */
bool UDPSoundEvent_SendRangingAck(uint32_t now_ms,const RangingStatusAck *ack);

/** @brief 安装RSC2会话消费者；只在固定对端的NO_SYS前台调用，输入仅本次有效。 */
typedef void (*UDPSoundEvent_RangingSessionSink)(const RangingSessionMessage *message);
/** @brief 设置会话消费者；NULL丢弃RSC2，不改变PTP或共同域。 */
void UDPSoundEvent_SetRangingSessionSink(UDPSoundEvent_RangingSessionSink sink);
/** @brief 通过原5009端口发送RSC2；now_ms只作接口一致性，失败返回false。 */
bool UDPSoundEvent_SendRangingSession(uint32_t now_ms,const RangingSessionMessage *message);

#ifdef SBCG_USE_MODE_OBSERVATION
/** @brief 固定对端MDP1消费者；只在前台接收回调内借用消息。 */
typedef void (*UDPSoundEvent_ModeControlSink)(const ModeControlMessage *message);

/**
 * @brief MDB1业务闸门上下文；由应用在前台查询模式状态后填写。
 * @note configured=false表示未登记启动代次，保持阶段一原始未封装收发作为兼容路径；
 *       configured=true时受保护报文必须封装且世代匹配，禁止原始旧包写入业务状态。
 */
typedef struct
{
    bool configured;
    bool allowed;        /**< RANGE/CALIBRATION：CHIRP/WFR1/RST2/RAK2准入。 */
    bool clap_allowed;   /**< CLAP：CLAP事件与CLP1/CLA1准入。 */
    bool wave_allowed;   /**< WAVE：WVP2计划准入。 */
    BoardRole role;
    uint64_t a_boot;
    uint64_t b_boot;
    uint64_t mode_seq;
} UDPSoundEvent_BusinessContext;

/**
 * @brief 业务闸门上下文提供者；前台网络回调与发送路径同步调用。
 * @param out 非NULL输出，仅本次调用有效；返回false表示业务闸门未启用。
 * @return 已登记启动代次并填好上下文为true。
 */
typedef bool (*UDPSoundEvent_BusinessContextProvider)(UDPSoundEvent_BusinessContext *out);

/** @brief 安装业务闸门上下文提供者；NULL恢复阶段一未封装兼容路径。 @param provider 可NULL。 @return 无返回值。 */
void UDPSoundEvent_SetBusinessContextProvider(UDPSoundEvent_BusinessContextProvider provider);

/** @brief MDB1业务通道计数，用于周期性诊断输出。 */
typedef struct
{
    uint32_t tx_ok;
    uint32_t tx_blocked;
    uint32_t tx_err;
    uint32_t rx_ok;
    uint32_t rx_blocked;
    uint32_t rx_unwrapped;
    uint32_t rx_bad;
} UDPSoundEvent_BusinessCounters;

/** @brief 复制MDB1业务计数；NULL参数不操作。 @param out 调用方输出。 @return 无返回值。 */
void UDPSoundEvent_GetBusinessCounters(UDPSoundEvent_BusinessCounters *out);

/**
 * @brief 发送受保护的CLAP短报文（CLP1/CLA1）；未登记代次时按原字节未封装发送。
 * @param inner 非NULL已编码CLP1/CLA1内层字节，仅本次借用。
 * @param length 内层字节数（CLP1为80，CLA1为24）。
 * @return 已提交UDP为true；模式闸门拒绝、编码/网络失败为false。
 * @note 仅原5009端口；登记代次后CLAP模式才允许封装，其他模式计入业务阻断。
 */
bool UDPSoundEvent_SendProtectedShort(const uint8_t *inner, uint16_t length);

/**
 * @brief 发送受保护的WAVE计划（WVP2内层56字节，封装后88字节）。
 * @param inner 非NULL已编码WVP2内层字节，仅本次借用。
 * @param length 内层字节数，固定56。
 * @return 已提交UDP为true；模式闸门拒绝、编码/网络失败为false。
 * @note 仅A在WAVE模式发送；登记代次后原始WVP2不再被B接受。
 */
bool UDPSoundEvent_SendProtectedWave(const uint8_t *inner, uint16_t length);

/**
 * @brief 设置MDP1前台接收消费者。
 * @param sink 可为NULL，NULL表示不交付，非NULL须在注册期间保持有效。
 * @return 无返回值。
 */
void UDPSoundEvent_SetModeControlSink(UDPSoundEvent_ModeControlSink sink);

/**
 * @brief 通过原5009端口向固定对端发送本板的MDP1消息。
 * @param message 非NULL只读消息，仅本次借用，发送角色须与本板一致。
 * @return UDP发送成功为true，字段或网络条件不满足以及发送失败为false。
 */
bool UDPSoundEvent_SendModeControl(const ModeControlMessage *message);
#endif

/** @brief 初始化本地身份、配对配置并清空統計；实际UDP PCB在Process懒创建。 */
bool UDPSoundEvent_Init(uint32_t now_ms, const BoardIdentityProfile *profile,
                        uint32_t (*tick)(void), UDPSoundEvent_Log log);

/**
 * @brief 同步当前有效共同域；撤域或换域时清除旧候选与未消费配对结果。
 * @param id 已由上层确认有效的非零共同域，0表示撤销声音事件收发资格。
 * @return 无返回值；保留累计配对统计，不影响DOMAIN握手或其它消息收发。
 * @note 仅NO_SYS串行前台调用；同一非零域重复同步保留候选，流边界须先传0。
 */
void UDPSoundEvent_SetTimeDomain(uint32_t id);

/**
 * @brief 清空配对候选、未消费结果和波形储备，保留计数和网络绑定。
 * @return 无返回值；未初始化时不操作。
 * @note 由前台在模式代次变化时调用，旧PCM槽通过有效标志撤销。
 */
void UDPSoundEvent_ResetPairing(void);

/** @brief 重试绑定、推进配对并限速输出结果/状态；主循环调用。 */
void UDPSoundEvent_Process(uint32_t now_ms);

/** @brief 发布一个本板事件；未绑定/链路不可用/编码失败返回false并计数。 */
bool UDPSoundEvent_Send(uint32_t now_ms, const SoundEventRecord *event);

/** @brief 复制发送/接收/配对计数；NULL不操作。 */
void UDPSoundEvent_GetSnapshot(SoundPairSnapshot *pair, uint32_t *tx_ok, uint32_t *tx_err,
                               uint32_t *rx_ok, uint32_t *rx_bad, uint32_t *foreign);
/** @brief 复制权威结果报文计数；NULL参数不操作。 */
void UDPSoundEvent_GetResultCounters(uint32_t *tx_ok, uint32_t *tx_err,
                                     uint32_t *rx_ok, uint32_t *rx_bad);

#endif
