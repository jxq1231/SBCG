#include "Sync/sync_estimator.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/* Count raw bytes, including LF or CRLF. One extra input byte detects overflow. */
#define CLI_MAX_LINE_BYTES 159U
#define CLI_FIELD_COUNT 6U

static int BadInput(void)
{
    puts("{\"index\":null,\"status\":\"CLI_BAD_INPUT\"}");
    return 2;
}

static int IsHorizontalSpace(char character)
{
    return character == ' ' || character == '\t';
}

static int ParseLine(const char *line, size_t length,
                     uint64_t fields[CLI_FIELD_COUNT])
{
    size_t cursor = 0U;

    for (size_t field = 0U; field < CLI_FIELD_COUNT; ++field)
    {
        while (cursor < length && IsHorizontalSpace(line[cursor])) { ++cursor; }
        if (cursor == length || line[cursor] < '0' || line[cursor] > '9')
        {
            return 0;
        }

        uint64_t value = 0U;
        while (cursor < length && line[cursor] >= '0' && line[cursor] <= '9')
        {
            const uint64_t digit = (uint64_t)(line[cursor] - '0');
            if (value > (UINT64_MAX - digit) / 10U) { return 0; }
            value = value * 10U + digit;
            ++cursor;
        }
        fields[field] = value;
        if (cursor < length && !IsHorizontalSpace(line[cursor])) { return 0; }
    }

    while (cursor < length && IsHorizontalSpace(line[cursor])) { ++cursor; }
    return cursor == length && fields[0] != 0U && fields[0] <= UINT32_MAX;
}

static const char *StatusName(SyncEstimatorStatus status)
{
    switch (status)
    {
        case SYNC_ESTIMATOR_OK: return "SYNC_ESTIMATOR_OK";
        case SYNC_ESTIMATOR_BAD_ARGUMENT: return "SYNC_ESTIMATOR_BAD_ARGUMENT";
        case SYNC_ESTIMATOR_OUT_OF_RANGE: return "SYNC_ESTIMATOR_OUT_OF_RANGE";
        case SYNC_ESTIMATOR_BAD_ORDER: return "SYNC_ESTIMATOR_BAD_ORDER";
        case SYNC_ESTIMATOR_WINDOW_EXCEEDED: return "SYNC_ESTIMATOR_WINDOW_EXCEEDED";
        case SYNC_ESTIMATOR_NEGATIVE_DELAY: return "SYNC_ESTIMATOR_NEGATIVE_DELAY";
    }
    return NULL;
}

int main(void)
{
#ifdef _WIN32
    /* Descriptor 0 is stdin; binary mode preserves CRLF for the raw byte limit.
       MinGW's strict C11 headers do not expose the nonstandard _fileno helper. */
    if (_setmode(0, _O_BINARY) == -1) { return BadInput(); }
#endif

    int received_line = 0;
    for (;;)
    {
        char line[CLI_MAX_LINE_BYTES + 2U];
        memset(line, 0xFF, sizeof(line));
        if (fgets(line, (int)sizeof(line), stdin) == NULL)
        {
            if (ferror(stdin) || !received_line) { return BadInput(); }
            return fflush(stdout) == EOF ? 1 : 0;
        }
        received_line = 1;

        /* Find fgets' final terminator, then reject embedded NUL and its suffix. */
        size_t length = sizeof(line);
        do { --length; } while (line[length] != '\0');
        if (length > CLI_MAX_LINE_BYTES || memchr(line, '\0', length) != NULL)
        {
            return BadInput();
        }
        if (length != 0U && line[length - 1U] == '\n')
        {
            --length;
            if (length != 0U && line[length - 1U] == '\r') { --length; }
        }

        uint64_t fields[CLI_FIELD_COUNT];
        if (!ParseLine(line, length, fields)) { return BadInput(); }
        const uint32_t index = (uint32_t)fields[0];
        const SyncExchange exchange = {fields[1], fields[2], fields[3], fields[4]};
        SyncEstimate result = {0, 0};
        const SyncEstimatorStatus status =
            SyncEstimator_Calculate(&exchange, fields[5], &result);
        const char *status_name = StatusName(status);
        if (status_name == NULL) { return 1; }

        int printed;
        if (status == SYNC_ESTIMATOR_OK)
        {
            printed = printf("{\"index\":%" PRIu32 ",\"status\":\"%s\","
                             "\"offset_x2_ns\":\"%" PRId64 "\","
                             "\"mean_path_delay_x2_ns\":\"%" PRId64 "\"}\n",
                             index, status_name, result.offset_x2_ns,
                             result.mean_path_delay_x2_ns);
        }
        else
        {
            printed = printf("{\"index\":%" PRIu32 ",\"status\":\"%s\"}\n",
                             index, status_name);
        }
        if (printed < 0) { return 1; }
    }
}
