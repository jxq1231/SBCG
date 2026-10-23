/** @file main.h @brief SDRAM主机测试所需的最小HAL/CMSIS桩；不用于固件构建。 */
#ifndef TEST_SDRAM_MAIN_H
#define TEST_SDRAM_MAIN_H
#include <stdint.h>
typedef struct { volatile uint32_t SDCR[2],SDTR[2],SDCMR,SDRTR,SDSR; } FMC_SDRAM_TypeDef;
typedef struct { uint32_t SDBank,ColumnBitsNumber,RowBitsNumber,MemoryDataWidth,InternalBankNumber,CASLatency,WriteProtection,SDClockPeriod,ReadBurst,ReadPipeDelay; } FMC_SDRAM_InitTypeDef;
typedef struct { FMC_SDRAM_TypeDef *Instance; FMC_SDRAM_InitTypeDef Init; } SDRAM_HandleTypeDef;
typedef struct { uint32_t CommandMode,CommandTarget,AutoRefreshNumber,ModeRegisterDefinition; } FMC_SDRAM_CommandTypeDef;
typedef enum {HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT} HAL_StatusTypeDef;
typedef struct { volatile uint32_t CTRL,RNR,RBAR,RASR; } FakeMPU;
typedef struct { volatile uint32_t CCR; } FakeSCB;
typedef struct { volatile uint32_t AHB3ENR; } FakeRCC;
typedef struct { volatile uint32_t MODER,OTYPER,PUPDR,AFR[2]; } FakeGPIO;
extern FMC_SDRAM_TypeDef fake_fmc;
extern FakeMPU fake_mpu;extern FakeSCB fake_scb;extern FakeRCC fake_rcc;extern FakeGPIO fake_c;
FMC_SDRAM_TypeDef *FakeFmcAccess(void);
FakeMPU *FakeMpuAccess(void);
#define FMC_SDRAM_DEVICE (FakeFmcAccess())
#define MPU (FakeMpuAccess())
#define SCB (&fake_scb)
#define RCC (&fake_rcc)
#define GPIOC (&fake_c)
#define FMC_SDRAM_BANK1 0U
#define FMC_SDRAM_COLUMN_BITS_NUM_8 0U
#define FMC_SDRAM_ROW_BITS_NUM_12 4U
#define FMC_SDRAM_MEM_BUS_WIDTH_16 16U
#define FMC_SDRAM_INTERN_BANKS_NUM_4 64U
#define FMC_SDRAM_CAS_LATENCY_3 384U
#define FMC_SDRAM_WRITE_PROTECTION_DISABLE 0U
#define FMC_SDRAM_CLOCK_PERIOD_3 3072U
#define FMC_SDRAM_RBURST_ENABLE 4096U
#define FMC_SDRAM_RPIPE_DELAY_0 0U
#define FMC_SDRAM_CMD_CLK_ENABLE 1U
#define FMC_SDRAM_CMD_PALL 2U
#define FMC_SDRAM_CMD_AUTOREFRESH_MODE 3U
#define FMC_SDRAM_CMD_LOAD_MODE 4U
#define FMC_SDRAM_CMD_TARGET_BANK1 16U
#define FMC_SDSR_BUSY 32U
#define FMC_SDRTR_COUNT_Pos 1U
#define FMC_SDRTR_COUNT 0x3FFEU
#define FMC_SDTR1_TMRD_Pos 0U
#define FMC_SDTR1_TXSR_Pos 4U
#define FMC_SDTR1_TRAS_Pos 8U
#define FMC_SDTR1_TRC_Pos 12U
#define FMC_SDTR1_TWR_Pos 16U
#define FMC_SDTR1_TRP_Pos 20U
#define FMC_SDTR1_TRCD_Pos 24U
#define MPU_CTRL_ENABLE_Msk 1U
#define MPU_RBAR_ADDR_Msk 0xFFFFFFE0U
#define MPU_RASR_XN_Msk 0x10000000U
#define MPU_RASR_S_Msk 0x40000U
#define MPU_RASR_B_Msk 0x10000U
#define MPU_RASR_C_Msk 0x20000U
#define MPU_RASR_AP_Pos 24U
#define MPU_RASR_TEX_Pos 19U
#define MPU_RASR_SIZE_Pos 1U
#define MPU_RASR_ENABLE_Msk 1U
#define SCB_CCR_IC_Msk 0x20000U
#define SCB_CCR_DC_Msk 0x10000U
#define RCC_AHB3ENR_FMCEN 1U
#define __DSB() ((void)0)
uint32_t HAL_RCC_GetHCLKFreq(void);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
HAL_StatusTypeDef HAL_SDRAM_SendCommand(SDRAM_HandleTypeDef*,FMC_SDRAM_CommandTypeDef*,uint32_t);
HAL_StatusTypeDef HAL_SDRAM_ProgramRefreshRate(SDRAM_HandleTypeDef*,uint32_t);
#endif
