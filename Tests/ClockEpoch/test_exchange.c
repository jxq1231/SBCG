/**
 * @file        test_exchange.c
 * @brief       验证校时失效通知不让旧时间域的数据继续成为有效结果。
 * @details     复用已有主机桩和真实服务，先执行原回归，再观察新的公开失效接口行为。
 * @note        仅主机测试，不连接板卡或改动外设。
 */
#include <assert.h>
#define main previous_test_main
#include "../TimeExchange/test_udp_time_exchange.c"
#undef main
/**
 * @brief   执行原回归和跨时钟边界的可观察行为断言。
 * @return  全部成立返回0；失败由原断言输出并退出。
 * @note    测试只用CPU副本，不触碰实物。
 */
int main(void)
{
    assert(previous_test_main()==0);
    uint8_t req[36];Reset(0);Request(req,1,0,1);Deliver(req,0,false);
    assert(ack_sends==1 && begins==1);
    assert(UDPTimeExchange_NotifyClockChange());UDPTimeExchange_Process(0);assert(report_sends==0);
    Request(req,2,0,1);Deliver(req,0,false);UDPTimeExchange_Process(0);assert(report_sends==1);
    assert(UDPTimeExchange_Deinit() && references==0 && allocations==releases);
    puts("PASS epoch: old pending report removed without removing PCB/wrappers; fresh pair still works");
    return 0;
}
