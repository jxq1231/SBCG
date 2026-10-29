/**
 * @file test_bsp_ptp_pps.c
 * @brief 复用真实fine BSP测试，补充PPS停机失败不得初始化/步进及故障停机检查。
 * @note 主机替代寄存器，不证明GPIO波形。
 */
#include "BSP/bsp_pps.h"
#define main previous_fine_tests
#include "test_bsp_ptp_fine.c"
#undef main
static BSP_PpsStatus stop_status=BSP_PPS_OK;
static unsigned stops;
/** @brief 模拟PPS同步停止；无参数，返回注入结果并统计调用。 */
BSP_PpsStatus BSP_PPS_Stop(void) { ++stops;return stop_status; }
/** @brief 验证初始化、相位覆盖、故障路径的同步停止；无参数，成功0。 */
int main(void)
{
 assert(previous_fine_tests()==0);
 Reset();stop_status=BSP_PPS_BAD_CONFIG;
 assert(BSP_PTP_InitFine()==BSP_PTP_BAD_CONFIG && writes==0U);
 assert(BSP_PTP_Init()==BSP_PTP_BAD_CONFIG && writes==0U);
 stop_status=BSP_PPS_OK;Fine();unsigned before=stops;uint32_t old_writes=writes;
 stop_status=BSP_PPS_BAD_CONFIG;
 assert(BSP_PTP_Step(100)==BSP_PTP_BAD_CONFIG && writes==old_writes && stops==before+1U);
 stop_status=BSP_PPS_OK;before=stops;
 assert(BSP_PTP_Step(100)==BSP_PTP_OK && stops==before+1U);
 before=stops;assert(BSP_PTP_SetFrequencyPpb(100)==BSP_PTP_OK && stops==before);
 stuck_mask=ETH_PTPTSCR_TSARU;
 assert(BSP_PTP_SetFrequencyPpb(200)!=BSP_PTP_OK && stops>before && !ready);
 puts("PASS PPS clock guard: init/step stop before writes, failed stop blocks command, pure frequency preserved, failure stops");
 return 0;
}
