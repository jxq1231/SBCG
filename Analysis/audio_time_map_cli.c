/* Host-only decimal adapter. Never linked into the STM32 firmware. */
#include "Audio/audio_time_map.h"

#include <inttypes.h>
#include <stdio.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define MAX_LINE_BYTES 512U
#define FIELD_COUNT 15U

static int BadInput(void)
{
    (void)fputs("CLI_BAD_INPUT: expected 15 bounded unsigned decimal tokens\n", stderr);
    return 2;
}

static int HorizontalSpace(char value)
{
    return value == ' ' || value == '\t';
}

static int Parse(const char *line, size_t length, uint64_t fields[FIELD_COUNT])
{
    size_t cursor = 0U;
    for (size_t field = 0U; field < FIELD_COUNT; ++field)
    {
        while (cursor < length && HorizontalSpace(line[cursor])) { ++cursor; }
        if (cursor == length || line[cursor] < '0' || line[cursor] > '9') { return 0; }
        uint64_t value = 0U;
        while (cursor < length && line[cursor] >= '0' && line[cursor] <= '9')
        {
            const uint64_t digit = (uint64_t)(line[cursor] - '0');
            if (value > (UINT64_MAX - digit) / 10U) { return 0; }
            value = value * 10U + digit;
            ++cursor;
        }
        fields[field] = value;
        if (cursor < length && !HorizontalSpace(line[cursor])) { return 0; }
    }
    while (cursor < length && HorizontalSpace(line[cursor])) { ++cursor; }
    return cursor == length && fields[1] <= 1U && fields[7] <= 1U &&
           fields[2] <= UINT32_MAX && fields[5] <= UINT32_MAX && fields[6] <= UINT32_MAX &&
           fields[8] <= UINT32_MAX && fields[11] <= UINT32_MAX && fields[12] <= UINT32_MAX &&
           fields[13] <= UINT32_MAX;
}

static int Process(const char *line, size_t length)
{
    uint64_t fields[FIELD_COUNT];
    if (!Parse(line, length, fields)) { return BadInput(); }
    const AudioTimeMapAnchor left = {
        fields[1] != 0U, (uint32_t)fields[2], fields[3], fields[4],
        (uint32_t)fields[5], (uint32_t)fields[6]
    };
    const AudioTimeMapAnchor right = {
        fields[7] != 0U, (uint32_t)fields[8], fields[9], fields[10],
        (uint32_t)fields[11], (uint32_t)fields[12]
    };
    AudioTimeMapResult result = {false, 0U, 0U, 0U};
    const AudioTimeMapStatus status = AudioTimeMap_Estimate(
        &left, &right, (uint32_t)fields[13], fields[14], &result);
    if (printf("%" PRIu64 " %u %u %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
               fields[0], (unsigned)status, result.valid ? 1U : 0U,
               result.estimate_ns, result.model_lower_ns, result.model_upper_ns) < 0)
    {
        (void)fputs("CLI_OUTPUT_ERROR\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
#ifdef _WIN32
    if (_setmode(0, _O_BINARY) == -1) { return BadInput(); }
#endif
    char line[MAX_LINE_BYTES];
    size_t length = 0U;
    int received = 0;
    for (;;)
    {
        const int byte = fgetc(stdin);
        if (byte == EOF)
        {
            if (ferror(stdin) || (!received && length == 0U)) { return BadInput(); }
            if (length != 0U)
            {
                const int status = Process(line, length);
                if (status != 0) { return status; }
            }
            return fflush(stdout) == EOF ? 1 : 0;
        }
        if (byte == '\n')
        {
            if (length != 0U && line[length - 1U] == '\r') { --length; }
            const int status = Process(line, length);
            if (status != 0) { return status; }
            received = 1;
            length = 0U;
        }
        else
        {
            if (length >= MAX_LINE_BYTES || byte == 0 || byte > 127) { return BadInput(); }
            line[length++] = (char)byte;
        }
    }
}
