/**
 * @file Screen1View.cpp
 * @brief 在原测距状态页上增加四个逻辑页面和固定底部导航。
 * @details 原十三项文本继续按版本绑定；击掌/波形/设置明确标识尚未接通，点击不改变后台业务。
 * @note 仅前台定容控件；不读取PCM、消费队列或修改测距参数。触控失效时取消按键。
 */
#include <gui/screen1_screen/Screen1View.hpp>
#include "UI/touchgfx_service.h"
#include "BSP/bsp_touch.h"
#include <touchgfx/Color.hpp>
#include <texts/TextKeysAndLanguages.hpp>
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
Screen1View::Screen1View() : lastGeneration(0), haveGeneration(false),
    activePage(RANGE), pressedPage(-1)
{
    configureNavigation();
}

/** @brief 设置固定位置、现有通配符字体及前台拥有的缓冲，不申请堆内存。@return 无返回值。 */
void Screen1View::configureNavigation()
{
    const auto normal = touchgfx::Color::getColorFromRGB(23, 34, 52);
    const auto muted = touchgfx::Color::getColorFromRGB(146, 164, 184);
    const auto white = touchgfx::Color::getColorFromRGB(232, 240, 248);
    const auto accent = touchgfx::Color::getColorFromRGB(45, 212, 191);
    measurePanel.setPosition(12, 46, 216, 104);
    syncPanel.setPosition(240, 46, 228, 104);
    waiting.setVisible(false);
    precision.setVisible(false);
    footer.setVisible(false);
    networkPanel.setPosition(12, 156, 140, 71);
    tempPanel.setPosition(164, 156, 140, 71);
    audioPanel.setPosition(316, 156, 152, 71);
    networkTitle.setPosition(24, 161, 115, 18);
    networkText.setPosition(24, 185, 125, 28);
    sourceText.setPosition(176, 161, 124, 18);
    temperatureText.setPosition(176, 180, 124, 26);
    speedText.setPosition(176, 207, 124, 18);
    audioTitle.setPosition(328, 157, 126, 18);
    audioText.setPosition(328, 175, 130, 23);
    detailText.setPosition(328, 198, 130, 15);
    eventsText.setPosition(328, 213, 130, 14);

    pagePanel.setPosition(12, 46, 456, 176);
    pagePanel.setColor(normal);
    add(pagePanel);
    pageTitle.setPosition(24, 58, 432, 29);
    pageTitle.setTypedText(touchgfx::TypedText(T_T_SYNCTEXT));
    pageTitle.setWildcard(pageTitleBuffer);
    pageTitle.setColor(white);
    pageTitleBuffer[0] = 0;
    add(pageTitle);
    for (uint8_t i = 0; i < 5U; ++i)
    {
        pageLines[i].setPosition(24, static_cast<int16_t>(96 + i * 23), 432, 22);
        pageLines[i].setTypedText(touchgfx::TypedText(T_T_ROLETEXT));
        pageLines[i].setWildcard(pageLineBuffers[i]);
        pageLines[i].setColor(i == 0U ? accent : muted);
        pageLineBuffers[i][0] = 0;
        add(pageLines[i]);
    }
    const char* settingText[] = {"CALIBRATE", "ENABLE", "CLEAR"};
    for (uint8_t i = 0; i < 3U; ++i)
    {
        settingTiles[i].setPosition(static_cast<int16_t>(24 + i * 146), 177, 138, 34);
        settingTiles[i].setColor(touchgfx::Color::getColorFromRGB(35, 44, 60));
        add(settingTiles[i]);
        settingLabels[i].setPosition(static_cast<int16_t>(36 + i * 146), 183, 114, 23);
        settingLabels[i].setTypedText(touchgfx::TypedText(T_T_ROLETEXT));
        settingLabels[i].setWildcard(settingBuffers[i]);
        settingLabels[i].setColor(muted);
        settingBuffers[i][0] = 0;
        UpdateText(settingLabels[i], settingBuffers[i], settingText[i]);
        add(settingLabels[i]);
    }
    navigationBackground.setPosition(0, 228, 480, 44);
    navigationBackground.setColor(touchgfx::Color::getColorFromRGB(11, 18, 32));
    add(navigationBackground);
    const char* labels[] = {"RANGE", "CLAP", "WAVE", "SETUP"};
    for (uint8_t i = 0; i < PAGE_COUNT; ++i)
    {
        navigationTiles[i].setPosition(static_cast<int16_t>(4 + i * 120), 232, 112, 36);
        add(navigationTiles[i]);
        navigationLabels[i].setPosition(static_cast<int16_t>(16 + i * 120), 239, 92, 23);
        navigationLabels[i].setTypedText(touchgfx::TypedText(T_T_ROLETEXT));
        navigationLabels[i].setWildcard(navigationBuffers[i]);
        navigationBuffers[i][0] = 0;
        UpdateText(navigationLabels[i], navigationBuffers[i], labels[i]);
        add(navigationLabels[i]);
    }
    showPage(RANGE);
}

