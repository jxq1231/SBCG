#ifndef TEMPERATURE_NATIVE_MAIN_H
#define TEMPERATURE_NATIVE_MAIN_H
#include <stdint.h>
typedef struct { uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR; } GPIO_TypeDef;
typedef struct { uint32_t CTRL, CYCCNT, LAR; } NativeDwt;
typedef struct { uint32_t DEMCR; } NativeCoreDebug;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
extern GPIO_TypeDef native_gpio;
extern NativeDwt native_dwt;
extern NativeCoreDebug native_debug;
extern uint32_t SystemCoreClock;
#define DHT11_DATA_Pin (1U<<7U)
#define DHT11_DATA_GPIO_Port (&native_gpio)
#define DWT (&native_dwt)
#define CoreDebug (&native_debug)
#define DWT_CTRL_NOCYCCNT_Msk (1U<<25U)
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define CoreDebug_DEMCR_TRCENA_Msk (1U<<24U)
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
#endif
