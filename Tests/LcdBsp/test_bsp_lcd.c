/**
 * @file test_bsp_lcd.c
 * @brief 用真实BSP状态机检查显示延后启动、缓冲租约、VBR确认和致命错误。
 * @note 每个场景新进程；假地址不解引用，因此任何BSP初始化清屏都会导致测试失败。
 */
#include "BSP/bsp_lcd.h"
#include "BSP/bsp_sdram.h"
#include "main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
LTDC_TypeDef fake_ltdc;
LTDC_Layer_TypeDef fake_layer1,fake_layer2;
FakeSCB fake_scb;
FakeRCC fake_rcc;
GPIO_TypeDef fake_disp,fake_bl;
LTDC_HandleTypeDef hltdc;
static bool ready,hal_fail;
static unsigned mx_calls,set_calls,barriers,clock_queries;
static GPIO_PinState disp,bl;
static const char *scenario;
static FakeRCC clock_after_mx;
/** @brief 模拟显示所有权查询。@return 当前用例设定的READY。 */
bool BSP_SDRAM_IsDisplayReady(void){return ready;}
/** @brief 记录内存屏障。@return 无返回值。 */
void FakeDsb(void){++barriers;}
/** @brief 模拟生成初始化并按场景破坏单个配置。@return 无返回值。 */
void MX_LTDC_Init(void)
{
 ++mx_calls;assert(disp==GPIO_PIN_RESET&&bl==GPIO_PIN_RESET);
 assert(barriers>0U);
 hltdc.Instance=LTDC;hltdc.State=HAL_LTDC_STATE_READY;
 hltdc.Init=(LTDC_InitTypeDef){0,0,0,0,40,9,53,11,533,283,565,285,{0,0,0}};
 hltdc.LayerCfg[0]=(LTDC_LayerCfgTypeDef){0,480,0,272,2,255,0,0x400,5,BSP_LCD_FRAME_A,480,272,{0,0,0}};
 fake_ltdc.GCR=LTDC_GCR_LTDCEN;fake_ltdc.IER=LTDC_IT_FU|LTDC_IT_TE;
 fake_layer1=(LTDC_Layer_TypeDef){1,2,BSP_LCD_FRAME_A,(960U<<16)|963U,272};
 fake_rcc=(FakeRCC){UINT32_C(0x3F078A83),UINT32_C(0x22406419),UINT32_C(0x54003000),UINT32_C(0x00410000)};
 if(!strcmp(scenario,"clock")||!strcmp(scenario,"clock_n"))fake_rcc.PLLSAICFGR+=1U<<6U;
 if(!strcmp(scenario,"clock_r"))fake_rcc.PLLSAICFGR^=1U<<28U;
 if(!strcmp(scenario,"clock_m"))fake_rcc.PLLCFGR++;
 if(!strcmp(scenario,"clock_div"))fake_rcc.DCKCFGR1^=1U<<16U;
 if(!strcmp(scenario,"clock_sai_not_ready"))fake_rcc.CR&=~RCC_CR_PLLSAIRDY;
 if(!strcmp(scenario,"clock_main_not_ready"))fake_rcc.CR&=~RCC_CR_PLLRDY;
 if(!strcmp(scenario,"clock_hse_not_ready"))fake_rcc.CR&=~RCC_CR_HSERDY;
 if(!strcmp(scenario,"clock_hsi"))fake_rcc.PLLCFGR&=~RCC_PLLCFGR_PLLSRC;
 if(!strcmp(scenario,"clock_m_zero"))fake_rcc.PLLCFGR&=~RCC_PLLCFGR_PLLM;
 if(!strcmp(scenario,"clock_n_zero"))fake_rcc.PLLSAICFGR&=~RCC_PLLSAICFGR_PLLSAIN;
 if(!strcmp(scenario,"clock_r_zero"))fake_rcc.PLLSAICFGR&=~RCC_PLLSAICFGR_PLLSAIR;
 clock_after_mx=fake_rcc;
 if(!strcmp(scenario,"timing"))hltdc.Init.TotalWidth++;
 if(!strcmp(scenario,"blending"))hltdc.LayerCfg[0].BlendingFactor2=0;
 if(!strcmp(scenario,"alpha"))hltdc.LayerCfg[0].Alpha=0;
 if(!strcmp(scenario,"address"))fake_layer1.CFBAR=BSP_LCD_FRAME_B;
 if(!strcmp(scenario,"pitch"))fake_layer1.CFBLR++;
 if(!strcmp(scenario,"layer2"))fake_layer2.CR=1;
 if(!strcmp(scenario,"init_error"))hltdc.ErrorCode=1;
 if(!strcmp(scenario,"init_underflow"))fake_ltdc.ISR=LTDC_ISR_FUIF;
}
/** @brief 复现本地HAL不支持LTDC频率查询。@param selector 必须为LTDC。@return 固定0Hz，与本地HAL实际路径一致。@note 查询计数证明BSP不再依赖此接口。 */
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t selector){assert(selector==RCC_PERIPHCLK_LTDC);++clock_queries;return 0U;}
/** @brief 保存运行时GPIO输出。@param port 显示/背光端口。@param pin 对应引脚。@param level 输出值。@return 无返回值。 */
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState level)
{
 if(port==LCD_DISP_GPIO_Port){assert(pin==LCD_DISP_Pin);disp=level;}
 else {assert(port==LCD_BL_CTRL_GPIO_Port&&pin==LCD_BL_CTRL_Pin);bl=level;}
 if(level==GPIO_PIN_SET)assert(mx_calls==1U);
}
/** @brief 模拟仅写影子地址接口。@param h LTDC句柄。@param address 后台缓冲。@param layer 必须0。@return 注入状态。 */
HAL_StatusTypeDef HAL_LTDC_SetAddress_NoReload(LTDC_HandleTypeDef *h,uint32_t address,uint32_t layer)
{
 assert(h==&hltdc&&layer==0&&fake_ltdc.SRCR==0U);++set_calls;
 if(hal_fail)return HAL_BUSY;
 h->LayerCfg[0].FBStartAdress=address;fake_layer1.CFBAR=address;return HAL_OK;
}
/** @brief 检查致命错误不释放缓冲/不自动恢复。@return 无返回值。 */
static void check_fatal(void)
{
 BSP_LcdSnapshot s;BSP_LCD_GetSnapshot(&s);assert(s.status==BSP_LCD_ERROR);
 assert(bl==GPIO_PIN_RESET&&disp==GPIO_PIN_RESET);
 assert(BSP_LCD_BeginFrame()==NULL&&!BSP_LCD_Present(500U)&&!BSP_LCD_Init());
 BSP_LCD_CancelFrame();BSP_LCD_Process(600U);assert(BSP_LCD_BeginFrame()==NULL);
}
/** @brief 运行单一行为场景。@param argc 必须2。@param argv 场景名。@return 0通过。 */
int main(int argc,char **argv)
{
 assert(argc==2);scenario=argv[1];BSP_LcdSnapshot s;
 BSP_LCD_GetSnapshot(NULL);assert(BSP_LCD_BeginFrame()==NULL&&!BSP_LCD_Present(0));
 assert(!BSP_LCD_Init()&&mx_calls==0U);BSP_LCD_Process(0);BSP_LCD_CancelFrame();
 if(!strcmp(scenario,"not_ready")){puts("PASS LCD not_ready");return 0;}
 ready=true;
 if(!strcmp(scenario,"cache")){fake_scb.CCR=SCB_CCR_DC_Msk;assert(!BSP_LCD_Init());check_fatal();puts("PASS LCD cache");return 0;}
 assert(BSP_LCD_Init()&&BSP_LCD_Init()&&mx_calls==0U);
 uint16_t *frame=BSP_LCD_BeginFrame();assert((uintptr_t)frame==BSP_LCD_FRAME_A);
 assert(BSP_LCD_BeginFrame()==NULL);
 BSP_LCD_CancelFrame();assert(BSP_LCD_BeginFrame()==frame);
 bool ok=BSP_LCD_Present(1U);
 assert(memcmp(&clock_after_mx,&fake_rcc,sizeof fake_rcc)==0);
 if(!strncmp(scenario,"clock",5)||!strcmp(scenario,"timing")||!strcmp(scenario,"blending")||
    !strcmp(scenario,"alpha")||!strcmp(scenario,"address")||!strcmp(scenario,"pitch")||
    !strcmp(scenario,"layer2")||!strcmp(scenario,"init_error")||!strcmp(scenario,"init_underflow")){
  assert(!ok);check_fatal();printf("PASS LCD %s\n",scenario);return 0;
 }
 assert(ok&&mx_calls==1U&&disp==GPIO_PIN_SET&&bl==GPIO_PIN_SET&&fake_ltdc.IER==0U);
 assert(clock_queries==0U);
 assert(fake_rcc.CR==UINT32_C(0x3F078A83)&&fake_rcc.PLLCFGR==UINT32_C(0x22406419));
 assert(fake_rcc.PLLSAICFGR==UINT32_C(0x54003000)&&fake_rcc.DCKCFGR1==UINT32_C(0x00410000));
 BSP_LCD_GetSnapshot(&s);assert(s.status==BSP_LCD_RUNNING&&s.front_address==BSP_LCD_FRAME_A&&s.presented_frames==1U);
 assert(BSP_LCD_Init()&&mx_calls==1U&&!BSP_LCD_Present(5U));
 if(!strcmp(scenario,"ready_lost")){
  ready=false;assert(BSP_LCD_BeginFrame()==NULL);check_fatal();puts("PASS LCD ready_lost");return 0;
 }
 if(!strcmp(scenario,"cache_changed")){
  fake_scb.CCR=SCB_CCR_IC_Msk;BSP_LCD_Process(5U);check_fatal();puts("PASS LCD cache_changed");return 0;
 }
 assert((uintptr_t)BSP_LCD_BeginFrame()==BSP_LCD_FRAME_B);
 if(!strcmp(scenario,"hal_error"))hal_fail=true;
 uint32_t request_ms=!strcmp(scenario,"wrap")?UINT32_MAX-49U:10U;
 ok=BSP_LCD_Present(request_ms);
 if(hal_fail){assert(!ok);check_fatal();BSP_LCD_GetSnapshot(&s);assert(s.hal_errors==1U);puts("PASS LCD hal_error");return 0;}
 assert(ok&&set_calls==1U&&fake_ltdc.SRCR==LTDC_SRCR_VBR&&fake_ltdc.IER==0U);
 BSP_LCD_CancelFrame();assert(BSP_LCD_BeginFrame()==NULL);
 BSP_LCD_GetSnapshot(&s);assert(s.front_address==BSP_LCD_FRAME_A&&s.pending_address==BSP_LCD_FRAME_B&&s.presented_frames==1U);
 if(!strcmp(scenario,"underflow"))fake_ltdc.ISR=LTDC_ISR_FUIF;
 if(!strcmp(scenario,"transfer"))fake_ltdc.ISR=LTDC_ISR_TERRIF;
 if(!strcmp(scenario,"runtime_hal"))hltdc.ErrorCode=1U;
 BSP_LCD_Process(request_ms+99U);
 if(!strcmp(scenario,"underflow")||!strcmp(scenario,"transfer")||!strcmp(scenario,"runtime_hal")){
  check_fatal();BSP_LCD_GetSnapshot(&s);assert(s.presented_frames==1U&&s.pending_address==BSP_LCD_FRAME_B);
  assert(s.underrun_errors+s.transfer_errors+s.hal_errors==1U);printf("PASS LCD %s\n",scenario);return 0;
 }
 assert(BSP_LCD_BeginFrame()==NULL);
 if(!strcmp(scenario,"timeout")||!strcmp(scenario,"wrap")){
  BSP_LCD_Process(request_ms+100U);check_fatal();BSP_LCD_GetSnapshot(&s);
  assert(s.reload_timeouts==1U&&s.pending_address==BSP_LCD_FRAME_B);printf("PASS LCD %s\n",scenario);return 0;
 }
 fake_ltdc.SRCR=0;fake_ltdc.ISR=LTDC_ISR_RRIF;BSP_LCD_Process(request_ms+100U);
 BSP_LCD_GetSnapshot(&s);assert(s.front_address==BSP_LCD_FRAME_B&&s.pending_address==0U&&s.presented_frames==2U);
 assert((uintptr_t)BSP_LCD_BeginFrame()==BSP_LCD_FRAME_A);BSP_LCD_CancelFrame();
 printf("PASS LCD %s\n",scenario);return 0;
}
