#include "Network/ranging_status_wire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)

static RangingStatusMessage Message(void)
{
    return (RangingStatusMessage){
        .state=RANGING_STATE_TRACKING,.direction=1U,.quality=90U,
        .flags=RANGING_STATUS_FLAG_HAS_VALUE|RANGING_STATUS_FLAG_FRESH|
               RANGING_STATUS_FLAG_IDENTITY_VERIFIED|RANGING_STATUS_FLAG_FORMAL,
        .domain=0x11223344U,.authority_clock_epoch=0x55667788U,
        .state_revision=2U,.measurement_id=3U,.temperature_version=4U,
        .sound_speed_mm_s=343000U,.distance_mm=320,.calibration_version=5U,
        .calibration_offset_mm=-7,.reason=0x1234U,
        .source_common_ns=UINT64_C(0x0102030405060708),
        .fresh_deadline_ns=UINT64_C(0x0102030405060D74),
        .display_deadline_ns=UINT64_C(0x0102030405061F48),
        .filtered_delta_ns=933000,.source_key_crc32=0xDEADBEEFU,
        .detection_version=0x0102U,.evidence_kind=RANGING_EVIDENCE_MATCHED};
}

static const uint8_t GOLDEN[88]={
    0x52,0x53,0x54,0x32,0x02,0x01,0x01,0x5A,0x00,0x0F,0x00,0x58,
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x00,0x00,0x00,0x02,
    0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x04,0x00,0x05,0x3B,0xD8,
    0x00,0x00,0x01,0x40,0x00,0x05,0xFF,0xF9,0x12,0x34,0x00,0x00,
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x01,0x02,0x03,0x04,
    0x05,0x06,0x0D,0x74,0x01,0x02,0x03,0x04,0x05,0x06,0x1F,0x48,
    0x00,0x00,0x00,0x00,0x00,0x0E,0x3C,0x88,0xDE,0xAD,0xBE,0xEF,
    0x01,0x02,0x01,0x00};

static const uint8_t ACK_GOLDEN[24]={
    0x52,0x41,0x4B,0x32,0x02,0x00,0x00,0x00,0x11,0x22,0x33,0x44,
    0x55,0x66,0x77,0x88,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x03};

