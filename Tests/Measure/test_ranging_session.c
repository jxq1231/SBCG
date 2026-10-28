/**
 * @file        test_ranging_session.c
 * @brief       RSC2真实纯模块的主机回归与确定性重启反例。
 * @details     覆盖字节布局、三次握手、重试退避、双侧独立重启、共同读时失效与代际耗尽。
 * @note        只验证软件状态，不访问网络或硬件，不证明永久启动唯一性。
 */
#include "Network/ranging_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1); } } while(0)
#define NS UINT64_C(10000000000)

/** @brief 输入域77/时钟代际1的真实读时替身。 @param s 非NULL实例。 @param ms 本机毫秒。 @param ns 共同纳秒。 @return 无返回值。 */
static void Healthy(RangingSession *s,uint32_t ms,uint64_t ns)
{ RangingSession_Update(s,true,77U,1U,true,ns,ms); }

/** @brief 读取公开资格副本。 @param s 非NULL实例。 @return 当前快照。 */
static RangingSessionSnapshot Snapshot(RangingSession *s)
{ RangingSessionSnapshot p;RangingSession_Get(s,&p);return p; }

/** @brief 经过真实模块完成三次握手。 @param a A实例。 @param b B实例。 @param ms 毫秒基准。 @param ns 纳秒基准。 @return 无返回值，失败终止测试。 */
static void Exchange(RangingSession *a,RangingSession *b,uint32_t ms,uint64_t ns)
{
    RangingSessionMessage request,offer,confirm;
    Healthy(a,ms,ns);Healthy(b,ms,ns);
    CHECK(RangingSession_TakeOutgoing(b,ms,&request));
    CHECK(request.type==RANGING_SESSION_CHALLENGE);
    CHECK(RangingSession_OnMessage(a,&request,ms));
    CHECK(!Snapshot(a).established);
    CHECK(RangingSession_TakeOutgoing(a,ms,&offer));
    CHECK(offer.type==RANGING_SESSION_OFFER);
    CHECK(RangingSession_OnMessage(b,&offer,ms));
    CHECK(Snapshot(b).established);
    CHECK(RangingSession_TakeOutgoing(b,ms,&confirm));
    CHECK(confirm.type==RANGING_SESSION_CONFIRM);
    CHECK(RangingSession_OnMessage(a,&confirm,ms));
    CHECK(Snapshot(a).established);
    CHECK(Snapshot(a).authority_epoch==Snapshot(b).authority_epoch);
    CHECK(Snapshot(a).source_floor_ns==ns);
    CHECK(Snapshot(b).source_floor_ns==ns);
}

/** @brief 验证固定字节布局及畸形字段拒绝。 @return 无返回值。 */
static void Codec(void)
{
    RangingSessionMessage m={RANGING_SESSION_OFFER,0x01020304U,0x05060708U,UINT64_C(0x1112131415161718)},out={0};
    const uint8_t golden[24]={ 'R','S','C','2',2,3,0,0,1,2,3,4,5,6,7,8,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18 };
    uint8_t bytes[24];
    CHECK(RangingSessionWire_Encode(&m,bytes,24));CHECK(memcmp(bytes,golden,24)==0);
    CHECK(RangingSessionWire_Decode(bytes,24,&out));CHECK(out.challenge_ns==m.challenge_ns);
    CHECK(out.domain==m.domain && out.authority_epoch==m.authority_epoch && out.type==m.type);
    for(unsigned i=0;i<24;i++) {
        uint8_t bad[24];memcpy(bad,bytes,24);
        if(i<8) { bad[i]^=0x80U;CHECK(!RangingSessionWire_Decode(bad,24,&out)); }
    }
    CHECK(!RangingSessionWire_Decode(bytes,23,&out));CHECK(!RangingSessionWire_Decode(NULL,24,&out));
    m.domain=0;CHECK(!RangingSessionWire_Encode(&m,bytes,24));
    m.domain=1;m.authority_epoch=0;CHECK(!RangingSessionWire_Encode(&m,bytes,24));
    m.type=RANGING_SESSION_CHALLENGE;CHECK(RangingSessionWire_Encode(&m,bytes,24));
    m.type=RANGING_SESSION_SOLICIT;m.challenge_ns=0;CHECK(RangingSessionWire_Encode(&m,bytes,24));
    CHECK(RangingSessionWire_Decode(bytes,24,&out));
}

/** @brief 验证三次握手与内容冲突拒绝不改变当前代际。 @return 无返回值。 */
static void HandshakeAndDuplicate(void)
{
    RangingSession a,b;RangingSession_Init(&a,BOARD_ROLE_A);RangingSession_Init(&b,BOARD_ROLE_B);
    Exchange(&a,&b,0,NS);
    CHECK(Snapshot(&a).authority_epoch==1U);
    CHECK(RangingSession_TakeContextChanged(&a));CHECK(!RangingSession_TakeContextChanged(&a));
    RangingSessionMessage same={RANGING_SESSION_CHALLENGE,77,0,NS};
    CHECK(RangingSession_OnMessage(&a,&same,100));
    CHECK(Snapshot(&a).authority_epoch==1U && Snapshot(&a).established);
    RangingSessionMessage conflict={RANGING_SESSION_CONFIRM,77,2,NS};
    CHECK(!RangingSession_OnMessage(&a,&conflict,100));
    conflict.authority_epoch=1;conflict.challenge_ns++;
    CHECK(!RangingSession_OnMessage(&a,&conflict,100));
    CHECK(Snapshot(&a).authority_epoch==1U);
}

