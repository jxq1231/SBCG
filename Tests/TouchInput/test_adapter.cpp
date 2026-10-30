/**
 * @file        test_adapter.cpp
 * @brief       真实 TouchGFX 控制器到触控 BSP 的接线测试。
 * @details     在 HAL tick 与 BSP 边界提供明确结果，检验成功坐标、释放和初始化转发。
 * @note        不代替 FT5336 协议测试或实屏触控验证。
 */
#include "STM32TouchController.hpp"
#include "BSP/bsp_touch.h"
#include "main.h"
#include <cstdio>
#include <cstdlib>

/** @brief 表达式不成立时报告位置并终止，避免依赖 NDEBUG。 */
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (0)

static unsigned init_calls, sample_calls, tick_calls;
static uint32_t tick_ms = 0x12345678U;
static bool pressed = true;
static int32_t point_x = 479, point_y = 271;

/** @brief 返回设定的毫秒时刻，并统计适配器每次采样是否只查询一次。 */
extern "C" uint32_t HAL_GetTick(void)
{
    ++tick_calls;
    return tick_ms;
}

/** @brief 记录真实适配器的软件初始化请求，不接触硬件。 */
extern "C" void BSP_Touch_Init(void)
{
    ++init_calls;
}

/**
 * @brief   返回预设 BSP 边界结果，并校验真实适配器转发的 tick 和输出指针。
 * @param   now_ms 必须与 HAL_GetTick 返回值相同。
 * @param   x 必须为非空输出；每次均填入预置值，检验失败时适配器不会转发旧坐标。
 * @param   y 与 x 相同，测试拥有指向的临时输出。
 * @return  由 pressed 控制的采样成功或释放。
 */
extern "C" bool BSP_Touch_Sample(uint32_t now_ms, int32_t *x, int32_t *y)
{
    CHECK(now_ms == tick_ms && x != nullptr && y != nullptr);
    ++sample_calls;
    *x = point_x;
    *y = point_y;
    return pressed;
}

/** @brief 验证真实适配器的成功坐标、软件初始化与失败输出不变契约。 */
int main()
{
    STM32TouchController controller;
    int32_t x = -10, y = -20;
    CHECK(controller.sampleTouch(x, y));
    CHECK(x == 479 && y == 271 && sample_calls == 1U && tick_calls == 1U);
    controller.init();
    CHECK(init_calls == 1U && sample_calls == 1U);
    pressed = false;
    point_x = 111;
    point_y = 222;
    tick_ms = 0U;
    x = -10;
    y = -20;
    CHECK(!controller.sampleTouch(x, y));
    CHECK(x == -10 && y == -20 && sample_calls == 2U && tick_calls == 2U);
    pressed = true;
    CHECK(controller.sampleTouch(x, y));
    CHECK(x == 111 && y == 222 && sample_calls == 3U && tick_calls == 3U);
    std::puts("PASS: real STM32TouchController forwards init, tick and valid coordinates");
    return 0;
}
