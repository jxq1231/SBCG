/**
 * @file        test_app_identity.c
 * @brief       验证身份宏开启时的A/B启动、未知身份禁用及重入保护。
 * @details     复用原应用桩并链接真实纯C身份解析；BSP缓存读取由测试选择，不触碰硬件。
 * @note        覆盖三字UID不匹配、正常服务保留和日志失败计数；不能替代双板实物验收。
 */
/* Reuse service/transport stubs; the original main remains a separate suite. */
#define main legacy_app_test_main
#include "test_app_main.c"
#undef main
#include "BSP/bsp_board_identity.h"

static BoardIdentityProfile configured_profile;
static const BoardIdentityProfile *configured;
/**
 * @brief   返回本用例选定的模拟BSP已准备配置。
 * @return  configured指针或NULL，不转移所有权。
 * @note    输入来自测试静态状态，不读MCU或真实LwIP。
 */
const BoardIdentityProfile *BSP_BoardIdentity_Get(void) { return configured; }

/**
 * @brief   清零本用例关注的各前台服务调用计数。
 * @return  无返回值（void）。
 * @note    仅改主机桩的观察计数，不初始化真实应用或外设。
 */
static void ClearCalls(void)
{
    monitor_calls=lwip_calls=udp_calls=probe_calls=ptp_calls=rx_calls=tx_calls=0;
    time_calls=observation_calls=temperature_calls=console_calls=codec_calls=0;
    audio_calls=anchor_calls=map_calls=0;
}
/**
 * @brief   核对未配置身份未启动业务，且连续三轮不访问服务。
 * @return  无返回值（void）；断言失败立即终止用例。
 * @note    依赖刚完成的Reset，推进测试tick并调用App_Process。
 */
static void CheckDisabled(void)
{
    assert(!udp_initialized && !probe_initialized && !ptp_initialized && !rx_initialized && !tx_initialized);
    assert(!time_initialized && !observation_initialized && !temperature_initialized);
    assert(console_inits==0 && rx_inits==0 && codec_inits==0 && audio_inits==0 && anchor_inits==0 && map_inits==0);
    assert(write_count==2 && strcmp(last_line,"$BOARD,ROLE=UNCONFIGURED,NET=DISABLED,SYNC=0\r\n")==0);
    ClearCalls();
    for (unsigned i=0;i<3;++i) { tick_ms+=1000;App_Process(); }
    assert(monitor_calls==0 && lwip_calls==0 && udp_calls==0 && probe_calls==0 && ptp_calls==0);
    assert(rx_calls==0 && tx_calls==0 && time_calls==0 && observation_calls==0);
    assert(temperature_calls==0 && console_calls==0 && codec_calls==0 && audio_calls==0 && anchor_calls==0 && map_calls==0);
    assert(write_count==2);
}
/**
 * @brief   执行A/B配置、未知及不匹配拒绝、重入和发送失败用例。
 * @return  全部断言通过返回0，否则由断言退出非零。
 * @note    主机测试入口，不烧录、不连接网卡或串口。
 */
int main(void)
{
    configured=NULL;Reset(0,true);CheckDisabled();
    const uint32_t ids[2][3]={{0x001A002D,0x30365105,0x36363631},{0x00380050,0x35385104,0x37313532}};
    for (unsigned role=0;role<2;++role)
    {
        memcpy(info_uid,ids[role],sizeof info_uid);
        assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&configured_profile));
        configured=&configured_profile;Reset(0,true);
        assert(write_count==2 && udp_initialized && ptp_initialized && temperature_initialized && map_inits==1);
        assert(strstr(last_line,role==0 ? "ROLE=A,IP=192.168.10.1,MAC=02:00:00:00:00:01,PEER_IP=192.168.10.2" : "ROLE=B,IP=192.168.10.2,MAC=02:00:00:00:00:02,PEER_IP=192.168.10.1"));
        assert(strstr(last_line,",SYNC=0\r\n"));
        App_Process();assert(lwip_calls==1 && map_calls==1);
    }
    /* Reinitialization must preserve wrapper order with the role gate enabled. */
    App_Init();
    assert(deinit_calls==2 && completed_init_calls==2 && observation_init_calls==2);
    assert(audio_inits==2 && map_inits==2 && time_initialized);
    const unsigned init_before=audio_inits;
    const uint32_t writes_before=write_count, loops_before=lwip_calls;
    deinit_ok=false;App_Init();
    assert(audio_inits==init_before && write_count==writes_before+1);
    assert(strcmp(last_line,"$TS_INIT,RESULT=OWNERSHIP_ERROR\r\n")==0);
    App_Process();assert(lwip_calls==loops_before+1);
    deinit_ok=true;
    /* A prepared profile must not silently authorize another MCU identity. */
    memcpy(info_uid,ids[0],sizeof info_uid);Reset(0,true);CheckDisabled();
    assert(BoardIdentity_Resolve(&BoardIdentity_DefaultMap,info_uid,&configured_profile));
    configured=&configured_profile;write_ok=false;Reset(0,false);
    write_ok=true;tick_ms=1000;App_Process();
    assert(strstr(last_line,"TX_ERRORS=2\r\n"));
    puts("PASS identity app: A/B role output, unknown/mismatch disabled, old services retained and write errors counted");
    return 0;
}
