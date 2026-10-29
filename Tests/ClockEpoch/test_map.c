/**
 * @file        test_map.c
 * @brief       验证校时失效通知不让旧时间域的数据继续成为有效结果。
 * @details     复用已有主机桩和真实服务，先执行原回归，再观察新的公开失效接口行为。
 * @note        仅主机测试，不连接板卡或改动外设。
 */
#include <assert.h>
#define main previous_test_main
#include "../Audio/test_audio_time_map_monitor.c"
#undef main
/**
 * @brief   执行原回归和跨时钟边界的可观察行为断言。
 * @return  全部成立返回0；失败由原断言输出并退出。
 * @note    测试只用CPU副本，不触碰实物。
 */
int main(void)
{
    assert(previous_test_main()==0);
    Init(); Feed(1,1000,1,0,100);Feed(2,2000,1,48000,101);
    AudioTimeMapMonitor_NotifyClockChange();
    Feed(3,3000,1,96000,1);Feed(4,4000,1,144000,2);CHECK(lines==0);
    Feed(5,5000,1,192000,3);CHECK(lines==2 && strstr(output[0],"L=3,M=4,R=5,"));
    Feed(6,6000,1,240000,4);AudioTimeMapMonitor_NotifyClockChange();
    Feed(7,7000,1,288000,1000);Feed(8,8000,1,336000,1001);CHECK(lines==2);
    Feed(9,9000,1,384000,1002);CHECK(lines==4 && strstr(output[2],"SEQ=2,") && strstr(output[2],"L=7,M=8,R=9,"));
    puts("PASS epoch: neither forward nor backward clock change mixes source triples; result sequence retained");
    return 0;
}
