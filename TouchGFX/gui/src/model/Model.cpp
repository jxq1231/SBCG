/**
 * @file        Model.cpp
 * @brief       实现当前TouchGFX模型的构造与空tick入口。
 * @details     构造时清空监听器指针；tick不读取业务、不发送通知，状态文本由UI服务提供。
 * @note        tick由FrontendApplication前台调度，当前没有模型侧业务处理。
 */
#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>

/** @brief 将非持有的监听器指针初始化为空。@note 由FrontendHeap构造模型，无独立业务资源。 */
Model::Model() : modelListener(0)
{

}

/** @brief 保留前台模型tick入口，当前为空操作。@return 无返回值，不查询状态或消费业务队列。 */
void Model::tick()
{

}
