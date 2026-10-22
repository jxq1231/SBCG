#include "audio_capture.h"
#include "audio_statistics.h"
#include "BSP/bsp_audio.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#define AUDIO_LOG_PERIOD_MS 1000U
static AudioCapture_Log log_line;
static uint32_t initialized_ms, last_log_ms, sequence, level_ms;
static uint32_t previous_callbacks, previous_callback_ms;
static bool have_rate_snapshot, level_valid;
static AudioStatistics levels;

/* newlib-nano and native MinGW differ in long-long printf support. Keep
   the observed-frame counter portable without enabling larger printf libs. */
static void Decimal64(uint64_t value, char output[21])
{
    char reverse[20];
    unsigned count = 0U;
    do { reverse[count++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U);
    for (unsigned i = 0; i < count; ++i) { output[i] = reverse[count - i - 1U]; }
    output[count] = '\0';
}

void AudioCapture_Init(uint32_t now_ms, AudioCapture_Log log)
{
    log_line = log;
    initialized_ms = last_log_ms = now_ms;
    sequence = level_ms = previous_callbacks = previous_callback_ms = 0U;
    have_rate_snapshot = level_valid = false;
    levels = (AudioStatistics){0};
    const bool stopped = BSP_Audio_Stop();
    const bool started = stopped && log != NULL && BSP_Audio_Start();
    if (log == NULL) { return; }
    BSP_AudioSnapshot snapshot;
    BSP_Audio_GetSnapshot(&snapshot);
    char line[224];
    const int length = snprintf(line, sizeof line,
        "$AUDIO_INIT,RESULT=%s,NOMINAL_HZ=%u,DERIVED_MILLI_HZ=%" PRIu32
        ",START_ERR=%" PRIu32 ",CODEC=%" PRIu32 ",I2C_ERR=%08" PRIX32
        ",REG=%04" PRIX16 ",WRITES=%" PRIu16 "\r\n",
        started ? "OK" : "ERROR", BSP_AUDIO_NOMINAL_HZ, snapshot.derived_milli_hz,
        snapshot.start_error, snapshot.codec_status, snapshot.codec_hal_error,
        snapshot.codec_register, snapshot.codec_writes);
    if (length > 0 && (size_t)length < sizeof line) { log(line, length); }
}

void AudioCapture_Process(uint32_t now_ms, bool processing_allowed)
{
    if (log_line == NULL || !processing_allowed) { return; }
    BSP_AudioBlock block;
    if (BSP_Audio_BeginBlock(&block))
    {
        AudioStatistics next;
        const bool computed = AudioStatistics_Compute(block.samples, BSP_AUDIO_HALF_FRAMES, &next);
        if (BSP_Audio_EndBlock(&block, computed))
        { levels = next; level_ms = block.completed_ms; level_valid = true; }
        else { level_valid = false; }
    }
    if ((uint32_t)(now_ms - last_log_ms) < AUDIO_LOG_PERIOD_MS) { return; }
    last_log_ms = now_ms;
    BSP_AudioSnapshot snapshot;
    BSP_Audio_GetSnapshot(&snapshot);
    const uint32_t age_ms = snapshot.callback_count == 0U ? now_ms - initialized_ms
                                                        : now_ms - snapshot.last_callback_ms;
    const char *name = !snapshot.running ? "ERROR" : age_ms >= AUDIO_LOG_PERIOD_MS ? "STALLED" : "RUN";
    const bool valid = snapshot.running && age_ms < AUDIO_LOG_PERIOD_MS && level_valid &&
                       (uint32_t)(now_ms - level_ms) < AUDIO_LOG_PERIOD_MS;
    uint32_t count = 0U, elapsed_ms = 0U;
    if (have_rate_snapshot)
    {
        count = snapshot.callback_count - previous_callbacks;
        elapsed_ms = snapshot.last_callback_ms - previous_callback_ms;
    }
    else if (snapshot.callback_count > 1U)
    {
        count = snapshot.callback_count - 1U;
        elapsed_ms = snapshot.last_callback_ms - snapshot.first_callback_ms;
    }
    uint32_t rate_hz = 0U;
    if (elapsed_ms != 0U)
    {
        const uint64_t rate = (uint64_t)count * BSP_AUDIO_HALF_FRAMES * 1000U / elapsed_ms;
        if (rate <= UINT32_MAX) { rate_hz = (uint32_t)rate; }
    }
    previous_callbacks = snapshot.callback_count;
    previous_callback_ms = snapshot.last_callback_ms;
    have_rate_snapshot = snapshot.callback_count != 0U;
    char level_fields[64] = "";
    if (valid)
    {
        (void)snprintf(level_fields, sizeof level_fields,
            ",P0=%" PRIu32 ",P1=%" PRIu32 ",R0=%" PRIu32 ",R1=%" PRIu32,
            levels.channel[0].peak, levels.channel[1].peak,
            levels.channel[0].rms, levels.channel[1].rms);
    }
    const uint64_t frames = ((uint64_t)snapshot.half_count + snapshot.full_count) * BSP_AUDIO_HALF_FRAMES;
    char frame_text[21];
    Decimal64(frames, frame_text);
    char line[256];
    const int length = snprintf(line, sizeof line,
        "$AUDIO,SEQ=%" PRIu32 ",STATE=%s,FRAMES=%s,H=%" PRIu32
        ",F=%" PRIu32 ",OK=%" PRIu32 ",DROP=%" PRIu32 ",ERR=%" PRIu32
        ",HAL=%08" PRIX32 ",DMA=%08" PRIX32 ",RATE_HZ=%" PRIu32 ",VALID=%u%s\r\n",
        ++sequence, name, frame_text, snapshot.half_count, snapshot.full_count,
        snapshot.processed_blocks, snapshot.dropped_blocks, snapshot.error_count,
        snapshot.hal_error, snapshot.dma_error, rate_hz, valid ? 1U : 0U, level_fields);
    if (length > 0 && (size_t)length < sizeof line) { log_line(line, length); }
}
