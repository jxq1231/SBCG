/**
 * @file        ModelListener.hpp
 * @brief       声明Presenter使用的模型绑定基类。
 * @details     仅保存框架提供的非持有Model指针，当前没有业务通知接口。
 * @note        绑定不创建或销毁模型，模型有效期由FrontendHeap管理。
 */
#ifndef MODELLISTENER_HPP
#define MODELLISTENER_HPP

#include <gui/model/Model.hpp>

/** @brief 保存模型借用关系的基类，当前不接收业务通知。 */
class ModelListener
{
public:
    /** @brief 构造空模型指针，不申请资源。 */
    ModelListener() : model(0) {}
    
    /** @brief 结束监听器生命周期，不销毁借用的模型。 */
    virtual ~ModelListener() {}

    /**
     * @brief 保存框架模型指针，不取得所有权。
     * @param m 可空借用指针；其有效期由调用方保证，绑定过程不解引用。
     * @return 无返回值，不触发业务处理。
     */
    void bind(Model* m)
    {
        model = m;
    }
protected:
    /** @brief 非持有的模型指针，初始化为空。 */
    Model* model;
};

#endif // MODELLISTENER_HPP
