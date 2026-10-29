/**
 * @file        FrontendApplication.hpp
 * @brief       声明TouchGFX应用层及前台tick转发顺序。
 * @details     每个框架tick先调用当前空模型tick，再调用生成应用基类继续事件处理。
 * @note        NoOS服务在前台进入框架；这里不建立独立时基、线程或中断处理。
 */
#ifndef FRONTENDAPPLICATION_HPP
#define FRONTENDAPPLICATION_HPP

#include <gui_generated/common/FrontendApplicationBase.hpp>

class FrontendHeap;

using namespace touchgfx;

/** @brief 持有生成应用行为的用户扩展层，模型与存储以借用引用传入。 */
class FrontendApplication : public FrontendApplicationBase
{
public:
    /**
     * @brief 使用FrontendHeap提供的模型及存储构造应用。
     * @param m 借用的模型引用，生命周期须覆盖应用。
     * @param heap 借用的前端存储引用，生命周期须覆盖应用。
     */
    FrontendApplication(Model& m, FrontendHeap& heap);
    /** @brief 结束应用生命周期，无额外资源释放逻辑。 */
    virtual ~FrontendApplication() { }

    /**
     * @brief 在框架前台tick中先调模型，再转发至生成基类。
     * @return 无返回值；当前model.tick为空，具体页面事件由基类继续处理。
     * @note 调用频率由框架事件决定，不代表一秒业务文本更新频率。
     */
    virtual void handleTickEvent()
    {
        model.tick();
        FrontendApplicationBase::handleTickEvent();
    }
private:
};

#endif // FRONTENDAPPLICATION_HPP
