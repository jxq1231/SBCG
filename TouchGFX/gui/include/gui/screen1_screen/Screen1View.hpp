/**
 * @file Screen1View.hpp
 * @brief TouchGFX单页状态视图，只复制服务准备的文本。
 * @details 显示真实业务快照及未实现测距的占位，不直接查询硬件或消费音频。
 * @note setup/tick均由前台框架调用；生成控件位于Screen1ViewBase。
 */
#ifndef SCREEN1VIEW_HPP
#define SCREEN1VIEW_HPP

#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>
#include <gui/screen1_screen/Screen1Presenter.hpp>

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
protected:
    /** @brief 取得约1Hz服务文本，仅变化字段触发重绘。@return 无返回值。 */
    void refreshStatus();
    uint32_t lastGeneration;
    bool haveGeneration;
};

#endif // SCREEN1VIEW_HPP
