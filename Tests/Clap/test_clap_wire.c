/** @file test_clap_wire.c @brief 击掌结果与ACK的大端布局、保留字和非法字段检查。 */
#include "Network/clap_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/** @brief 覆盖固定80/24字节协议与失败输出不变。 */
int main(void)
{
    ClapResultMessage message={.domain=0x01020304U,.authority_epoch=7U,.id=8U,
        .a_session=1U,.a_sequence=2U,.b_session=3U,.b_sequence=4U,
        .source_common_ns=UINT64_C(0x0102030405060708),.delta_ns=-1000000,
        .distance_mm=355,.sound_speed_mm_s=343000U,.temperature_version=5U,
        .temperature_milli_c=20000,.calibration_version=6U,.calibration_offset_mm=12,
        .direction=2U,.quality=90U};
    uint8_t data[CLAP_RESULT_WIRE_BYTES],before[CLAP_RESULT_WIRE_BYTES];
    ClapResultMessage out={0};
    assert(ClapWire_EncodeResult(&message,data,sizeof data));
    assert(memcmp(data,"CLP1",4U)==0 && data[4]==1U && data[5]==0U);
    assert(data[8]==1U && data[9]==2U && data[10]==3U && data[11]==4U);
    assert(data[36]==1U && data[43]==8U && data[44]==0xFFU);
    assert(ClapWire_DecodeResult(data,sizeof data,&out));
    assert(out.delta_ns==-1000000 && out.distance_mm==355 && out.calibration_offset_mm==12);
    assert(out.source_common_ns==message.source_common_ns && out.b_sequence==4U);
    memcpy(before,data,sizeof data);data[79]=1U;
    out.id=99U;assert(!ClapWire_DecodeResult(data,sizeof data,&out) && out.id==99U);
    memcpy(data,before,sizeof data);data[4]=2U;
    assert(!ClapWire_DecodeResult(data,sizeof data,&out));
    memcpy(data,before,sizeof data);data[5]=1U;
    assert(!ClapWire_DecodeResult(data,sizeof data,&out));
    memcpy(data,before,sizeof data);data[0]='X';
    assert(!ClapWire_DecodeResult(data,sizeof data,&out));
    assert(!ClapWire_DecodeResult(before,79U,&out));
    memcpy(data,before,sizeof data);message.quality=101U;
    assert(!ClapWire_EncodeResult(&message,data,sizeof data) && memcmp(data,before,sizeof data)==0);
    message.quality=90U;message.direction=1U; /* 负TDOA必须B方向。 */
    assert(!ClapWire_EncodeResult(&message,data,sizeof data));
    message.direction=2U;message.a_session=0U;
    assert(!ClapWire_EncodeResult(&message,data,sizeof data));
    message.a_session=1U;message.distance_mm=10001;
    assert(!ClapWire_EncodeResult(&message,data,sizeof data));
    message.distance_mm=355;message.calibration_offset_mm=-12;
    assert(ClapWire_EncodeResult(&message,data,sizeof data));
    assert(ClapWire_DecodeResult(data,sizeof data,&out) && out.calibration_offset_mm==-12);
    ClapAckMessage ack={9U,7U,8U,UINT64_C(0x0102030405060708)},ack_out={0};
    uint8_t ack_data[CLAP_ACK_WIRE_BYTES];
    assert(ClapWire_EncodeAck(&ack,ack_data,sizeof ack_data));
    assert(memcmp(ack_data,"CLA1",4U)==0 && ack_data[16]==1U && ack_data[23]==8U);
    assert(ClapWire_DecodeAck(ack_data,sizeof ack_data,&ack_out));
    assert(ack_out.domain==9U && ack_out.authority_epoch==7U && ack_out.id==8U && ack_out.source_common_ns==ack.source_common_ns);
    assert(!ClapWire_DecodeAck(ack_data,23U,&ack_out));
    ack_data[0]='X';assert(!ClapWire_DecodeAck(ack_data,sizeof ack_data,&ack_out));
    ack.id=0U;assert(!ClapWire_EncodeAck(&ack,ack_data,sizeof ack_data));
    puts("PASS CLAP wire: big endian CLP1/CLA1, fixed sizes, fields, reserved, signed values");
    return 0;
}
