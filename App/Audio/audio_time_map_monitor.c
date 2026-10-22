#include "audio_time_map_monitor.h"
#include "audio_time_anchor.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define OBSERVATION_MAX_AGE_MS 250U
#define LINE_CAPACITY 256U
_Static_assert(sizeof("$AUDIO_MAP,SEQ=4294967295,STATUS=OK,CORE=0,VALID=1,"
    "SESSION=4294967295,L=4294967295,M=4294967295,R=4294967295,"
    "FRAME=18446744073709551615,EST_NS=18446744073709551615,"
    "LO_NS=18446744073709551615,HI_NS=18446744073709551615,SYNC=0\r\n")
    <= LINE_CAPACITY, "map line exceeds application queue capacity");

static AudioTimeMapLog log_line;
static AudioTimeAnchorObservation window[3];
static uint32_t count, output_sequence, last_sequence, last_session;
static bool have_sequence, have_session;
/* Foreground owns this buffer and logger consumes/copies synchronously. */
static char line[LINE_CAPACITY];

static void Decimal(uint64_t value, char out[21])
{
    char reversed[20]; unsigned n = 0U;
    do { reversed[n++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U);
    for (unsigned i=0U; i<n; ++i) { out[i] = reversed[n-i-1U]; }
    out[n] = '\0';
}
static void SignedDecimal(int64_t value, char out[22])
{
    if (value < 0)
    {
        out[0] = '-';
        Decimal((uint64_t)(-(value + 1)) + 1U, out + 1);
    }
    else { Decimal((uint64_t)value, out); }
}
static uint64_t TimeNs(const AudioTimeMapAnchor *a)
{ return (uint64_t)a->sec * UINT64_C(1000000000) + a->nsec; }
static bool Valid(const AudioTimeMapAnchor *a)
{
    return a->valid && a->session != 0U && a->nsec < 1000000000U &&
           a->frame_hi >= a->frame_lo && a->frame_hi != UINT64_MAX &&
           a->frame_hi-a->frame_lo <= AUDIO_TIME_MAP_MAX_WIDTH_FRAMES;
}
static void Emit(int length)
{ if (length > 0 && (size_t)length < sizeof line) { log_line(line,length); } }
static void Failure(const char *status, uint32_t source, AudioTimeMapStatus core)
{
    count = 0U;
    ++output_sequence;
    Emit(snprintf(line,sizeof line,
        "$AUDIO_MAP,SEQ=%" PRIu32 ",STATUS=%s,CORE=%u,VALID=0,SRC=%" PRIu32 ",SYNC=0\r\n",
        output_sequence,status,(unsigned)core,source));
}
static void Calculate(void)
{
    const AudioTimeMapAnchor *middle = &window[1].anchor;
    const uint64_t frame = middle->frame_lo + (middle->frame_hi-middle->frame_lo+1U)/2U;
    AudioTimeMapResult result;
    AudioTimeMapStatus status = AudioTimeMap_Estimate(&window[0].anchor,&window[2].anchor,
                                                    middle->session,frame,&result);
    if (status != AUDIO_TIME_MAP_OK) { Failure("CORE",window[2].sequence,status);return; }
    char target[21],estimate[21],lower[21],upper[21];
    Decimal(frame,target);Decimal(result.estimate_ns,estimate);
    Decimal(result.model_lower_ns,lower);Decimal(result.model_upper_ns,upper);
    ++output_sequence;
    Emit(snprintf(line,sizeof line,
        "$AUDIO_MAP,SEQ=%" PRIu32 ",STATUS=OK,CORE=0,VALID=1,SESSION=%" PRIu32
        ",L=%" PRIu32 ",M=%" PRIu32 ",R=%" PRIu32 ",FRAME=%s,EST_NS=%s,LO_NS=%s,HI_NS=%s,SYNC=0\r\n",
        output_sequence,middle->session,window[0].sequence,window[1].sequence,
        window[2].sequence,target,estimate,lower,upper));
    const uint64_t observed_ns = TimeNs(middle);
    /* Both validated absolute PTP values <= 4.295e18, safely below INT64_MAX. */
    const int64_t residual_ns = observed_ns >= result.estimate_ns ?
        (int64_t)(observed_ns-result.estimate_ns) : -(int64_t)(result.estimate_ns-observed_ns);
    char observed[21],residual[22];
    Decimal(observed_ns,observed);SignedDecimal(residual_ns,residual);
    Emit(snprintf(line,sizeof line,
        "$AUDIO_MAP_CHECK,SEQ=%" PRIu32 ",OBS_NS=%s,RES_NS=%s,SYNC=0\r\n",
        output_sequence,observed,residual));
    count = 0U;
}
void AudioTimeMapMonitor_Init(AudioTimeMapLog log)
{
    log_line = log;count = output_sequence = last_sequence = last_session = 0U;
    have_sequence = have_session = false;
    memset(window,0,sizeof window);
}
void AudioTimeMapMonitor_Process(uint32_t now_ms, bool processing_allowed)
{
    AudioTimeAnchorObservation observation;
    if (log_line == NULL || !processing_allowed || !AudioTimeAnchor_TakeObservation(&observation)) { return; }
    const bool sequence_gap = have_sequence && observation.sequence != last_sequence + 1U;
    have_sequence = true;last_sequence = observation.sequence;
    if (!Valid(&observation.anchor)) { Failure("ANCHOR",observation.sequence,AUDIO_TIME_MAP_OK);return; }
    if ((uint32_t)(now_ms-observation.captured_ms) > OBSERVATION_MAX_AGE_MS)
    { Failure("STALE",observation.sequence,AUDIO_TIME_MAP_OK);return; }
    if (sequence_gap) { Failure("SEQUENCE",observation.sequence,AUDIO_TIME_MAP_OK); }
    else if (have_session && observation.anchor.session != last_session)
    { Failure("SESSION",observation.sequence,AUDIO_TIME_MAP_OK); }
    else if (count != 0U && (TimeNs(&observation.anchor) <= TimeNs(&window[count-1U].anchor) ||
             observation.anchor.frame_lo <= window[count-1U].anchor.frame_hi))
    { Failure("ORDER",observation.sequence,AUDIO_TIME_MAP_OK);return; }
    last_session = observation.anchor.session;have_session = true;
    window[count++] = observation;
    if (count == 3U) { Calculate(); }
}
