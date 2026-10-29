/**
 * @file        Model.hpp
 * @brief       声明TouchGFX模型的监听器绑定与前台tick接口。
 * @details     当前只保存非持有的监听器指针，tick没有业务处理或通知。
 * @note        模型由FrontendHeap持有，业务快照与一秒文本更新由UI服务负责。
 */
#ifndef MODEL_HPP
#define MODEL_HPP

class ModelListener;

/** @brief 当前页面使用的轻量模型，不拥有监听器对象。 */
class Model
{
public:
    /** @brief 构造空监听器状态，无硬件初始化。 */
    Model();

    /**
     * @brief 保存框架指定的监听器，不转移其所有权。
     * @param listener 可空借用指针，使用期间由调用方保证对象有效；当前tick不解引用。
     * @return 无返回值，仅替换指针，不触发通知。
     */
    void bind(ModelListener* listener)
    {
        modelListener = listener;
    }

    /** @brief 由FrontendApplication前台调用的空模型入口。@return 无返回值，无业务副作用。 */
    void tick();
protected:
    /** @brief 框架绑定的非持有监听器，构造时为空。 */
    ModelListener* modelListener;
};

#endif // MODEL_HPP
