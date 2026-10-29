/**
 * @file test_ptp_role.c
 * @brief 验证B板健康fine时钟重装不归零，故障不被掩盖；A仍使用原初始化。
 * @note 主机桩测试，不代表寄存器或实物验收。
 */
#define main previous_test_main
#include "../Ptp/test_ptp_clock.c"
#undef main
#include "BSP/bsp_board_identity.h"
static BoardIdentityProfile profile;
static unsigned fine_calls;
static BSP_PtpStatus fine_result;
/** @brief 返回测试静态身份。 @return profile只读指针。 */
const BoardIdentityProfile *BSP_BoardIdentity_Get(void) { return &profile; }
/** @brief 模拟fine初始化，不写硬件。 @return 测试指定状态。 */
BSP_PtpStatus BSP_PTP_InitFine(void) { ++fine_calls; return fine_result; }
/** @brief 核对角色初始化及重装边界。 @return 成功为0。 */
int main(void)
{
 profile.role=BOARD_ROLE_A; assert(previous_test_main()==0); assert(fine_calls==0);
 profile.role=BOARD_ROLE_B; fine_result=BSP_PTP_OK; Reset(0);
 assert(init_calls==0 && fine_calls==1); PtpClock_Process(1000); assert(strstr(line,"STATE=LOCAL"));
 fine_result=BSP_PTP_NOT_READY; PtpClock_Init(0,Log); PtpClock_Process(1000);
 assert(fine_calls==2 && init_calls==0 && strstr(line,"STATE=LOCAL"));
 read_result=BSP_PTP_BAD_CONFIG; PtpClock_Init(0,Log); PtpClock_Process(1000);
 assert(strstr(line,"STATE=ERROR") && strstr(line,"BAD_CONFIG"));
 fine_result=BSP_PTP_UPDATE_TIMEOUT; read_result=BSP_PTP_OK;
 PtpClock_Init(0,Log); PtpClock_Process(1000); assert(strstr(line,"UPDATE_TIMEOUT"));
 puts("PASS role: B fine startup, healthy reuse without coarse reset, invalid clock and command failure visible"); return 0;
}
