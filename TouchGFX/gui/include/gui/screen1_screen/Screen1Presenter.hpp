/**
 * @file        Screen1Presenter.hpp
 * @brief       声明状态页Presenter及其视图借用关系。
 * @details     继承框架Presenter与ModelListener；当前激活/退出回调均无业务处理。
 * @note        不管理音频、网络或显示外设，状态文本绑定由Screen1View负责。
 */
#ifndef SCREEN1PRESENTER_HPP
#define SCREEN1PRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class Screen1View;

/** @brief 状态页的框架Presenter，当前仅保存视图引用。 */
class Screen1Presenter : public touchgfx::Presenter, public ModelListener
{
public:
    /** @brief 保存视图引用，不转移所有权。@param v 有效页面引用，须覆盖Presenter使用期。 */
    Screen1Presenter(Screen1View& v);

    /**
     * @brief 页面切入时由框架调用，当前为空操作。
     * @return 无返回值，不启动任何业务或硬件资源。
     */
    virtual void activate();

    /**
     * @brief 页面切出时由框架调用，当前为空操作。
     * @return 无返回值，不释放业务或硬件资源。
     */
    virtual void deactivate();

    /** @brief 析构Presenter，不销毁借用视图。 */
    virtual ~Screen1Presenter() {}

private:
    /** @brief 私有默认构造声明，无实现；实际构造须提供有效视图引用。 */
    Screen1Presenter();

    /** @brief 非持有的页面视图引用。 */
    Screen1View& view;
};

#endif // SCREEN1PRESENTER_HPP
