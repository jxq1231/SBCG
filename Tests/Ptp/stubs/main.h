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

#endif
