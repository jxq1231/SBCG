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
 * @brief       实现当前未启用触控的TouchGFX控制器占位接口。
 * @details     init为空操作，sampleTouch固定返回false且不修改坐标。
 * @note        未启动I2C或触控芯片；当前状态页只提供只读显示。
 */
/* USER CODE END Header */

/* USER CODE BEGIN STM32TouchController */

#include <STM32TouchController.hpp>

/** @brief 保留框架初始化入口，当前无触控初始化。@return 无返回值，不操作外设。 */
void STM32TouchController::init()
{
}

/**
 * @brief 向框架报告当前没有触摸输入。
 * @param x 调用方横坐标引用，本实现不读取或修改。
 * @param y 调用方纵坐标引用，本实现不读取或修改。
 * @return 固定false，坐标输出无有效触摸语义。
 * @note 由框架按采样调度调用，不访问I2C、GPIO或触控芯片。
 */
bool STM32TouchController::sampleTouch(int32_t& x, int32_t& y)
{
    return false;
}

/* USER CODE END STM32TouchController */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
