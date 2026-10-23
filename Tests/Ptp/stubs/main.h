#ifndef SBCG_TEST_PTP_MAIN_H
#define SBCG_TEST_PTP_MAIN_H

/* Real vendor declarations; only the peripheral memory is replaced. */
#include "stm32f7xx_hal.h"
extern ETH_TypeDef test_eth;
extern RCC_TypeDef test_rcc;
#undef ETH
#define ETH (&test_eth)
#undef RCC
#define RCC (&test_rcc)

#ifdef SBCG_TEST_PTP_FINE
void PtpFine_TestSet(volatile uint32_t *reg, uint32_t bits);
void PtpFine_TestWrite(volatile uint32_t *reg, uint32_t value);
#undef SET_BIT
#define SET_BIT(REG, BITS) PtpFine_TestSet(&(REG), (BITS))
#undef WRITE_REG
#define WRITE_REG(REG, VALUE) PtpFine_TestWrite(&(REG), (VALUE))
#endif

#ifdef SBCG_TEST_PTP_TX
void PtpTx_TestBarrier(void);
void PtpTx_TestClear(volatile uint32_t *reg, uint32_t bits);
#undef __DMB
#define __DMB() PtpTx_TestBarrier()
#undef CLEAR_BIT
#define CLEAR_BIT(REG, BITS) PtpTx_TestClear(&(REG), (BITS))
#endif

#endif
