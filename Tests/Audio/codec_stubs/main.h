#ifndef CODEC_SETUP_TEST_MAIN_H
#define CODEC_SETUP_TEST_MAIN_H

#include <stdint.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { HAL_I2C_STATE_RESET, HAL_I2C_STATE_READY, HAL_I2C_STATE_BUSY_RX } HAL_I2C_StateTypeDef;
typedef struct { HAL_I2C_StateTypeDef state; uint32_t flags, error; } I2C_HandleTypeDef;
#define SET 1U
#define I2C_FLAG_BUSY 1U
#define I2C_MEMADD_SIZE_16BIT 2U
#define HAL_I2C_ERROR_NONE 0U
#define HAL_I2C_ERROR_AF 4U
#define HAL_I2C_ERROR_TIMEOUT 32U
#define __HAL_I2C_GET_FLAG(h, f) (((h)->flags & (f)) ? SET : 0U)
HAL_I2C_StateTypeDef HAL_I2C_GetState(const I2C_HandleTypeDef *handle);
uint32_t HAL_I2C_GetError(const I2C_HandleTypeDef *handle);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delay_ms);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *handle, uint16_t address,
    uint16_t reg, uint16_t width, uint8_t *bytes, uint16_t count, uint32_t timeout_ms);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *handle, uint16_t address,
    uint16_t reg, uint16_t width, uint8_t *bytes, uint16_t count, uint32_t timeout_ms);

#endif
