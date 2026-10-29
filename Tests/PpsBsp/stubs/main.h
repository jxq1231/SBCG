#ifndef SBCG_TEST_PPS_MAIN_H
#define SBCG_TEST_PPS_MAIN_H
/* Keep all STM32 types and masks vendor-owned; replace peripheral memory/IO only. */
#include "stm32f7xx_hal.h"
extern ETH_TypeDef test_eth;
extern RCC_TypeDef test_rcc;
extern TIM_TypeDef test_tim;
extern GPIO_TypeDef test_gpio;
#undef ETH
#define ETH (&test_eth)
#undef RCC
#define RCC (&test_rcc)
#undef TIM2
#define TIM2 (&test_tim)
#undef GPIOA
#define GPIOA (&test_gpio)
#define PPS_OUT_Pin GPIO_PIN_15
#define PPS_OUT_GPIO_Port GPIOA
uint32_t PpsTest_Read(volatile uint32_t *reg);
void PpsTest_Write(volatile uint32_t *reg, uint32_t value);
#undef READ_REG
#define READ_REG(REG) PpsTest_Read(&(REG))
#undef WRITE_REG
#define WRITE_REG(REG, VALUE) PpsTest_Write(&(REG), (VALUE))
#undef SET_BIT
#define SET_BIT(REG, BITS) PpsTest_Write(&(REG), PpsTest_Read(&(REG)) | (BITS))
#undef CLEAR_BIT
#define CLEAR_BIT(REG, BITS) PpsTest_Write(&(REG), PpsTest_Read(&(REG)) & ~(BITS))
#undef MODIFY_REG
#define MODIFY_REG(REG, CLEAR, SET) PpsTest_Write(&(REG), (PpsTest_Read(&(REG)) & ~(CLEAR)) | (SET))
#endif
