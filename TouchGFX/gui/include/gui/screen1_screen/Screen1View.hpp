/**
 * @file Screen1View.hpp
 * @brief TouchGFX测距、击掌、波形和设置四页导航视图。
 * @details 测距绑定服务副本，其余页面明确标识尚未接通；导航只改变控件可见性。
 * @note 所有控件定容，不消费音频或改变测距；生成基类保持原样。
 */
#ifndef SCREEN1VIEW_HPP
#define SCREEN1VIEW_HPP

#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>
#include <gui/screen1_screen/Screen1Presenter.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/events/ClickEvent.hpp>
#include <touchgfx/events/DragEvent.hpp>

class Screen1View : public Screen1ViewBase
{
public:
    /** @brief 建立尚未绑定数据版本的视图。@return 构造函数无返回值。 */
    Screen1View();
    /** @brief 释放视图；文本缓冲由生成基类持有。@return 无返回值。 */
    virtual ~Screen1View() {}
    /** @brief 页面进入时建立基类并读取最新可用副本。@return 无返回值。 */
    virtual void setupScreen();
    /** @brief 页面退出时交还基类，不停止任何业务服务。@return 无返回值。 */
    virtual void tearDownScreen();
    /** @brief VSYNC前台tick时按数据版本更新变化控件。@return 无返回值。 */
    virtual void handleTickEvent();
    /** @brief 按下后在同一导航项松开才切页；取消/读取故障不触发。@param event 框架拥有的屏幕坐标事件，仅本次借用。@return 无返回值。 */
    virtual void handleClickEvent(const touchgfx::ClickEvent& event);
    /** @brief 手指拖出原按钮后取消本次选择。@param event 屏幕坐标拖动事件，仅本次借用。@return 无返回值。 */
    virtual void handleDragEvent(const touchgfx::DragEvent& event);
protected:
    enum Page : uint8_t { RANGE, CLAP, WAVE, SETUP, PAGE_COUNT };
    /** @brief 初始化派生View定容控件及压缩后的测距布局，不改变生成基类。@return 无返回值。 */
    void configureNavigation();
    /** @brief 设置逻辑页面与导航颜色。@param page 本类生成的合法页面。@return 无返回值，不改变业务服务。 */
    void showPage(Page page);
    /** @brief 重绘导航选中/按下状态。@return 无返回值，仅使导航区域变脏。 */
    void refreshNavigation();
    /** @brief 查找导航命中项。@param x 屏幕横坐标像素。@param y 屏幕纵坐标像素。@return 0..3为按钮，-1为按钮之外。 */
    static int8_t navigationAt(int16_t x, int16_t y);
    /** @brief 取得约1Hz服务文本，仅变化字段触发重绘。@return 无返回值。 */
    void refreshStatus();
    uint32_t lastGeneration;
    bool haveGeneration;
    Page activePage;
    int8_t pressedPage;
    touchgfx::Box navigationBackground;
    touchgfx::Box navigationTiles[PAGE_COUNT];
    touchgfx::TextAreaWithOneWildcard navigationLabels[PAGE_COUNT];
    touchgfx::Unicode::UnicodeChar navigationBuffers[PAGE_COUNT][8];
    touchgfx::Box pagePanel;
    touchgfx::TextAreaWithOneWildcard pageTitle;
    touchgfx::Unicode::UnicodeChar pageTitleBuffer[40];
    touchgfx::TextAreaWithOneWildcard pageLines[5];
    touchgfx::Unicode::UnicodeChar pageLineBuffers[5][64];
    touchgfx::Box settingTiles[3];
    touchgfx::TextAreaWithOneWildcard settingLabels[3];
    touchgfx::Unicode::UnicodeChar settingBuffers[3][16];
};

#endif // SCREEN1VIEW_HPP
