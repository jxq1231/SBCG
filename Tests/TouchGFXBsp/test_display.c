/**
 * @file        test_display.c
 * @brief       验证真实显示BSP的清屏边界、延后LTDC与前台错误关闭。
 * @details     在主机映射固定A/B地址，检查清零顺序和未使用内存哨兵。
 * @note        仅Windows主机使用VirtualAlloc；所有HAL/RCC/LTDC均为测试寄存器副本。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "BSP/bsp_touchgfx_display.h"
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
static bool ready;
static unsigned mx_calls,barriers,clock_queries;
static GPIO_PinState disp,bl;
static const char *scenario;
static unsigned char *memory;
static FakeRCC clock_after_init;
/** @brief 提供显示内存所有权。@return 当前测试READY。 */
bool BSP_SDRAM_IsDisplayReady(void){return ready;}
/** @brief 记录内存交接屏障。@return 无返回值。 */
void FakeDsb(void){++barriers;}
/** @brief 复现HAL不支持LTDC频率查询。@param selector 查询选择器。@return 0Hz，计数证明未调用。 */
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t selector){(void)selector;++clock_queries;return 0;}
/** @brief 记录运行时显示输出。@param port 逻辑端口。@param pin 逻辑引脚。@param level 输出值。@return 无返回值。 */
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState level)
{
 if(port==LCD_DISP_GPIO_Port){assert(pin==LCD_DISP_Pin);disp=level;}
 else{assert(port==LCD_BL_CTRL_GPIO_Port&&pin==LCD_BL_CTRL_Pin);bl=level;}
 if(level==GPIO_PIN_SET)assert(mx_calls==1U);
}
/** @brief 模拟生成LTDC初始化并在入口验证两缓冲已清屏。@return 无返回值，按场景注入配置错误。 */
void MX_LTDC_Init(void)
{
 ++mx_calls;assert(disp==GPIO_PIN_RESET&&bl==GPIO_PIN_RESET&&barriers>0U);
 for(unsigned i=0;i<BSP_TOUCHGFX_FRAME_BYTES;++i)assert(memory[i]==0U&&memory[0x40000U+i]==0U);
 for(unsigned i=BSP_TOUCHGFX_FRAME_BYTES;i<0x40000U;++i)assert(memory[i]==0x5AU);
 for(unsigned i=0x40000U+BSP_TOUCHGFX_FRAME_BYTES;i<0x80000U;++i)assert(memory[i]==0x5AU);
 hltdc.Instance=LTDC;hltdc.State=HAL_LTDC_STATE_READY;
 hltdc.Init=(LTDC_InitTypeDef){0,0,0,0,40,9,53,11,533,283,565,285,{0,0,0}};
 hltdc.LayerCfg[0]=(LTDC_LayerCfgTypeDef){0,480,0,272,2,255,0,0x400,5,BSP_TOUCHGFX_FRAME_A,480,272,{0,0,0}};
 fake_ltdc.GCR=LTDC_GCR_LTDCEN;fake_ltdc.IER=LTDC_IT_FU|LTDC_IT_TE|LTDC_IT_LI;
 fake_layer1=(LTDC_Layer_TypeDef){1,2,BSP_TOUCHGFX_FRAME_A,(960U<<16)|963U,272};
 fake_rcc=(FakeRCC){UINT32_C(0x3F078A83),UINT32_C(0x22406419),UINT32_C(0x54003000),UINT32_C(0x00410000)};
 if(!strcmp(scenario,"clock"))fake_rcc.PLLSAICFGR+=1U<<6U;
 if(!strcmp(scenario,"timing"))hltdc.Init.TotalWidth++;
 if(!strcmp(scenario,"address"))fake_layer1.CFBAR=BSP_TOUCHGFX_FRAME_B;
 if(!strcmp(scenario,"pitch"))fake_layer1.CFBLR++;
 if(!strcmp(scenario,"init_error"))hltdc.ErrorCode=1U;
 if(!strcmp(scenario,"init_underflow"))fake_ltdc.ISR=LTDC_ISR_FUIF;
 clock_after_init=fake_rcc;
}
/** @brief 验证错误关闭且不能重启。@return 无返回值，不改寄存器。 */
static void Fatal(void)
{
 BSP_TouchGFXDisplaySnapshot s;BSP_TouchGFXDisplay_GetSnapshot(&s);assert(s.status==BSP_TOUCHGFX_ERROR);
 assert(bl==GPIO_PIN_RESET&&disp==GPIO_PIN_RESET&&!BSP_TouchGFXDisplay_Init());
 assert(!BSP_TouchGFXDisplay_Reveal(BSP_TOUCHGFX_FRAME_A));
 BSP_TouchGFXDisplay_Process();
}
/** @brief 执行独立BSP场景。@param argc 必须2。@param argv 非NULL场景参数。@return 0表示通过。 */
int main(int argc,char **argv)
{
 assert(argc==2);scenario=argv[1];BSP_TouchGFXDisplaySnapshot s;
 assert(!BSP_TouchGFXDisplay_Init()&&!mx_calls);BSP_TouchGFXDisplay_Process();
 if(!strcmp(scenario,"not_ready")){puts("PASS TouchGFX BSP not_ready");return 0;}
 ready=true;
 if(!strcmp(scenario,"cache")){fake_scb.CCR=SCB_CCR_DC_Msk;assert(!BSP_TouchGFXDisplay_Init()&&!mx_calls);Fatal();return 0;}
 memory=VirtualAlloc((void*)(uintptr_t)BSP_TOUCHGFX_FRAME_A,0x80000U,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 assert(memory==(void*)(uintptr_t)BSP_TOUCHGFX_FRAME_A);memset(memory,0x5A,0x80000U);
 bool ok=BSP_TouchGFXDisplay_Init();
 if(!strcmp(scenario,"clock")||!strcmp(scenario,"timing")||!strcmp(scenario,"address")||
    !strcmp(scenario,"pitch")||!strcmp(scenario,"init_error")||!strcmp(scenario,"init_underflow")){
  assert(!ok);Fatal();printf("PASS TouchGFX BSP %s\n",scenario);return 0;
 }
 assert(ok&&mx_calls==1U&&barriers>0U&&!clock_queries&&bl==GPIO_PIN_RESET&&disp==GPIO_PIN_RESET);
 assert(fake_ltdc.IER==(LTDC_IT_LI|LTDC_IT_FU|LTDC_IT_TE));
 assert(!memcmp(&fake_rcc,&clock_after_init,sizeof fake_rcc));
 BSP_TouchGFXDisplay_GetSnapshot(&s);assert(s.status==BSP_TOUCHGFX_READY&&!s.front_address);
 assert(BSP_TouchGFXDisplay_Init()&&mx_calls==1U);
 assert(BSP_TouchGFXDisplay_Reveal(BSP_TOUCHGFX_FRAME_B)&&bl==GPIO_PIN_SET&&disp==GPIO_PIN_SET);
 BSP_TouchGFXDisplay_GetSnapshot(&s);assert(s.status==BSP_TOUCHGFX_RUNNING&&s.front_address==BSP_TOUCHGFX_FRAME_B);
 if(!strcmp(scenario,"underflow"))fake_ltdc.ISR=LTDC_ISR_FUIF;
 if(!strcmp(scenario,"transfer"))fake_ltdc.ISR=LTDC_ISR_TERRIF;
 if(!strcmp(scenario,"hal_error"))hltdc.ErrorCode=1U; /* IRQ may already have cleared raw ISR. */
 if(!strcmp(scenario,"ready_lost"))ready=false;
 if(!strcmp(scenario,"cache_changed"))fake_scb.CCR=SCB_CCR_IC_Msk;
 if(!strcmp(scenario,"invalid"))assert(!BSP_TouchGFXDisplay_Reveal(BSP_TOUCHGFX_FRAME_A+2U));
 BSP_TouchGFXDisplay_Process();
 if(strcmp(scenario,"normal"))Fatal();
 else{BSP_TouchGFXDisplay_GetSnapshot(&s);assert(s.status==BSP_TOUCHGFX_RUNNING&&bl==GPIO_PIN_SET);}
 assert(fake_ltdc.IER==(LTDC_IT_LI|LTDC_IT_FU|LTDC_IT_TE));
 printf("PASS TouchGFX BSP %s\n",scenario);return 0;
}
