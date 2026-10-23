/**
 * @file        sound_speed.c
 * @brief       由温度计算声速近似值。
 * @details     按c=331.3+0.606T米每秒，将毫摄氏度温度修正项四舍五入为毫米每秒。
 * @note        不作湿度补偿；温度范围来自temperature_config.h，失败保持输出。
 */
#include "sound_speed.h"
#include "temperature_config.h"

#include <stddef.h>

/**
 * @brief   按温度近似式计算毫米每秒声速。
 * @param   temperature_milli_c 毫摄氏度温度，配置范围-20000至60000。
 * @param   out_mm_s 可写声速输出，不可为NULL；失败保持原值。
 * @return  温度有效且输出非NULL返回true，否则false。
 * @note    纯计算；温度修正项按最近整数取整，半值远离零，不作湿度补偿。
 */
bool SoundSpeed_Calculate(int32_t temperature_milli_c, uint32_t *out_mm_s)
{
    int64_t scaled_correction;
    int64_t correction_mm_s;

    if ((out_mm_s == NULL)
        || (temperature_milli_c < TEMP_MIN_MILLI_C)
        || (temperature_milli_c > TEMP_MAX_MILLI_C))
    {
        return false;
    }

    scaled_correction = INT64_C(606) * temperature_milli_c;
    /* C division truncates toward zero: bias each sign away from zero. */
    if (scaled_correction < 0)
    {
        correction_mm_s = (scaled_correction - 500) / 1000;
    }
    else
    {
        correction_mm_s = (scaled_correction + 500) / 1000;
    }
    *out_mm_s = (uint32_t)(INT64_C(331300) + correction_mm_s);
    return true;
}
