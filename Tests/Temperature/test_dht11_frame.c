#include "Temperature/dht11_frame.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static void checksum(uint8_t frame[5])
{
    frame[4] = (uint8_t)((unsigned)frame[0] + frame[1] + frame[2] + frame[3]);
}

static void valid(uint8_t frame[5], int32_t temperature_milli_c,
                  uint32_t relative_humidity_milli_pct)
{
    Dht11Sample sample = {INT32_MIN, UINT32_MAX};
    uint8_t before[5];
    memcpy(before, frame, sizeof before);
    CHECK(Dht11Frame_Decode(frame, &sample) == DHT11_FRAME_OK);
    CHECK(sample.temperature_milli_c == temperature_milli_c);
    CHECK(sample.relative_humidity_milli_pct == relative_humidity_milli_pct);
    CHECK(memcmp(frame, before, sizeof before) == 0);
}

static void invalid(const uint8_t frame[5], Dht11FrameStatus expected)
{
    Dht11Sample sample = {123456, 654321U};
    uint8_t before[5];
    if (frame != NULL) { memcpy(before, frame, sizeof before); }
    CHECK(Dht11Frame_Decode(frame, &sample) == expected);
    CHECK(sample.temperature_milli_c == 123456);
    CHECK(sample.relative_humidity_milli_pct == 654321U);
    if (frame != NULL) { CHECK(memcmp(frame, before, sizeof before) == 0); }
}

static void reference_vectors(void)
{
    uint8_t positive[5] = {53, 0, 24, 4, 81};
    uint8_t negative[5] = {53, 0, 10, 129, 192};
    uint8_t zero[5] = {53, 0, 0, 0, 53};
    uint8_t negative_zero[5] = {53, 0, 0, 128, 181};
    valid(positive, 24400, 53000U);
    valid(negative, -10100, 53000U);
    valid(zero, 0, 53000U);
    valid(negative_zero, 0, 53000U);
}

static void argument_and_checksum(void)
{
    uint8_t frame[5] = {53, 0, 24, 4, 81};
    unsigned byte;
    unsigned bit;
    invalid(NULL, DHT11_FRAME_ARGUMENT);
    CHECK(Dht11Frame_Decode(frame, NULL) == DHT11_FRAME_ARGUMENT);
    CHECK(Dht11Frame_Decode(NULL, NULL) == DHT11_FRAME_ARGUMENT);
    for (byte = 0U; byte < 5U; ++byte)
    {
        for (bit = 0U; bit < 8U; ++bit)
        {
            uint8_t damaged[5];
            memcpy(damaged, frame, sizeof damaged);
            damaged[byte] ^= (uint8_t)(1U << bit);
            invalid(damaged, DHT11_FRAME_CHECKSUM);
        }
    }
    /* A valid frame wraps 95 + 0 + 20 + 137 = 252, below wrap.
     * 95 + 0 + 255 + 137 = 487 wraps to 231 but is out of range.
     * CHECKSUM must be evaluated before semantic range validation. */
    frame[0] = 95U; frame[2] = 255U; frame[3] = 137U;
    checksum(frame);
    CHECK(frame[4] == 231U);
    invalid(frame, DHT11_FRAME_RANGE);
    frame[4] ^= 1U;
    invalid(frame, DHT11_FRAME_CHECKSUM);
}

static void ranges_and_format(void)
{
    uint8_t frame[5] = {5, 0, 20, 128, 153};
    unsigned decimal;
    valid(frame, -20000, 5000U);
    frame[0] = 95U; frame[2] = 60U; frame[3] = 0U; checksum(frame);
    valid(frame, 60000, 95000U);
    frame[3] = 1U; checksum(frame); invalid(frame, DHT11_FRAME_RANGE);
    frame[2] = 20U; frame[3] = 129U; checksum(frame);
    invalid(frame, DHT11_FRAME_RANGE);
    frame[2] = 19U; frame[3] = 137U; checksum(frame);
    valid(frame, -19900, 95000U);
    frame[2] = 59U; frame[3] = 9U; checksum(frame);
    valid(frame, 59900, 95000U);
    frame[2] = 0U; frame[3] = 0U;
    frame[0] = 4U; checksum(frame); invalid(frame, DHT11_FRAME_RANGE);
    frame[0] = 96U; checksum(frame); invalid(frame, DHT11_FRAME_RANGE);
    frame[0] = 255U; checksum(frame); invalid(frame, DHT11_FRAME_RANGE);
    frame[0] = 53U; frame[1] = 1U; checksum(frame);
    invalid(frame, DHT11_FRAME_RANGE);
    frame[1] = 255U; checksum(frame); invalid(frame, DHT11_FRAME_RANGE);
    frame[1] = 0U;
    for (decimal = 10U; decimal < 128U; ++decimal)
    {
        frame[3] = (uint8_t)decimal; checksum(frame);
        invalid(frame, DHT11_FRAME_RANGE);
        frame[3] = (uint8_t)(decimal | 128U); checksum(frame);
        invalid(frame, DHT11_FRAME_RANGE);
    }
    frame[2] = 61U; frame[3] = 0U; checksum(frame);
    invalid(frame, DHT11_FRAME_RANGE);
    frame[2] = 21U; frame[3] = 128U; checksum(frame);
    invalid(frame, DHT11_FRAME_RANGE);
    frame[2] = 255U; checksum(frame); invalid(frame, DHT11_FRAME_RANGE);
}

int main(void)
{
    reference_vectors();
    argument_and_checksum();
    ranges_and_format();
    printf("PASS dht11_frame: reference_vectors, argument_and_checksum, "
           "ranges_and_format (%u checks)\n", checks);
    return 0;
}
