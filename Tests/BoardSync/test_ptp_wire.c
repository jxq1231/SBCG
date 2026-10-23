/**
 * @file test_ptp_wire.c
 * @brief 原生主机验证PTPv2编解码字节、边界和非法报文拒绝。
 * @details 使用真实纯C编解码器，无硬件或网络访问。
 * @note 测试失败通过assert终止，不能证明实物链路时序。
 */
#include "Network/ptp_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/** @brief 执行标准布局与非法输入断言。 @return 全部通过返回0，失败由assert终止。 */
int main(void)
{
    uint8_t id[10], wire[54];
    const uint8_t mac[6]={2,0,0,0,0,1};
    PtpWire_Identity(mac,id);
    const uint8_t expected[10]={2,0,0,255,254,0,0,1,0,1};
    assert(memcmp(id,expected,10)==0);
    PtpWireMessage m={0},r;
    m.type=PTP_WIRE_SYNC; m.sequence=65535; memcpy(m.source,id,10);
    assert(PtpWire_Encode(&m,wire)==44);
    assert(wire[0]==0 && wire[1]==2 && wire[2]==0 && wire[3]==44);
    assert(wire[6]==6 && wire[7]==0 && wire[32]==0 && wire[33]==254);
    assert(PtpWire_Decode(wire,44,&r)==PTP_WIRE_OK && r.sequence==65535);
    m.type=PTP_WIRE_FOLLOW_UP; m.timestamp_ns=UINT64_C(4294967295999999999);
    assert(PtpWire_Encode(&m,wire)==44 && PtpWire_Decode(wire,44,&r)==PTP_WIRE_OK);
    assert(r.timestamp_ns==m.timestamp_ns);
    m.type=PTP_WIRE_DELAY_RESP; memcpy(m.requester,id,10);
    assert(PtpWire_Encode(&m,wire)==54 && PtpWire_Decode(wire,54,&r)==PTP_WIRE_OK);
    assert(memcmp(r.requester,id,10)==0);
    const unsigned int bad[]={0,1,2,3,4,5,6,7,16,19,28,29,32,33,34};
    for(unsigned int i=0;i<sizeof(bad)/sizeof(bad[0]);++i)
    {
        assert(PtpWire_Encode(&m,wire)==54); wire[bad[i]]^=1;
        assert(PtpWire_Decode(wire,54,&r)!=PTP_WIRE_OK);
    }
    assert(PtpWire_Encode(&m,wire)==54); wire[8]=1;
    assert(PtpWire_Decode(wire,54,&r)==PTP_WIRE_CORRECTION);
    assert(PtpWire_Decode(wire,53,&r)==PTP_WIRE_INVALID);
    m.type=PTP_WIRE_DELAY_REQ; m.timestamp_ns=0;
    assert(PtpWire_Encode(&m,wire)==44 && PtpWire_Decode(wire,44,&r)==PTP_WIRE_OK);
    wire[43]=1; assert(PtpWire_Decode(wire,44,&r)==PTP_WIRE_INVALID);
    puts("PASS wire: standard PTPv2 EUI64/header/body, strict bounds/flags/domain/correction");
    return 0;
}
