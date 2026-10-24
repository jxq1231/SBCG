/**
 * @file test_bsp_sdram.c
 * @brief 通过分区MPU和HAL桩检验真实BSP的启动、拒绝、限时与终态行为。
 * @note 每个命令行场景使用新进程，测试不访问实物。
 */
#include "BSP/bsp_sdram.h"
#include "main.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
FMC_SDRAM_TypeDef fake_fmc;FakeMPU fake_mpu;FakeSCB fake_scb;FakeRCC fake_rcc;FakeGPIO fake_c;
SDRAM_HandleTypeDef hsdram1;
static unsigned calls,delay_calls,init_test_calls,step_calls,fail_command,refresh_calls;
static uint32_t tick,hclk=200000000U,tick_calls;
static bool stuck_after_send,refresh_fail,bad_refresh,force_fail,force_pass;
static uint32_t mpu_base[8],mpu_attributes[8],last_region=UINT32_MAX,fmc_reads;
/** @brief 记录模拟FMC读取入口。@return 测试拥有的控制器；不访问实物。 */
FMC_SDRAM_TypeDef *FakeFmcAccess(void){++fmc_reads;return &fake_fmc;}
/** @brief 按RNR更新只读分区窗口，检查旧属性未被写入。@return 测试拥有的MPU。 */
FakeMPU *FakeMpuAccess(void)
{
 assert(fake_mpu.RNR<8U);
 if(last_region!=fake_mpu.RNR){
  if(last_region<8U){assert(fake_mpu.RBAR==mpu_base[last_region]);assert(fake_mpu.RASR==mpu_attributes[last_region]);}
  fake_mpu.RBAR=mpu_base[fake_mpu.RNR];fake_mpu.RASR=mpu_attributes[fake_mpu.RNR];last_region=fake_mpu.RNR;
 }
 return &fake_mpu;
}
/** @brief 返回模拟HCLK。@return Hz。 */
uint32_t HAL_RCC_GetHCLKFreq(void){return hclk;}
/** @brief 返回模拟tick并限制轮询总数。@return 毫秒。 */
uint32_t HAL_GetTick(void){++tick_calls;assert(tick_calls<100000U);return tick;}
/** @brief 检查唯一上电等待。@param ms 必须为1ms。@return 无返回值。 */
void HAL_Delay(uint32_t ms){assert(calls==1U&&ms==1U);tick+=ms;++delay_calls;}
/** @brief 检查命令及顺序并注入错误。@param h 模拟句柄。@param c 输入命令。@param ms 有限超时。@return 模拟HAL状态。 */
HAL_StatusTypeDef HAL_SDRAM_SendCommand(SDRAM_HandleTypeDef*h,FMC_SDRAM_CommandTypeDef*c,uint32_t ms)
{
 assert(h==&hsdram1&&ms>0U);++calls;assert(c->CommandMode==calls);
 assert(c->CommandTarget==FMC_SDRAM_CMD_TARGET_BANK1);
 assert(c->AutoRefreshNumber==(calls==3U?8U:1U));
 assert(c->ModeRegisterDefinition==(calls==4U?0x0230U:0U));
 if(calls>1U)assert(delay_calls==1U);
 if(stuck_after_send)fake_fmc.SDSR=FMC_SDSR_BUSY;
 return calls==fail_command?HAL_ERROR:HAL_OK;
}
/** @brief 检查刷新并注入写入/读回错误。@param h 模拟句柄。@param count 必须1021周期。@return 模拟HAL状态。 */
HAL_StatusTypeDef HAL_SDRAM_ProgramRefreshRate(SDRAM_HandleTypeDef*h,uint32_t count)
{
 assert(h==&hsdram1&&calls==4U&&count==1021U);++refresh_calls;
 if(refresh_fail)return HAL_ERROR;
 fake_fmc.SDRTR=(bad_refresh?count-1U:count)<<1U;return HAL_OK;
}
/** @brief 检查BSP交给核心的区域。@param t 输出状态。@param m 固定8MB起点。@param bytes 区域字节数。@return true。 */
bool SDRAM_TestInit(SDRAM_Test*t,volatile void*m,uint32_t bytes)
{assert(m==(volatile void*)(uintptr_t)BSP_SDRAM_BASE&&bytes==BSP_SDRAM_BYTES);++init_test_calls;memset(t,0,sizeof *t);t->phase=SDRAM_TEST_DATA;return true;}
/** @brief 模拟内存核心终态。@param t 可写状态。@param ms 前台毫秒。@return 无返回值。 */
void SDRAM_TestStep(SDRAM_Test*t,uint32_t ms)
{
 (void)ms;++step_calls;
 if(force_fail){t->phase=SDRAM_TEST_FAIL;t->error_offset_bytes=1234;t->expected=1;t->actual=2;}
 else if(force_pass){t->phase=SDRAM_TEST_PASS;t->verified_words=8388608U;t->pass=4U;}
}
/** @brief 建立与生成配置匹配的桩，默认选择Region2。@return 无返回值。 */
static void setup(void)
{
 hsdram1.Instance=&fake_fmc;
 hsdram1.Init=(FMC_SDRAM_InitTypeDef){0,0,4,16,64,384,0,3072,4096,0};
 fake_fmc.SDCR[0]=0x1DD4U;fake_fmc.SDTR[0]=0x01126471U;
 mpu_base[1]=0xC0000001U;mpu_attributes[1]=0x1308002DU;
 mpu_base[2]=0xA0000002U;mpu_attributes[2]=0x13050017U;
 fake_mpu.CTRL=1;fake_mpu.RNR=2;
 fake_rcc.AHB3ENR=1;fake_c.MODER=2U<<6;fake_c.AFR[0]=12U<<12;
}
/** @brief 执行一个命令行场景。@param argc 必须2。@param argv 场景名称。@return 0通过，断言失败退出。 */
int main(int argc,char**argv)
{
 assert(argc==2);setup();const char *which=argv[1];BSP_SdramSnapshot s;
 if(!strcmp(which,"clock"))hclk=216000000;
 if(!strcmp(which,"pin"))fake_c.AFR[0]=0;
 if(!strcmp(which,"timing"))fake_fmc.SDTR[0]^=1U<<24;
 if(!strcmp(which,"controller"))fake_fmc.SDCR[0]^=0x400;
 if(!strcmp(which,"mpu"))mpu_attributes[1]^=0x01000000;
 if(!strcmp(which,"mpu2_missing"))mpu_attributes[2]=0;
 if(!strcmp(which,"mpu2_base"))mpu_base[2]=0xA0001002U;
 if(!strcmp(which,"mpu2_cache"))mpu_attributes[2]^=MPU_RASR_C_Msk;
 if(!strcmp(which,"mpu_disabled"))fake_mpu.CTRL=0;
 if(!strncmp(which,"selector",8))fake_mpu.RNR=(uint32_t)atoi(which+8);
 if(!strcmp(which,"cache"))fake_scb.CCR=SCB_CCR_DC_Msk;
 if(!strcmp(which,"handle"))hsdram1.Init.CASLatency=0;
 if(!strcmp(which,"fmc_clock"))fake_rcc.AHB3ENR=0;
 if(!strcmp(which,"busy_before"))fake_fmc.SDSR=FMC_SDSR_BUSY;
 if(!strcmp(which,"busy_after"))stuck_after_send=true;
 if(!strncmp(which,"command",7))fail_command=(unsigned)atoi(which+7);
 if(!strcmp(which,"refresh"))refresh_fail=true;
 if(!strcmp(which,"readback"))bad_refresh=true;
 const uint32_t original_selector=fake_mpu.RNR;
 if(!strcmp(which,"display")||!strcmp(which,"diagnostic_then_display")){
  if(!strcmp(which,"diagnostic_then_display")){
   assert(BSP_SDRAM_Init());assert(!BSP_SDRAM_IsDisplayReady());
   assert(!BSP_SDRAM_InitDisplay());assert(calls==4U&&init_test_calls==1U);
   force_pass=true;BSP_SDRAM_Process(20U,true);
   assert(!BSP_SDRAM_InitDisplay()&&!BSP_SDRAM_IsDisplayReady());
  } else {
   assert(BSP_SDRAM_InitDisplay()&&BSP_SDRAM_IsDisplayReady());
   assert(calls==4U&&refresh_calls==1U&&init_test_calls==0U);
   assert(BSP_SDRAM_InitDisplay()&&!BSP_SDRAM_Init());
   BSP_SDRAM_Process(20U,true);BSP_SDRAM_Process(30U,true);
   BSP_SDRAM_GetSnapshot(&s);
   assert(s.status==BSP_SDRAM_READY&&step_calls==0U&&init_test_calls==0U);
   assert(calls==4U&&refresh_calls==1U&&s.verified_bytes==0U&&s.pass==0U);
  }
  printf("PASS BSP SDRAM %s\n",which);return 0;
 }
 bool ok=BSP_SDRAM_Init();BSP_SDRAM_GetSnapshot(&s);
 assert(fake_mpu.RNR==original_selector);
 (void)FakeMpuAccess(); /* Restored bank must still have its original attributes. */
 if(!strcmp(which,"ok")||!strcmp(which,"pass")||!strcmp(which,"memory")||!strncmp(which,"selector",8)) {
  assert(ok&&s.status==BSP_SDRAM_TESTING);assert(calls==4&&delay_calls==1&&refresh_calls==1&&init_test_calls==1);
  assert(s.sdclk_hz==66666666U&&s.refresh_count==1021U);
  assert(BSP_SDRAM_Init());assert(calls==4&&init_test_calls==1);
  BSP_SDRAM_Process(10,false);assert(step_calls==0);
  force_pass=!strcmp(which,"pass");force_fail=!strcmp(which,"memory");
  BSP_SDRAM_Process(20,true);assert(step_calls==1);BSP_SDRAM_GetSnapshot(&s);
  if(force_pass){assert(s.status==BSP_SDRAM_PASS&&s.verified_bytes==33554432U&&s.pass==4);}
  if(force_fail){assert(s.status==BSP_SDRAM_MEMORY_ERROR&&s.error_offset_bytes==1234&&s.expected==1&&s.actual==2);}
  if(force_fail||force_pass){BSP_SDRAM_Process(30,true);assert(step_calls==1);}
 } else {
  assert(!ok&&init_test_calls==0);BSP_SDRAM_Process(20,true);assert(step_calls==0);
  assert(!BSP_SDRAM_Init());assert(fake_mpu.RNR==original_selector);
  if(!strncmp(which,"mpu",3))assert(fmc_reads==0U);
  if(!strncmp(which,"busy",4)){assert(s.status==BSP_SDRAM_TIMEOUT&&calls==(stuck_after_send?1U:0U));}
  else if(fail_command){assert(s.status==BSP_SDRAM_COMMAND_ERROR&&calls==fail_command&&s.command_index==fail_command);}
  else if(refresh_fail||bad_refresh){assert(s.status==BSP_SDRAM_REFRESH_ERROR);}
  else {assert(s.status==BSP_SDRAM_CONFIG_ERROR&&calls==0);}
 }
 BSP_SDRAM_GetSnapshot(NULL);printf("PASS BSP SDRAM %s\n",which);return 0;
}
