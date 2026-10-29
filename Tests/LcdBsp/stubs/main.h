/** @file main.h @brief 主机LCD测试最小HAL/CMSIS桩，仅模拟寄存器与调用边界。 */
#ifndef TEST_LCD_MAIN_H
#define TEST_LCD_MAIN_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
typedef struct { uint32_t Blue, Green, Red; } LTDC_ColorTypeDef;
typedef struct {
 uint32_t HSPolarity,VSPolarity,DEPolarity,PCPolarity;
 uint32_t HorizontalSync,VerticalSync,AccumulatedHBP,AccumulatedVBP;
 uint32_t AccumulatedActiveW,AccumulatedActiveH,TotalWidth,TotalHeigh;
 LTDC_ColorTypeDef Backcolor;
} LTDC_InitTypeDef;
typedef struct {
 uint32_t WindowX0,WindowX1,WindowY0,WindowY1,PixelFormat,Alpha,Alpha0;
 uint32_t BlendingFactor1,BlendingFactor2,FBStartAdress,ImageWidth,ImageHeight;
 LTDC_ColorTypeDef Backcolor;
} LTDC_LayerCfgTypeDef;
typedef struct { volatile uint32_t GCR,SRCR,ISR,ICR,IER; } LTDC_TypeDef;
typedef struct { volatile uint32_t CR,PFCR,CFBAR,CFBLR,CFBLNR; } LTDC_Layer_TypeDef;
typedef struct { LTDC_TypeDef *Instance; LTDC_InitTypeDef Init; LTDC_LayerCfgTypeDef LayerCfg[2]; uint32_t State,ErrorCode; } LTDC_HandleTypeDef;
typedef struct { volatile uint32_t CCR; } FakeSCB;
typedef struct { volatile uint32_t CR,PLLCFGR,PLLSAICFGR,DCKCFGR1; } FakeRCC;
typedef struct { uint32_t unused; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET,GPIO_PIN_SET } GPIO_PinState;
extern LTDC_TypeDef fake_ltdc;
extern LTDC_Layer_TypeDef fake_layer1,fake_layer2;
extern FakeSCB fake_scb;
extern FakeRCC fake_rcc;
extern GPIO_TypeDef fake_disp,fake_bl;
#define LTDC (&fake_ltdc)
#define LTDC_Layer1 (&fake_layer1)
#define LTDC_Layer2 (&fake_layer2)
#define SCB (&fake_scb)
#define RCC (&fake_rcc)
#define HSE_VALUE UINT32_C(25000000)
#define RCC_CR_HSERDY UINT32_C(0x00020000)
#define RCC_CR_PLLRDY UINT32_C(0x02000000)
#define RCC_CR_PLLSAIRDY UINT32_C(0x20000000)
#define RCC_PLLCFGR_PLLSRC UINT32_C(0x00400000)
#define RCC_PLLCFGR_PLLSRC_HSE UINT32_C(0x00400000)
#define RCC_PLLCFGR_PLLM UINT32_C(0x0000003F)
#define RCC_PLLSAICFGR_PLLSAIN_Pos 6U
#define RCC_PLLSAICFGR_PLLSAIN UINT32_C(0x00007FC0)
#define RCC_PLLSAICFGR_PLLSAIR_Pos 28U
#define RCC_PLLSAICFGR_PLLSAIR UINT32_C(0x70000000)
#define RCC_DCKCFGR1_PLLSAIDIVR_Pos 16U
#define RCC_DCKCFGR1_PLLSAIDIVR UINT32_C(0x00030000)
#define LCD_DISP_GPIO_Port (&fake_disp)
#define LCD_BL_CTRL_GPIO_Port (&fake_bl)
#define LCD_DISP_Pin 4096U
#define LCD_BL_CTRL_Pin 8U
#define LTDC_HSPOLARITY_AL 0U
#define LTDC_VSPOLARITY_AL 0U
#define LTDC_DEPOLARITY_AL 0U
#define LTDC_PCPOLARITY_IPC 0U
#define LTDC_PIXEL_FORMAT_RGB565 2U
#define LTDC_BLENDING_FACTOR1_CA 0x400U
#define LTDC_BLENDING_FACTOR2_CA 5U
#define HAL_LTDC_STATE_READY 1U
#define HAL_LTDC_ERROR_NONE 0U
#define LTDC_GCR_LTDCEN 1U
#define LTDC_LxCR_LEN 1U
#define LTDC_SRCR_VBR 2U
#define LTDC_ISR_FUIF 2U
#define LTDC_ISR_TERRIF 4U
#define LTDC_ISR_RRIF 8U
#define LTDC_IT_LI 1U
#define LTDC_IT_FU 2U
#define LTDC_IT_TE 4U
#define LTDC_IT_RR 8U
#define SCB_CCR_IC_Msk 0x20000U
#define SCB_CCR_DC_Msk 0x10000U
#define RCC_PERIPHCLK_LTDC 1U
#define __HAL_LTDC_DISABLE_IT(h,it) ((h)->Instance->IER &= ~(it))
void FakeDsb(void);
#define __DSB() FakeDsb()
void MX_LTDC_Init(void);
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t);
void HAL_GPIO_WritePin(GPIO_TypeDef *,uint16_t,GPIO_PinState);
HAL_StatusTypeDef HAL_LTDC_SetAddress_NoReload(LTDC_HandleTypeDef *,uint32_t,uint32_t);
#endif
