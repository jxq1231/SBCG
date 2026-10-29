/**
 * @file        sound_speed.h
 * @brief       由温度计算声速近似值。
 * @details     声明本模块接口、数据类型或配置常量。按c=331.3+0.606T米每秒，将毫摄氏度温度修正项四舍五入为毫米每秒。
 * @note        不作湿度补偿；温度范围来自temperature_config.h，失败保持输出。
 */
#ifndef SBCG_SOUND_SPEED_H
#define SBCG_SOUND_SPEED_H

#include <stdbool.h>
#include <stdint.h>

/* Temperature-only approximation: c = 331.3 + 0.606 T [m/s].
 * Result is nearest mm/s, ties away from zero in the temperature term.
 * Out-of-range temperatures or NULL output leave output untouched. */
/**
 * @brief   按温度近似式计算毫米每秒声速。
 * @param   temperature_milli_c 毫摄氏度温度，配置范围-20000至60000。
 * @param   out_mm_s 可写声速输出，不可为NULL；失败保持原值。
 * @return  温度有效且输出非NULL返回true，否则false。
 * @note    纯计算；温度修正项按最近整数取整，半值远离零，不作湿度补偿。
 */
bool SoundSpeed_Calculate(int32_t temperature_milli_c, uint32_t *out_mm_s);

#endif
