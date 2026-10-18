/* Minimal HAL boundary for native tests of real BSP/component code. */
#ifndef SBCG_TEST_HAL_H
#define SBCG_TEST_HAL_H

#include <stdint.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t MACMIIAR; } ETH_TypeDef;
typedef struct { ETH_TypeDef *Instance; uint32_t gState; } ETH_HandleTypeDef;
#define HAL_ETH_STATE_READY 0x10U
#define ETH_MACMIIAR_MB 1U
#define READ_BIT(reg, bit) ((reg) & (bit))

uint32_t HAL_GetTick(void);
void HAL_ETH_SetMDIOClockRange(ETH_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_ETH_ReadPHYRegister(ETH_HandleTypeDef *handle,
    uint32_t address, uint32_t reg, uint32_t *value);
HAL_StatusTypeDef HAL_ETH_WritePHYRegister(ETH_HandleTypeDef *handle,
    uint32_t address, uint32_t reg, uint32_t value);

#endif
