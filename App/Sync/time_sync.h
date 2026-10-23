/**
 * @file        time_sync.h
 * @brief       固定A参考/B跟随的前台时间同步协调接口。
 * @details     安装板间协议包装，B端消费硬件四时刻并协调纯C伺服、消费者失效与有界BSP校正。
 * @note        ETH/PTP须先完成角色对应初始化，B采用BSP记录的已确认有界频率。仅NO_SYS串行前台调用，不表示独立精度验收。
 */
#ifndef SBCG_TIME_SYNC_H
#define SBCG_TIME_SYNC_H
#include "BSP/board_identity.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   核对并卸载最外层板间同步协议，停用协调器。
 * @return  true表示安全卸载；false表示网络回调/包装所有权不符，原协调状态保持。
 * @note    须先于UDPTimeExchange及旧PTP观察器卸载；不改硬件或已应用频率。
 */
bool TimeSync_Deinit(void);
/**
 * @brief   保存角色并安装板间协议，B端初始化纯C伺服。
 * @param   now_ms 当前HAL毫秒计数，用作协议与日志起点，允许自然回绕。
 * @param   profile 非NULL登记身份，只复制角色；协议层另行复制并严格核对整个身份资料。
 * @param   tick 非NULL且长期有效的毫秒回调，用于协议处理后的新鲜年龄判断。
 * @param   log 可为NULL的日志回调，返回前须消费或复制临时文本，不得保存指针。
 * @return  true表示已安装；false表示卸载、身份、读时或B板已确认频率超出伺服范围等前置条件失败。
 * @note    在PtpClock与旧UDPTimeExchange初始化之后调用，不初始化PTP硬件或掩盖未知频率。
 */
bool TimeSync_Init(uint32_t now_ms,const BoardIdentityProfile *profile,
                   uint32_t (*tick)(void),void (*log)(const char *,int));
/**
 * @brief   放行时处理板间交换、B端估计/控制和每秒限速观测。
 * @param   now_ms 当前HAL毫秒计数；协议处理后再读tick取得新鲜时间，均使用无符号差。
 * @param   processing_allowed 本次前台是否允许处理；false不发送协议、不估计、不写时钟。
 * @return  无返回值。
 * @note    A不运行伺服或写时钟。B校正前要求TX空闲并清理旧配对与音频窗口，失败进入FAULT；SYNC始终为0。
 */
void TimeSync_Process(uint32_t now_ms,bool processing_allowed);
#endif
