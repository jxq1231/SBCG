#ifndef RESET_STUB_MAIN_H
#define RESET_STUB_MAIN_H
#include <stdint.h>
typedef struct { uint32_t CSR; } TestRcc;
extern TestRcc test_rcc;
extern unsigned clear_calls;
extern int clear_fails;
#define RCC (&test_rcc)
#define RCC_CSR_RMVF (UINT32_C(1)<<24)
#define RCC_CSR_BORRSTF (UINT32_C(1)<<25)
#define RCC_CSR_PINRSTF (UINT32_C(1)<<26)
#define RCC_CSR_PORRSTF (UINT32_C(1)<<27)
#define RCC_CSR_SFTRSTF (UINT32_C(1)<<28)
#define RCC_CSR_IWDGRSTF (UINT32_C(1)<<29)
#define RCC_CSR_WWDGRSTF (UINT32_C(1)<<30)
#define RCC_CSR_LPWRRSTF (UINT32_C(1)<<31)
/** @brief 模拟硬件RMVF清除高位复位标志，保留LSI等低位；无返回值。 */
static inline void TestClear(void)
{++clear_calls;if(!clear_fails)RCC->CSR&=~UINT32_C(0xff000000);}
#define __HAL_RCC_CLEAR_RESET_FLAGS() TestClear()
#endif
