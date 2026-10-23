#include "Temperature/sound_speed.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static void expected(int32_t temperature_milli_c, uint32_t expected_mm_s)
{
    uint32_t actual_mm_s = 0U;
    CHECK(SoundSpeed_Calculate(temperature_milli_c, &actual_mm_s));
    CHECK(actual_mm_s == expected_mm_s);
}

static void vectors_and_rounding(void)
{
    expected(24400, 346086U);
    expected(-10100, 325179U);
    expected(20000, 343420U);
    expected(25000, 346450U);
    expected(-10000, 325240U);
    expected(0, 331300U);
    expected(-20000, 319180U);
    expected(60000, 367660U);
    expected(250, 331452U);
    expected(-250, 331148U);
    expected(1, 331301U);
    expected(-1, 331299U);
    expected(249, 331451U);
    expected(-249, 331149U);
    expected(251, 331452U);
    expected(-251, 331148U);
}

static void rejection(void)
{
    static const int32_t invalid[] = {-20001, 60001, INT32_MIN, INT32_MAX};
    unsigned i;
    CHECK(!SoundSpeed_Calculate(20000, NULL));
    CHECK(!SoundSpeed_Calculate(INT32_MIN, NULL));
    for (i = 0U; i < sizeof invalid / sizeof invalid[0]; ++i)
    {
        uint32_t actual_mm_s = UINT32_C(0xDEADBEEF);
        CHECK(!SoundSpeed_Calculate(invalid[i], &actual_mm_s));
        CHECK(actual_mm_s == UINT32_C(0xDEADBEEF));
    }
}

static void model_properties(void)
{
    uint32_t previous_mm_s = 0U;
    int32_t temperature_milli_c;
    for (temperature_milli_c = -20000; temperature_milli_c <= 60000;
         ++temperature_milli_c)
    {
        uint32_t actual_mm_s;
        int64_t error_thousandths_mm_s;
        CHECK(SoundSpeed_Calculate(temperature_milli_c, &actual_mm_s));
        CHECK(actual_mm_s >= previous_mm_s);
        /* Independent dimensional check: rounded result differs by <= .5 mm/s. */
        error_thousandths_mm_s = ((int64_t)actual_mm_s - 331300) * 1000
                                - (int64_t)606 * temperature_milli_c;
        CHECK(error_thousandths_mm_s >= -500);
        CHECK(error_thousandths_mm_s <= 500);
        if (error_thousandths_mm_s == 500) { CHECK(temperature_milli_c > 0); }
        if (error_thousandths_mm_s == -500) { CHECK(temperature_milli_c < 0); }
        previous_mm_s = actual_mm_s;
    }
}

int main(void)
{
    vectors_and_rounding();
    rejection();
    model_properties();
    printf("PASS sound_speed: vectors_and_rounding, rejection, model_properties "
           "(%u checks)\n", checks);
    return 0;
}