/** @brief 验证发送失败仍退避、重复挑战不续命与超时拒绝。 @return 无返回值。 */
static void RetryAndTimeout(void)
{
    RangingSession a,b;RangingSessionMessage m,offer;
    RangingSession_Init(&a,BOARD_ROLE_A);RangingSession_Init(&b,BOARD_ROLE_B);
    Healthy(&a,0,NS);Healthy(&b,0,NS);
    CHECK(RangingSession_TakeOutgoing(&a,0,&m));CHECK(m.type==RANGING_SESSION_SOLICIT);
    CHECK(!RangingSession_TakeOutgoing(&a,499,&m));CHECK(RangingSession_TakeOutgoing(&a,500,&m));
    CHECK(RangingSession_TakeOutgoing(&b,0,&m));
    CHECK(!RangingSession_TakeOutgoing(&b,499,&m));CHECK(RangingSession_TakeOutgoing(&b,500,&m));
    CHECK(RangingSession_OnMessage(&a,&m,500));
    CHECK(RangingSession_TakeOutgoing(&a,500,&offer));
    CHECK(!RangingSession_TakeOutgoing(&a,999,&offer));
    CHECK(RangingSession_OnMessage(&a,&m,999));
    CHECK(Snapshot(&a).authority_epoch==1);
    RangingSession_Tick(&a,2000);CHECK(!Snapshot(&a).established);
    CHECK(RangingSession_TakeOutgoing(&a,2000,&offer));CHECK(offer.type==RANGING_SESSION_SOLICIT);
    CHECK(!RangingSession_OnMessage(&b,&(RangingSessionMessage){RANGING_SESSION_OFFER,77,1,NS+1},1000));
    CHECK(!RangingSession_OnMessage(&b,&(RangingSessionMessage){RANGING_SESSION_OFFER,77,1,NS},1500));
    Healthy(&b,1501,NS+UINT64_C(1501000000));
    CHECK(RangingSession_TakeOutgoing(&b,1501,&m));CHECK(m.challenge_ns!=NS);
}

/** @brief 验证A/B分别重启及同TOKEN恢复仍严格提升代际。 @return 无返回值。 */
static void RebootsAndDomain(void)
{
    RangingSession a,b;RangingSessionMessage m;
    RangingSession_Init(&a,BOARD_ROLE_A);RangingSession_Init(&b,BOARD_ROLE_B);Exchange(&a,&b,0,NS);
    RangingSession_Init(&a,BOARD_ROLE_A);Healthy(&a,2000,NS+UINT64_C(2000000000));
    Healthy(&b,2000,NS+UINT64_C(2000000000));RangingSession_Tick(&b,2000);
    CHECK(RangingSession_TakeOutgoing(&a,2000,&m));CHECK(m.type==RANGING_SESSION_SOLICIT);
    CHECK(RangingSession_OnMessage(&b,&m,2000));CHECK(!Snapshot(&b).established);
    Exchange(&a,&b,2000,NS+UINT64_C(2000000000));CHECK(Snapshot(&a).authority_epoch==2);
    RangingSession_Init(&b,BOARD_ROLE_B);
    Exchange(&a,&b,4000,NS+UINT64_C(4000000000));CHECK(Snapshot(&a).authority_epoch==3);
    RangingSession_Update(&a,false,0,1,true,NS+UINT64_C(5000000000),5000);
    RangingSession_Update(&b,false,0,1,true,NS+UINT64_C(5000000000),5000);
    CHECK(!Snapshot(&a).established && !Snapshot(&b).established);
    Exchange(&a,&b,6000,NS+UINT64_C(6000000000));CHECK(Snapshot(&a).authority_epoch==4);
    CHECK(!RangingSession_OnMessage(&a,&(RangingSessionMessage){RANGING_SESSION_CONFIRM,77,3,NS+UINT64_C(4000000000)},6000));
}

/** @brief 验证读时硬失效、共同时间倒退、时钟代际与计数耗尽。 @return 无返回值。 */
static void ClockAndExhaustion(void)
{
    RangingSession a,b;RangingSessionMessage m;
    RangingSession_Init(&a,BOARD_ROLE_A);RangingSession_Init(&b,BOARD_ROLE_B);Exchange(&a,&b,0,NS);
    RangingSession_Update(&a,true,77,1,false,0,10);CHECK(!Snapshot(&a).established);
    Healthy(&a,20,NS+20);CHECK(RangingSession_TakeOutgoing(&a,20,&m));
    Healthy(&b,20,NS-1);CHECK(!Snapshot(&b).established);
    Healthy(&b,30,NS+30);
    RangingSession_Update(&b,true,77,2,true,NS+40,40);CHECK(!Snapshot(&b).established);
    RangingSession_Update(&b,true,77,2,true,0,50);CHECK(!Snapshot(&b).established);
    RangingSession_Init(&a,BOARD_ROLE_A);Healthy(&a,0,NS);
    m=(RangingSessionMessage){RANGING_SESSION_CHALLENGE,77,UINT32_MAX,NS};
    CHECK(!RangingSession_OnMessage(&a,&m,0));CHECK(Snapshot(&a).exhausted);
    CHECK(!RangingSession_TakeOutgoing(&a,10000,&m));
    RangingSession_Init(&a,BOARD_ROLE_A);Healthy(&a,UINT32_MAX-100U,NS);
    CHECK(RangingSession_TakeOutgoing(&a,UINT32_MAX-100U,&m));
    CHECK(!RangingSession_TakeOutgoing(&a,398,&m));CHECK(RangingSession_TakeOutgoing(&a,399,&m));
}

/** @brief 执行本文件全部纯模块断言。 @return 成功0，断言失败直接退出1。 */
int main(void)
{
    Codec();HandshakeAndDuplicate();RetryAndTimeout();RebootsAndDomain();ClockAndExhaustion();
    puts("PASS RSC2: codec, 3-way fencing, restart epochs, expiry, failure throttle, clock faults");
    return 0;
}