/** @brief 切换可见控件；业务文本在隐藏期间仍按原服务版本更新。@param page 合法页面枚举。@return 无返回值。 */
void Screen1View::showPage(Page page)
{
    activePage = page;
    pressedPage = -1;
    touchgfx::Drawable* rangeWidgets[] = {&measurePanel, &syncPanel, &networkPanel,
        &tempPanel, &audioPanel, &distanceTitle, &distanceText, &directionText,
        &qualityText, &syncTitle, &syncText, &offsetText, &networkTitle, &networkText,
        &sourceText, &temperatureText, &speedText, &audioTitle, &audioText,
        &detailText, &eventsText};
    for (auto* widget : rangeWidgets) { widget->setVisible(page == RANGE); }
    pagePanel.setVisible(page != RANGE);
    pageTitle.setVisible(page != RANGE);
    for (auto& line : pageLines) { line.setVisible(page != RANGE); }
    for (uint8_t i = 0; i < 3U; ++i)
    {
        settingTiles[i].setVisible(page == SETUP);
        settingLabels[i].setVisible(page == SETUP);
    }
    const char* title = "";
    const char* lines[5] = {"", "", "", "", ""};
    if (page == CLAP)
    {
        title = "CLAP RANGING";
        lines[0] = "NOT CONNECTED";
        lines[1] = "CLAP DISTANCE: --";
        lines[2] = "DIRECTION: --   QUALITY: --";
        lines[3] = "STANDARD RANGING CONTINUES";
        lines[4] = "NO CLAP RESULT IS GENERATED";
    }
    else if (page == WAVE)
    {
        title = "MICROPHONE WAVEFORM";
        lines[0] = "NOT CONNECTED";
        lines[1] = "LIVE PCM VIEW IS NOT ENABLED";
        lines[2] = "AUDIO CAPTURE CONTINUES";
        lines[3] = "SYNC STATUS REMAINS ON RANGE";
    }
    else if (page == SETUP)
    {
        title = "CALIBRATION SETTINGS";
        lines[0] = "CONTROLS NOT CONNECTED";
        lines[1] = "REFERENCE: 500 MM";
        lines[2] = "CALIBRATION IS NOT CHANGED HERE";
    }
    UpdateText(pageTitle, pageTitleBuffer, title);
    for (uint8_t i = 0; i < 5U; ++i)
    { UpdateText(pageLines[i], pageLineBuffers[i], lines[i]); }
    refreshNavigation();
    invalidate();
}

/** @brief 为选中、按下和普通导航项分别赋色并局部重绘。@return 无返回值。 */
void Screen1View::refreshNavigation()
{
    for (uint8_t i = 0; i < PAGE_COUNT; ++i)
    {
        const bool selected = i == activePage;
        const bool pressed = i == pressedPage;
        navigationTiles[i].setColor(selected || pressed ?
            touchgfx::Color::getColorFromRGB(27, 71, 78) :
            touchgfx::Color::getColorFromRGB(23, 34, 52));
        navigationLabels[i].setColor(selected || pressed ?
            touchgfx::Color::getColorFromRGB(45, 212, 191) :
            touchgfx::Color::getColorFromRGB(146, 164, 184));
        navigationTiles[i].invalidate();
        navigationLabels[i].invalidate();
    }
}

/** @brief 按实际矩形命中导航，间隔和屏外返回-1。@param x 横坐标像素。@param y 纵坐标像素。@return 导航索引或-1。 */
int8_t Screen1View::navigationAt(int16_t x, int16_t y)
{
    if (x < 4 || x >= 476 || y < 232 || y >= 268) { return -1; }
    const int16_t index = x / 120;
    const int16_t local = x % 120;
    return local >= 4 && local < 116 ? static_cast<int8_t>(index) : -1;
}

/** @brief 屏幕事件只驱动导航；异常读取造成的框架释放不视为确认。@param event 本次借用的全屏点击。@return 无返回值。 */
void Screen1View::handleClickEvent(const touchgfx::ClickEvent& event)
{
    if (event.getType() == touchgfx::ClickEvent::PRESSED)
    {
        pressedPage = navigationAt(event.getX(), event.getY());
        refreshNavigation();
    }
    else
    {
        BSP_TouchSnapshot touch;
        BSP_Touch_GetSnapshot(&touch);
        const int8_t selected = pressedPage;
        pressedPage = -1;
        if (event.getType() == touchgfx::ClickEvent::RELEASED && touch.sample_valid &&
            selected >= 0 && selected == navigationAt(event.getX(), event.getY()))
        { showPage(static_cast<Page>(selected)); }
        else { refreshNavigation(); }
    }
}

/** @brief 拖离原按钮后锁定取消，重新拖回也不恢复。@param event 当前全屏拖动。@return 无返回值。 */
void Screen1View::handleDragEvent(const touchgfx::DragEvent& event)
{
    if (pressedPage >= 0 && pressedPage != navigationAt(event.getNewX(), event.getNewY()))
    { pressedPage = -1; refreshNavigation(); }
}

/** @brief 进入状态页并尝试装载服务副本；无返回值，前台调用。 */
void Screen1View::setupScreen()
{
    Screen1ViewBase::setupScreen();
    pressedPage = -1;
    haveGeneration = false;
    refreshStatus();
}

/** @brief 退出时保留业务运行，仅执行基类页面清理；无返回值。 */
void Screen1View::tearDownScreen()
{
    Screen1ViewBase::tearDownScreen();
    pressedPage = -1;
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
