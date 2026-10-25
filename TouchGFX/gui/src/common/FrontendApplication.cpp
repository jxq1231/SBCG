/**
 * @file        FrontendApplication.cpp
 * @brief       实现用户应用层构造，将模型和固定前端存储交给生成基类。
 * @details     本文件仅委托基类构造，不增加业务初始化或硬件配置。
 * @note        前台tick转发在对应头文件内实现；模型与应用由FrontendHeap持有。
 */
#include <gui/common/FrontendApplication.hpp>

/**
 * @brief 使用既有模型和前端存储构造生成应用基类。
 * @param m 借用的模型引用，由FrontendHeap持有，必须覆盖应用有效期。
 * @param heap 借用的前端存储引用，必须覆盖应用有效期；不转移所有权。
 * @note 仅执行基类构造，当前构造体无附加业务。
 */
FrontendApplication::FrontendApplication(Model& m, FrontendHeap& heap)
    : FrontendApplicationBase(m, heap)
{

}
