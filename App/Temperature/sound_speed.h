#ifndef SBCG_SOUND_SPEED_H
#define SBCG_SOUND_SPEED_H

#include <stdbool.h>
#include <stdint.h>

/* Temperature-only approximation: c = 331.3 + 0.606 T [m/s].
 * Result is nearest mm/s, ties away from zero in the temperature term.
 * Out-of-range temperatures or NULL output leave output untouched. */
bool SoundSpeed_Calculate(int32_t temperature_milli_c, uint32_t *out_mm_s);

#endif
