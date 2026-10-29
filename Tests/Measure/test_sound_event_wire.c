#include "Network/sound_event_wire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static SoundEventRecord Event(void)
{
    SoundEventRecord e={0};e.valid=true;e.type=SOUND_EVENT_CHIRP;e.quality=77U;
    e.side=SOUND_SIDE_B;e.time_domain=7U;e.session=9U;e.sequence=3U;
    e.time_ns=1234567890123U;e.frame=99887766U;e.fraction_q16=12345U;return e;
}
int main(void)
{
    SoundEventRecord event=Event(),decoded;uint8_t data[SOUND_EVENT_WIRE_BYTES];
    assert(SoundEventWire_Encode(&event,data,sizeof data)==SOUND_EVENT_WIRE_OK);
    assert(data[0]=='S'&&data[1]=='E'&&data[2]=='V'&&data[3]=='T');
    assert(data[4]==SOUND_EVENT_WIRE_VERSION && data[5]==SOUND_SIDE_B);
    assert(SoundEventWire_Decode(data,sizeof data,&decoded)==SOUND_EVENT_WIRE_OK);
    assert(decoded.valid && decoded.type==SOUND_EVENT_CHIRP && decoded.quality==77U);
    assert(decoded.side==SOUND_SIDE_B && decoded.time_domain==7U && decoded.session==9U);
    assert(decoded.sequence==3U && decoded.time_ns==event.time_ns);
    assert(decoded.frame==event.frame);
    assert(decoded.fraction_q16==event.fraction_q16);
    assert(decoded.toa_candidate_count==0U);
    event.toa_candidate_count=3U;
    event.toa_candidate_q16[0]=0;event.toa_candidate_q16[1]=589824;event.toa_candidate_q16[2]=-589824;
    assert(SoundEventWire_Encode(&event,data,sizeof data)==SOUND_EVENT_WIRE_OK);
    assert(SoundEventWire_Decode(data,sizeof data,&decoded)==SOUND_EVENT_WIRE_OK);
    assert(decoded.toa_candidate_count==3U && decoded.toa_candidate_q16[0]==0 &&
           decoded.toa_candidate_q16[1]==589824 && decoded.toa_candidate_q16[2]==-589824);

    /* v1/v2/v3兼容：旧长度事件仍可被新接收端解码，缺失字段为0。 */
    uint8_t v1[SOUND_EVENT_WIRE_V1_BYTES];
    memcpy(v1,data,sizeof v1);v1[4]=1U;
    assert(SoundEventWire_Decode(v1,sizeof v1,&decoded)==SOUND_EVENT_WIRE_OK);
    assert(decoded.valid && decoded.toa_candidate_count==0U && decoded.frame==0U);
    uint8_t v2[SOUND_EVENT_WIRE_V2_BYTES];
    memcpy(v2,data,sizeof v2);v2[4]=2U;
    assert(SoundEventWire_Decode(v2,sizeof v2,&decoded)==SOUND_EVENT_WIRE_OK);
    assert(decoded.valid && decoded.toa_candidate_count==3U && decoded.frame==0U);
    uint8_t v3[SOUND_EVENT_WIRE_V3_BYTES];
    memcpy(v3,data,sizeof v3);v3[4]=3U;
    assert(SoundEventWire_Decode(v3,sizeof v3,&decoded)==SOUND_EVENT_WIRE_OK);
    assert(decoded.valid && decoded.toa_candidate_count==3U &&
           decoded.frame==event.frame && decoded.fraction_q16==0U);

    uint8_t bad[SOUND_EVENT_WIRE_BYTES];
    assert(SoundEventWire_Decode(NULL,sizeof bad,&decoded)==SOUND_EVENT_WIRE_BAD_ARGUMENT);
    assert(SoundEventWire_Decode(data,sizeof data-1,&decoded)==SOUND_EVENT_WIRE_BAD_LENGTH);
    memcpy(bad,data,sizeof bad);bad[0]='X';
    assert(SoundEventWire_Decode(bad,sizeof bad,&decoded)==SOUND_EVENT_WIRE_BAD_MAGIC);
    memcpy(bad,data,sizeof bad);bad[4]=5U;
    assert(SoundEventWire_Decode(bad,sizeof bad,&decoded)==SOUND_EVENT_WIRE_BAD_VERSION);
    memcpy(bad,data,sizeof bad);bad[5]=2U;
    assert(SoundEventWire_Decode(bad,sizeof bad,&decoded)==SOUND_EVENT_WIRE_BAD_FIELD);
    memcpy(bad,data,sizeof bad);bad[6]=0U;
    assert(SoundEventWire_Decode(bad,sizeof bad,&decoded)==SOUND_EVENT_WIRE_BAD_FIELD);
    memcpy(bad,data,sizeof bad);bad[7]=101U;
    assert(SoundEventWire_Decode(bad,sizeof bad,&decoded)==SOUND_EVENT_WIRE_BAD_FIELD);
    memcpy(bad,data,sizeof bad);bad[28]=1U;
    assert(SoundEventWire_Decode(bad,sizeof bad,&decoded)==SOUND_EVENT_WIRE_BAD_FIELD);
    memcpy(bad,data,sizeof bad);bad[24]=0xFFU;bad[25]=0xFFU;bad[26]=0xFFU;bad[27]=0xFFU;
    assert(SoundEventWire_Decode(bad,sizeof bad,&decoded)==SOUND_EVENT_WIRE_BAD_FIELD);
    memcpy(bad,data,sizeof bad);memset(bad+8,0,4U);
    assert(SoundEventWire_Decode(bad,sizeof bad,&decoded)==SOUND_EVENT_WIRE_BAD_FIELD);
    memset(&decoded,0xA5,sizeof decoded);
    assert(SoundEventWire_Encode(NULL,data,sizeof data)==SOUND_EVENT_WIRE_BAD_ARGUMENT);
    assert(SoundEventWire_Encode(&event,data,sizeof data-1)==SOUND_EVENT_WIRE_BAD_LENGTH);
    puts("PASS sound event wire: v3 big-endian roundtrip with TOA candidates and frame, v1/v2 compat gates");
    return 0;
}
