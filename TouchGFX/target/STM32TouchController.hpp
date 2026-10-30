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
 * @brief       声明TouchGFX与FT5336前台采样适配接口。
 * @details     调用用户BSP读取共享I2C3，输出横屏480×272单点坐标。
 * @note        复用生成外设，不增加IRQ/DMA；实屏方向与触控响应另行验收。
 */
/* USER CODE END Header */

/* USER CODE BEGIN STM32TouchController */

#ifndef STM32TOUCHCONTROLLER_HPP
#define STM32TOUCHCONTROLLER_HPP

#include <platform/driver/touch/TouchController.hpp>

/**
 * @brief 通过用户BSP向框架提供单点触摸。
 * @note 失败不改坐标；View用BSP有效性标志区分释放和取消。
 */

class STM32TouchController : public touchgfx::TouchController
{
public:

    /** @brief 构造适配器，不初始化硬件或申请资源。 */
    STM32TouchController() {}

    /**
     * @brief 框架初始化入口，只初始化BSP软件状态。
     * @return 无返回值，不重配I2C或其他外设。
     */
    virtual void init();

    /**
     * @brief 返回限频单点触控状态。
     * @param x 框架横坐标输出，仅成功按下时写入像素。
     * @param y 框架纵坐标输出，仅成功按下时写入像素。
     * @return 有效按下为true，释放或故障为false；故障不改坐标。
     */
    virtual bool sampleTouch(int32_t& x, int32_t& y);
};

#endif // STM32TOUCHCONTROLLER_HPP

/* USER CODE END STM32TouchController */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
