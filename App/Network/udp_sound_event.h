/**
 * @file        udp_sound_event.h
 * @brief       两板声音事件UDP发布、接收与配对结果输出（仅NO_SYS前台）。
 * @details     使用固定36字节事件线协议，校验来源、事件侧和当前有效域；撤域或换域清旧队列。
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
#include <stdbool.h>
#include <stdint.h>

typedef void (*UDPSoundEvent_Log)(const char *line, int length);
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
