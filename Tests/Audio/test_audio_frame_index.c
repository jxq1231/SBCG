#include "Audio/audio_frame_index.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main(void)
{
    AudioFrameIndex s;
    AudioFrameIndex_Init(&s, 10U);
    CHECK(s.frames == 0U && s.faults == 0U);
    AudioFrameIndex_Complete(&s, 0U, 181U);
    CHECK(s.frames == 8192U && s.faults == 0U);
    AudioFrameIndex_Complete(&s, 1U, 352U);
    CHECK(s.frames == 16384U && s.faults == 0U);
    /* This is a public state value, not a test-only production hook. */
    s.frames = UINT64_C(4294959104);
    AudioFrameIndex_Complete(&s, 0U, 523U);
    CHECK(s.frames == UINT64_C(4294967296) && s.faults == 0U);
    AudioFrameIndex_Complete(&s, 0U, 694U);
    CHECK((s.faults & AUDIO_INDEX_ORDER) != 0U);
    AudioFrameIndex_Complete(&s, 1U, 865U);
    CHECK((s.faults & AUDIO_INDEX_ORDER) != 0U);
    AudioFrameIndex_Init(&s, UINT32_MAX - 100U);
    AudioFrameIndex_Complete(&s, 0U, 70U);
    CHECK(s.faults == 0U);
    AudioFrameIndex_Complete(&s, 1U, 321U);
    CHECK((s.faults & AUDIO_INDEX_LATE) != 0U);
    AudioFrameIndex_Init(&s, 0U);
    AudioFrameIndex_Complete(&s, 0U, 250U);
    CHECK(s.faults == 0U);
    s.frames = UINT64_MAX - 8191U;
    AudioFrameIndex_Complete(&s, 1U, 421U);
    CHECK(s.frames == UINT64_MAX - 8191U && (s.faults & AUDIO_INDEX_OVERFLOW) != 0U);
    puts("PASS: frame numbering, 32-bit boundary, sticky ordering/latency ambiguity, tick wrap and 64-bit overflow");
    return 0;
}
