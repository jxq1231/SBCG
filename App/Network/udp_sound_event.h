/**
 * @file        udp_sound_event.h
 * @brief       两板声音事件UDP发布、接收与配对结果输出（仅NO_SYS前台）。
 * @details     使用固定36字节事件线协议；本模块不做PTP同步，只校验来源IP/端口、事件侧和配对门控。
 * @note        事件时间必须是本地PTP模型时间；共同时间域由上层同步状态提供，调用方不得伪造。
 */
#ifndef SBCG_UDP_SOUND_EVENT_H
#define SBCG_UDP_SOUND_EVENT_H

#include "Measure/sound_pair.h"
#include "Measure/measurement_result.h"
#include "Network/domain_wire.h"
#include "BSP/board_identity.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*UDPSoundEvent_Log)(const char *line, int length);
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

/** @brief 初始化本地身份、配对配置并清空統計；实际UDP PCB在Process懒创建。 */
bool UDPSoundEvent_Init(uint32_t now_ms, const BoardIdentityProfile *profile,
                        uint32_t (*tick)(void), UDPSoundEvent_Log log);

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
