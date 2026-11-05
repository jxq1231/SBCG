/**
 * @file Screen1View.hpp
 * @brief TouchGFX测距、击掌、波形和设置四页导航视图。
 * @details 测距与击掌独立绑定服务副本，波形只绘CPU快照，设置页经二次确认提交标定动作。
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
#include "Calibration/calibration_control.h"
#include <touchgfx/widgets/Widget.hpp>
extern "C" {
#include "UI/waveform_service.h"
}

/** @brief 前台单副本波形控件；逐帧显示增益绘制CPU数据，不持有PCM或DMA指针。 */
class WaveformWidget : public touchgfx::Widget
{
public:
    /** @brief 清空初始快照。@return 构造无返回值。 */
    WaveformWidget();
    /** @brief 从服务取CPU副本，仅新帧使绘图区失效。@return 无返回值，仅在可见页调用。 */
    void refresh();
    /** @brief 绘制裁剪区域背景、中线及波形。@param area 控件局部脏矩形，只在调用期间使用。@return 无返回值。 */
    virtual void draw(const touchgfx::Rect& area) const;
    /** @brief 整个控件绘制不透明背景。@return 控件局部实心矩形。 */
    virtual touchgfx::Rect getSolidRect() const;
    /** @brief 借用本控件CPU副本供元数据显示。@return 引用在下一次refresh前有效。 */
    const WaveformSnapshot& snapshot() const { return frame; }
private:
    WaveformSnapshot frame;
    bool haveFrame;
};

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
    /** @brief 查找设置页已启用按钮。@param x 横坐标像素。@param y 纵坐标像素。@return 0..2或-1，不产生业务动作。 */
    int8_t settingAt(int16_t x, int16_t y) const;
    /** @brief 更新标定状态、按钮资格及确认页；只读业务副本。@return 无返回值。 */
    void refreshCalibration();
    /** @brief 处理设置页确认/取消或打开确认。@param index 本类命中的0..2按钮索引。@return 无返回值，最多提交一条请求。 */
    void selectSetting(uint8_t index);
    /** @brief 取得约1Hz服务文本，仅变化字段触发重绘。@return 无返回值。 */
    void refreshStatus();
    /** @brief 独立显示最新击掌值及新鲜/保留/等待状态，不覆盖RANGE。@return 无返回值。 */
    void refreshClap();
    /** @brief 可见时刷新波形CPU快照及共同窗状态。@return 无返回值。 */
    void refreshWaveform();
    /** @brief 刷新本地页/请求模式/生效模式的顶部两行状态。@return 无返回值。 */
    void refreshMode();
    /** @brief 当前本地页对应的生效模式是否已激活。@param page 本地页面。@return 已激活或未登记代次为true。 */
    bool pageModeActive(Page page) const;
    uint32_t lastGeneration;
    bool haveGeneration;
    Page activePage;
    int8_t pressedControl;
    CalibrationAction confirmation;
    MeasurementCalibrationSample confirmationSample;
    bool settingEnabled[3];
    bool requestRejected;
    touchgfx::Box navigationBackground;
    touchgfx::Box navigationTiles[PAGE_COUNT];
    touchgfx::TextAreaWithOneWildcard navigationLabels[PAGE_COUNT];
    touchgfx::Unicode::UnicodeChar navigationBuffers[PAGE_COUNT][8];
    touchgfx::Box pagePanel;
    WaveformWidget waveform;
    touchgfx::TextAreaWithOneWildcard pageTitle;
    touchgfx::Unicode::UnicodeChar pageTitleBuffer[40];
    touchgfx::TextAreaWithOneWildcard pageLines[5];
    touchgfx::Unicode::UnicodeChar pageLineBuffers[5][64];
    touchgfx::TextAreaWithOneWildcard groupLine;
    touchgfx::Unicode::UnicodeChar groupLineBuffer[64];
    touchgfx::Box settingTiles[3];
    touchgfx::TextAreaWithOneWildcard settingLabels[3];
    touchgfx::Unicode::UnicodeChar settingBuffers[3][16];
};

#endif // SCREEN1VIEW_HPP
