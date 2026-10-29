/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : STM32TouchController.hpp
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
 * @file        STM32TouchController.hpp
 * @brief       声明当前无触控输入的框架控制器接口。
 * @details     提供空初始化与固定false的采样方法，不声明已启用的触摸功能。
 * @note        当前只读状态页不依赖触控芯片或I2C驱动。
 */
/* USER CODE END Header */

/* USER CODE BEGIN STM32TouchController */

#ifndef STM32TOUCHCONTROLLER_HPP
#define STM32TOUCHCONTROLLER_HPP

#include <platform/driver/touch/TouchController.hpp>

/**
 * @brief 当前未启用触控的TouchController适配类。
 * @note 保留框架接口，采样始终报告无触摸。
 */

class STM32TouchController : public touchgfx::TouchController
{
public:

    /** @brief 构造适配器，不初始化硬件或申请资源。 */
    STM32TouchController() {}

    /**
     * @brief 框架初始化入口，当前为空操作。
     * @return 无返回值，不启动触控驱动或外设。
     */
    virtual void init();

    /**
     * @brief 报告无触摸，不产生有效坐标。
     * @param x 调用方横坐标引用，本实现保持原值。
     * @param y 调用方纵坐标引用，本实现保持原值。
     * @return 固定false；不访问触控硬件。
     */
    virtual bool sampleTouch(int32_t& x, int32_t& y);
};

#endif // STM32TOUCHCONTROLLER_HPP

/* USER CODE END STM32TouchController */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
