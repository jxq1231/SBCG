#include "codec_monitor.h"
#include "BSP/bsp_audio_codec.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#define CODEC_POLL_PERIOD_MS 2000U
#define CODEC_LOG_CAPACITY 192U

static CodecMonitor_Log log_line;
static uint32_t last_poll_ms, sequence, ok_count, error_count;

void CodecMonitor_Init(uint32_t now_ms, CodecMonitor_Log log)
{
    log_line = log;
    last_poll_ms = now_ms;
    sequence = ok_count = error_count = 0U;
}

static const char *StatusName(BSP_AudioCodecStatus status)
{
    switch (status)
    {
        case BSP_CODEC_OK: return "OK";
        case BSP_CODEC_BUSY: return "BUSY";
        case BSP_CODEC_IO_ERROR: return "IO_ERROR";
        case BSP_CODEC_ID_MISMATCH: return "ID_MISMATCH";
        default: return "INVALID_ARG";
    }
}

void CodecMonitor_Process(uint32_t now_ms, bool io_allowed)
{
    if (!io_allowed || log_line == NULL ||
        (uint32_t)(now_ms - last_poll_ms) < CODEC_POLL_PERIOD_MS) { return; }
    last_poll_ms = now_ms;
    ++sequence;
    BSP_AudioCodecResult result;
    const BSP_AudioCodecStatus status = BSP_AudioCodec_ReadId(&result);
    if (status == BSP_CODEC_OK) { ++ok_count; }
    else { ++error_count; }

    char id_field[16] = "";
    if (result.id_valid)
    {
        (void)snprintf(id_field, sizeof id_field, ",ID=%04" PRIX16, result.id);
    }
    char line[CODEC_LOG_CAPACITY];
    const int length = snprintf(line, sizeof line,
        "$CODEC,SEQ=%" PRIu32 ",TICK_MS=%" PRIu32 ",ADDR7=%02X,RESULT=%s%s"
        ",HAL_ERR=%08" PRIX32 ",READ_MS=%" PRIu32 ",OK=%" PRIu32 ",ERRORS=%" PRIu32 "\r\n",
        sequence, now_ms, (unsigned)BSP_AUDIO_CODEC_ADDRESS_7BIT, StatusName(status), id_field,
        result.hal_error, result.read_ms, ok_count, error_count);
    if (length > 0 && (size_t)length < sizeof line) { log_line(line, length); }
}
