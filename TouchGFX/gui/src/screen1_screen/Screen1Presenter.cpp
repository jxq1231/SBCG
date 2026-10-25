/**
 * @file        Screen1Presenter.cpp
 * @brief       实现状态页Presenter的视图绑定和空激活/退出入口。
 * @details     保存借用的Screen1View引用；activate/deactivate当前不执行任何业务。
 * @note        状态文字由View读取UI服务准备的副本，Presenter不读取PCM或业务队列。
 */
#include <gui/screen1_screen/Screen1View.hpp>
#include <gui/screen1_screen/Screen1Presenter.hpp>

/** @brief 绑定页面视图，不取得所有权。@param v 有效视图引用，须覆盖Presenter使用期。 */
Screen1Presenter::Screen1Presenter(Screen1View& v)
    : view(v)
{

}

/** @brief 保留页面激活回调，当前为空操作。@return 无返回值，不启动业务或硬件。 */
void Screen1Presenter::activate()
{

}

/** @brief 保留页面退出回调，当前为空操作。@return 无返回值，不释放业务或硬件资源。 */
void Screen1Presenter::deactivate()
{

}
