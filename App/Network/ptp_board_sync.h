/**
 * @file ptp_board_sync.h
 * @brief 固定A参考板与B跟随板的单播两步PTPv2硬件四时刻交换。
 * @details 外包现有网络钩子；A发Sync/FollowUp并回应DelayReq，B交接完整硬件四时刻。
 * @note NO_SYS串行前台使用，不写时钟；仅固定域0/端口1，不含BMCA或完整PTPv2互操作保证。
 *       六秒启动静默依赖有界网络与前台调度；不声称跨掉电的唯一启动身份或抗恶意重放。
 */
#ifndef SBCG_PTP_BOARD_SYNC_H
#define SBCG_PTP_BOARD_SYNC_H
#include "BSP/board_identity.h"
#include "Sync/sync_estimator.h"
#include <stdbool.h>
#include <stdint.h>
/**
 * @brief 核对两个外层钩子及回调状态后卸载服务。
 * @return true为已卸载或尚未安装；false为活动回调/所有权不符，链和PCB保持原样。
 * @note 先卸载本模块，再卸载UDPTimeExchange及初始化内层旧观察器；仅前台调用。
 */
bool PtpBoardSync_Deinit(void);
/**
 * @brief 复制登记身份并安装最外层硬件时间戳包装。
 * @param now_ms 当前HAL毫秒计数，用作启动静默及绑定重试起点。
 * @param profile 非NULL登记身份，复制内容，不保存指针；必须精确符合默认UID映射。
 * @param tick 非NULL长期有效的HAL毫秒读取函数。
 * @param log 可为NULL的长期有效日志函数，须在返回前消费或复制字符。
 * @return true为已安装；false为身份、钩子或卸载错误。
 * @note 先安装旧PtpRx/PtpTx及UDPTimeExchange；不初始化硬件，不校准时钟。
 */
bool PtpBoardSync_Init(uint32_t now_ms,const BoardIdentityProfile *profile,
                       uint32_t (*tick)(void),void (*log)(const char *,int));
/**
 * @brief 推进绑定、启动静默、过期、A周期发送/B请求和日志。
 * @param now_ms HAL毫秒计数，使用无符号差值处理回绕。
 * @return 无返回值。
 * @note 串行前台调用；A每至少250ms一次Sync，不补发；失败交换不复用序号重试。
 */
void PtpBoardSync_Process(uint32_t now_ms);
/**
 * @brief 消费B端最新完整四时刻的CPU副本。
 * @param exchange 非NULL输出；成功复制自然A/B方向的四时刻，失败保持原值。
 * @param captured_ms 非NULL输出，完整结果形成时HAL毫秒计数。
 * @return true为消费一次结果，false为无结果、超时或参数为空。
 * @note 无pbuf或DMA所有权交接；最多保留一项，待消费时拒收下一Sync。
 */
bool PtpBoardSync_TakeExchange(SyncExchange *exchange,uint32_t *captured_ms);
/**
 * @brief 时钟相位或速率修订后废弃所有待配对和待交接记录。
 * @param epoch 当前本地时钟纪元，仅记录诊断；同纪元的频率修订也须调用。
 * @return 无返回值。
 * @note 仅在包装外前台调用；保持发送序号、对端序号水位和启动静默状态，不执行时钟写入。
 */
void PtpBoardSync_NotifyClockChange(uint32_t epoch);
/**
 * @brief 消费一次对端时钟倒退或六秒报文失联事件。
 * @return true为本次取走事件；false为无待取事件。
 * @note 仅前台；这是异常/重新获取信号，不是可靠跨掉电启动识别。
 */
bool PtpBoardSync_TakePeerReset(void);
#endif
