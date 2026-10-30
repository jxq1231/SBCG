/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : STM32TouchController.cpp
  ******************************************************************************
  * This file was created by TouchGFX Generator 4.26.1. This file is only
  * generated once! Delete this file from your project and re-generate code
  * using STM32CubeMX or change this file manually to update it.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/**
 * @file        STM32TouchController.cpp
 * @brief       将限频FT5336单点状态交给TouchGFX。
 * @details     BSP复用已生成I2C3，只在前台采样；坐标已经由BSP转换成480×272横屏像素。
 * @note        init只清软件状态，不配置硬件。错误释放由View读取sample_valid后取消手势。
 */
/* USER CODE END Header */

/* USER CODE BEGIN STM32TouchController */

#include <STM32TouchController.hpp>
#include "BSP/bsp_touch.h"
#include "main.h"

/** @brief 初始化触控软件缓存，沿用已经生成的共享I2C3。@return 无返回值，不操作外设配置。 */
void STM32TouchController::init()
{
    BSP_Touch_Init();
}

/**
 * @brief 向框架返回BSP的有效单点按下；失败不写调用者坐标。
 * @param x 框架拥有的横坐标输出，仅成功时写入0..479像素。
 * @param y 框架拥有的纵坐标输出，仅成功时写入0..271像素。
 * @return 有效按下为true，正常释放或无效输入为false。
 * @note 仅在DHT门控允许的TouchGFX前台调用；BSP限制I2C读取频率和单次事务。
 */
bool STM32TouchController::sampleTouch(int32_t& x, int32_t& y)
{
    int32_t sample_x, sample_y;
    if (!BSP_Touch_Sample(HAL_GetTick(), &sample_x, &sample_y)) { return false; }
    x = sample_x;
    y = sample_y;
    return true;
}

/* USER CODE END STM32TouchController */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
