/**
 * @file        TouchController.hpp
 * @brief       TouchGFX 控制器适配测试所需的框架接口边界。
 * @details     仅保留实际适配器重写的初始化与采样签名，不模拟触控行为。
 * @note        测试编译真实 STM32TouchController.cpp；FT5336 解析由独立 BSP 用例验证。
 */
#ifndef TOUCH_TEST_CONTROLLER_HPP
#define TOUCH_TEST_CONTROLLER_HPP

#include <cstdint>

namespace touchgfx
{
class TouchController
{
public:
    /** @brief 允许以框架基类销毁测试中的适配对象。 */
    virtual ~TouchController() = default;
    /** @brief 由具体适配器实现软件初始化。 */
    virtual void init() = 0;
    /** @brief 由具体适配器返回单点状态和引用坐标。 */
    virtual bool sampleTouch(int32_t& x, int32_t& y) = 0;
};
}

#endif
