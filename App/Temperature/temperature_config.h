/**
 * @file        temperature_config.h
 * @brief       集中定义DHT11采样时序、有效期、数值范围及开发UID白名单。
 * @details     声明本模块接口、数据类型或配置常量。提供毫秒调度间隔、微秒脉宽/超时、毫摄氏度温度范围和千分之一百分比湿度范围。
 * @note        由温度服务、帧解析和DHT11 BSP使用；UID仅许可开发采样，不分配网络A/B角色。
 */
#ifndef SBCG_TEMPERATURE_CONFIG_H
#define SBCG_TEMPERATURE_CONFIG_H

#include <stdint.h>

/** @brief 前台调度时限，单位由宏名MS/US指明；并非额外定时器或IRQ配置。 */
#define TEMP_POWERUP_MS 1000U
#define TEMP_PERIOD_MS 5000U
#define TEMP_LOG_PERIOD_MS 1000U
#define TEMP_START_LOW_MS 20U
#define TEMP_START_MIN_US 18000U
#define TEMP_START_MAX_US 30000U
#define TEMP_CAPTURE_TIMEOUT_US 6000U
#define TEMP_EDGE_TIMEOUT_US 150U
#define TEMP_RESP_MIN_US 60U
#define TEMP_RESP_MAX_US 110U
#define TEMP_BIT_LOW_MIN_US 35U
#define TEMP_BIT_LOW_MAX_US 80U
#define TEMP_ZERO_MIN_US 15U
#define TEMP_ZERO_MAX_US 45U
#define TEMP_ONE_MIN_US 55U
#define TEMP_ONE_MAX_US 100U
#define TEMP_STALE_MS 15000U
/** @brief 帧解析和手动输入采用的温度范围，单位毫摄氏度。 */
#define TEMP_MIN_MILLI_C (-20000)
#define TEMP_MAX_MILLI_C 60000
/** @brief 传感器相对湿度范围，单位千分之一百分比。 */
#define TEMP_RH_MIN_MILLI_PCT 5000U
#define TEMP_RH_MAX_MILLI_PCT 95000U
/* Development sampling allowlist only; this does not assign an A/B role. */
#define TEMP_TEST_UID_W0 UINT32_C(0x001A002D)
#define TEMP_TEST_UID_W1 UINT32_C(0x30365105)
#define TEMP_TEST_UID_W2 UINT32_C(0x36363631)

#endif
