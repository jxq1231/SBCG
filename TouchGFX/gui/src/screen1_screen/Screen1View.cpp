/**
 * @file Screen1View.cpp
 * @brief 将共享状态文本绑定到Designer生成的十三个动态文本控件。
 * @details 先检查副本版本，再比较Unicode缓冲，只使变化控件失效。
 * @note 不读取PCM/候选队列，不在IRQ绘图，不产生模拟测距结果。
 */
#include <gui/screen1_screen/Screen1View.hpp>
#include "UI/touchgfx_service.h"
#include <cstring>

/**
 * @brief 有界转换ASCII服务文本，并在内容变化时通知控件重绘。
 * @param widget 前台拥有的单通配符控件，生命周期覆盖此调用。
 * @param buffer 生成基类拥有的非空Unicode数组，长度N包含终止符。
 * @param text 非NULL、NUL结尾的服务副本文本，不保留指针。
 * @return 无返回值；仅修改对应缓冲及失效区域。
 */
template <std::size_t N>
static void UpdateText(touchgfx::TextAreaWithOneWildcard& widget,
                       touchgfx::Unicode::UnicodeChar (&buffer)[N], const char* text)
{
    touchgfx::Unicode::UnicodeChar replacement[N] = {};
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(text), replacement,
                               static_cast<uint16_t>(N));
    replacement[N - 1U] = 0;
    if (std::memcmp(buffer, replacement, sizeof buffer) != 0)
    {
        std::memcpy(buffer, replacement, sizeof buffer);
        widget.invalidate();
    }
}

/** @brief 初始化本视图的数据版本门控；尚未读取业务。@return 无返回值。 */
Screen1View::Screen1View() : lastGeneration(0), haveGeneration(false)
{

}

/** @brief 进入状态页并尝试装载服务副本；无返回值，前台调用。 */
void Screen1View::setupScreen()
{
    Screen1ViewBase::setupScreen();
    haveGeneration = false;
    refreshStatus();
}

/** @brief 退出时保留业务运行，仅执行基类页面清理；无返回值。 */
void Screen1View::tearDownScreen()
{
    Screen1ViewBase::tearDownScreen();
}

/** @brief 框架前台tick后检查版本；不将VSYNC频率误当业务更新频率。@return 无返回值。 */
void Screen1View::handleTickEvent()
{
    Screen1ViewBase::handleTickEvent();
    refreshStatus();
}

/** @brief 从服务复制文本，按字段更新；副本不可用或版本相同则保持原页。@return 无返回值。 */
void Screen1View::refreshStatus()
{
    UI_StatusText text;
    uint32_t generation;
    if (!UI_TouchGFX_GetText(&text, &generation) ||
        (haveGeneration && generation == lastGeneration)) { return; }
    UpdateText(roleText, roleTextBuffer, text.role);
    UpdateText(distanceText, distanceTextBuffer, text.distance);
    UpdateText(directionText, directionTextBuffer, text.direction);
    UpdateText(qualityText, qualityTextBuffer, text.quality);
    UpdateText(syncText, syncTextBuffer, text.sync);
    UpdateText(offsetText, offsetTextBuffer, text.offset);
    UpdateText(networkText, networkTextBuffer, text.network);
    UpdateText(sourceText, sourceTextBuffer, text.temperature_source);
    UpdateText(temperatureText, temperatureTextBuffer, text.temperature);
    UpdateText(speedText, speedTextBuffer, text.speed);
    UpdateText(audioText, audioTextBuffer, text.audio);
    UpdateText(detailText, detailTextBuffer, text.audio_detail);
    UpdateText(eventsText, eventsTextBuffer, text.counts);
    lastGeneration = generation;
    haveGeneration = true;
}
