/**
 * @file        test_ptp_clock.c
 * @brief       验证校时失效通知不让旧时间域的数据继续成为有效结果。
 * @details     复用已有主机桩和真实服务，先执行原回归，再观察新的公开失效接口行为。
 * @note        仅主机测试，不连接板卡或改动外设。
 */
#include <assert.h>
#define main previous_test_main
#include "../Ptp/test_ptp_clock.c"
#undef main
/**
 * @brief   执行原回归和跨时钟边界的可观察行为断言。
 * @return  全部成立返回0；失败由原断言输出并退出。
 * @note    测试只用CPU副本，不触碰实物。
 */
int main(void)
{
    assert(previous_test_main()==0);
    Reset(0); PtpClock_Process(1000);
    current=(BSP_PtpTime){0,5};
    PtpClock_NotifyClockChange(); PtpClock_Process(2000);
    assert(strstr(line,"STATE=LOCAL") && strstr(line,"PREV=0,ERRORS=0"));
    assert(init_calls==1);
    puts("PASS epoch: known negative step does not create a false backward error or reinit hardware");
    return 0;
}