int main(void)
{
    uint8_t data[RANGING_STATUS_WIRE_BYTES];
    RangingStatusMessage source=Message();
    assert(RangingStatusWire_Encode(&source,data,sizeof data)==RANGING_STATUS_WIRE_OK);
    assert(memcmp(data,GOLDEN,sizeof GOLDEN)==0);
    RangingStatusMessage decoded;
    assert(RangingStatusWire_Decode(GOLDEN,sizeof GOLDEN,&decoded)==RANGING_STATUS_WIRE_OK);
    assert(decoded.state==source.state && decoded.direction==source.direction &&
           decoded.quality==source.quality && decoded.flags==source.flags &&
           decoded.domain==source.domain && decoded.measurement_id==source.measurement_id &&
           decoded.distance_mm==source.distance_mm &&
           decoded.source_common_ns==source.source_common_ns &&
           decoded.filtered_delta_ns==source.filtered_delta_ns &&
           decoded.evidence_kind==source.evidence_kind);
    assert(RangingStatusWire_Encode(&source,data,sizeof data-1U)==
           RANGING_STATUS_WIRE_INVALID_LENGTH);

    /* 未知flag、代理冒充身份、FORMAL缺FRESH、deadline逆序均必须拒绝。 */
    source.flags|=0x0010U;assert(RangingStatusWire_Encode(&source,data,sizeof data)!=
        RANGING_STATUS_WIRE_OK);source=Message();
    source.flags=RANGING_STATUS_FLAG_HAS_VALUE|RANGING_STATUS_FLAG_IDENTITY_VERIFIED;
    source.evidence_kind=RANGING_EVIDENCE_PROXY;
    assert(RangingStatusWire_Encode(&source,data,sizeof data)!=
        RANGING_STATUS_WIRE_OK);source=Message();
    source.flags=RANGING_STATUS_FLAG_HAS_VALUE|RANGING_STATUS_FLAG_IDENTITY_VERIFIED|
        RANGING_STATUS_FLAG_FORMAL;
    assert(RangingStatusWire_Encode(&source,data,sizeof data)!=
        RANGING_STATUS_WIRE_OK);source=Message();
    source.fresh_deadline_ns=source.display_deadline_ns+1U;
    assert(RangingStatusWire_Encode(&source,data,sizeof data)!=
        RANGING_STATUS_WIRE_OK);

    /* 无值ACQUIRING/HOLD有值/负delta三个边界。 */
    RangingStatusMessage empty=(RangingStatusMessage){
        .state=RANGING_STATE_ACQUIRING,.direction=0U,.quality=90U,.flags=0U,
        .domain=1U,.authority_clock_epoch=2U,.state_revision=3U,
        .sound_speed_mm_s=343000U};
    assert(RangingStatusWire_Encode(&empty,data,sizeof data)==RANGING_STATUS_WIRE_OK);
    assert(RangingStatusWire_Decode(data,sizeof data,&decoded)==RANGING_STATUS_WIRE_OK &&
           !(decoded.flags&RANGING_STATUS_FLAG_HAS_VALUE));
    RangingStatusMessage hold=Message();
    hold.state=RANGING_STATE_HOLD;
    hold.flags=RANGING_STATUS_FLAG_HAS_VALUE;
    hold.fresh_deadline_ns=0U;
    hold.filtered_delta_ns=-933000;
    assert(RangingStatusWire_Encode(&hold,data,sizeof data)==RANGING_STATUS_WIRE_OK);
    assert(RangingStatusWire_Decode(data,sizeof data,&decoded)==RANGING_STATUS_WIRE_OK &&
           decoded.state==RANGING_STATE_HOLD && decoded.filtered_delta_ns==-933000);
    hold.flags|=RANGING_STATUS_FLAG_FRESH;
    assert(RangingStatusWire_Encode(&hold,data,sizeof data)!=
        RANGING_STATUS_WIRE_OK);
    RangingStatusMessage identity_without_value=empty;
    identity_without_value.flags=RANGING_STATUS_FLAG_IDENTITY_VERIFIED;
    assert(RangingStatusWire_Encode(&identity_without_value,data,sizeof data)!=
        RANGING_STATUS_WIRE_OK);

    /* RAK2 golden/roundtrip；保留字、版本和长度必须校验。 */
    const RangingStatusAck ack={RANGING_STATUS_ACK_OK,0x11223344U,0x55667788U,2U,3U};
    uint8_t ack_data[RANGING_STATUS_ACK_BYTES];
    assert(RangingStatusAck_Encode(&ack,ack_data,sizeof ack_data)==RANGING_STATUS_WIRE_OK);
    assert(memcmp(ack_data,ACK_GOLDEN,sizeof ACK_GOLDEN)==0);
    RangingStatusAck decoded_ack;
    assert(RangingStatusAck_Decode(ACK_GOLDEN,sizeof ACK_GOLDEN,&decoded_ack)==
           RANGING_STATUS_WIRE_OK && decoded_ack.status==RANGING_STATUS_ACK_OK &&
           decoded_ack.measurement_id==3U);
    ack_data[6]=1U;assert(RangingStatusAck_Decode(ack_data,sizeof ack_data,&decoded_ack)!=
        RANGING_STATUS_WIRE_OK);
    ack_data[4]=1U;assert(RangingStatusAck_Decode(ack_data,sizeof ack_data,&decoded_ack)!=
        RANGING_STATUS_WIRE_OK);

    puts("PASS ranging status wire: RST2/RAK2 golden bytes, flags/deadline/state/evidence gates");
    return 0;
}
