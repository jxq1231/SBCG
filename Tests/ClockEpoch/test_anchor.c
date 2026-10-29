/**
 * @file        test_anchor.c
 * @brief       验证校时失效通知不让旧时间域的数据继续成为有效结果。
 * @details     复用已有主机桩和真实服务，先执行原回归，再观察新的公开失效接口行为。
 * @note        仅主机测试，不连接板卡或改动外设。
 */
#include <assert.h>
#define main previous_test_main
#include "../Audio/test_audio_time_anchor.c"
#undef main
/**
 * @brief   执行原回归和跨时钟边界的可观察行为断言。
 * @return  全部成立返回0；失败由原断言输出并退出。
 * @note    测试只用CPU副本，不触碰实物。
 */
int main(void)
{
    assert(previous_test_main()==0);
    AudioTimeAnchor_Init(0,Log); Prepare(100,101,100);AudioTimeAnchor_Process(1000,true);
    AudioTimeAnchor_NotifyClockChange(2000);
    AudioTimeAnchorObservation sample;
    CHECK(!AudioTimeAnchor_TakeObservation(&sample));
    Prepare(200,201,1); AudioTimeAnchor_Process(3000,true);
    CHECK(strstr(line,"STATUS=OK") && strstr(line,"SEQ=2,"));
    AudioTimeAnchor_NotifyClockChange(4000);Prepare(300,301,1000);
    AudioTimeAnchor_Process(5000,true);CHECK(strstr(line,"STATUS=OK") && strstr(line,"SEQ=3,"));
    puts("PASS epoch: pending anchor erased, same audio session recovers from both directions, sequence retained");
    return 0;
}
