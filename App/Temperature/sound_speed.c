#include "sound_speed.h"
#include "temperature_config.h"

#include <stddef.h>

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
