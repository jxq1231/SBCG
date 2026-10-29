/**
 * @file        test_observation.c
 * @brief       验证校时失效通知不让旧时间域的数据继续成为有效结果。
 * @details     复用已有主机桩和真实服务，先执行原回归，再观察新的公开失效接口行为。
 * @note        仅主机测试，不连接板卡或改动外设。
 */
#include <assert.h>
#define main previous_test_main
#include "../Sync/test_sync_observation.c"
#undef main
/**
 * @brief   执行原回归和跨时钟边界的可观察行为断言。
 * @return  全部成立返回0；失败由原断言输出并退出。
 * @note    测试只用CPU副本，不触碰实物。
 */
int main(void)
{
    assert(previous_test_main()==0);
    Reset(0);SyncObservationInput old=Input(1,0);assert(SyncObservation_Submit(&old));
    SyncObservation_NotifyClockChange();SyncObservation_Process(0);assert(Results()==0);
    SyncObservationInput fresh=Input(2,0);assert(SyncObservation_Submit(&fresh));SyncObservation_Process(0);
    assert(Results()==1 && strstr(Result(0),"REQ_SEQ=2,"));
    puts("PASS epoch: stale four-stamp queue discarded while logger/new submissions stay usable");
    return 0;
}
