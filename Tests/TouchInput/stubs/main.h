/**
 * @file        main.h
 * @brief       触控主机测试使用的最小 HAL I2C 读边界。
 * @details     仅声明生产模块允许使用的同步读取、状态和 tick 接口。
 * @note        故意不提供初始化、写寄存器、GPIO、IRQ、DMA 和恢复接口。
 */
#ifndef TOUCH_TEST_MAIN_H
#define TOUCH_TEST_MAIN_H

#include <stdint.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { HAL_I2C_STATE_RESET, HAL_I2C_STATE_READY, HAL_I2C_STATE_BUSY_RX } HAL_I2C_StateTypeDef;
typedef struct { HAL_I2C_StateTypeDef state; uint32_t flags, error; } I2C_HandleTypeDef;

#define SET 1U
#define I2C_FLAG_BUSY 1U
#define I2C_MEMADD_SIZE_8BIT 1U
#define HAL_I2C_ERROR_AF 4U
#define HAL_I2C_ERROR_TIMEOUT 32U
/** @brief 仿真 HAL 的 BUSY 标志只读查询，无副作用。 */
#define __HAL_I2C_GET_FLAG(h, f) (((h)->flags & (f)) ? SET : 0U)

#ifdef __cplusplus
extern "C" {
#endif

HAL_I2C_StateTypeDef HAL_I2C_GetState(const I2C_HandleTypeDef *handle);
uint32_t HAL_I2C_GetError(const I2C_HandleTypeDef *handle);
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *handle, uint16_t address,
    uint16_t reg, uint16_t width, uint8_t *bytes, uint16_t count, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
