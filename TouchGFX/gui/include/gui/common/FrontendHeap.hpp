/**
 * @file        FrontendHeap.hpp
 * @brief       声明TouchGFX前端单例及编译期确定的页面、Presenter和过渡存储。
 * @details     类型列表选出最大对象尺寸，各Partition固定容纳一个对象；模型与应用作为成员持有。
 * @note        getInstance使用函数静态实例，返回借用引用；这里不为每帧动态申请存储。
 */
#ifndef FRONTENDHEAP_HPP
#define FRONTENDHEAP_HPP

#include <gui_generated/common/FrontendHeapBase.hpp>

/** @brief 固定前端存储单例；本页之外的用户自定义类型列表当前为空。 */
class FrontendHeap : public FrontendHeapBase
{
public:
    /** @brief 用户扩展View类型列表，当前无额外View；Nil为列表终止标记。 */
    typedef touchgfx::meta::TypeList< touchgfx::meta::Nil,
                            touchgfx::meta::Nil
                            > UserDefinedViewTypes;

    /** @brief 用户扩展Presenter类型列表，当前为空。 */
    typedef touchgfx::meta::TypeList< touchgfx::meta::Nil,
                            touchgfx::meta::Nil
                            > UserDefinedPresenterTypes;

    /** @brief 用户扩展过渡类型列表，当前为空。 */
    typedef touchgfx::meta::TypeList< touchgfx::meta::Nil,
                            touchgfx::meta::Nil
                            > UserDefinedTransitionTypes;



    /** @brief 在编译期合并生成与用户View类型，确定固定存储所需的最大View尺寸。 */
    typedef touchgfx::meta::select_type_maxsize< UserDefinedViewTypes >::type MaxUserViewType;

    typedef touchgfx::meta::TypeList< MaxGeneratedViewType,
	        touchgfx::meta::TypeList< MaxUserViewType,
                            touchgfx::meta::Nil
                            > > CombinedViewTypes;

    typedef touchgfx::meta::select_type_maxsize< CombinedViewTypes >::type MaxViewType;

    /** @brief 在编译期合并生成与用户Presenter类型，确定最大Presenter尺寸。 */
    typedef touchgfx::meta::select_type_maxsize< UserDefinedPresenterTypes >::type MaxUserPresenterType;

    typedef touchgfx::meta::TypeList< MaxGeneratedPresenterType,
            touchgfx::meta::TypeList< MaxUserPresenterType,
                            touchgfx::meta::Nil
                            > > CombinedPresenterTypes;
    typedef touchgfx::meta::select_type_maxsize< CombinedPresenterTypes >::type MaxPresenterType;

    /** @brief 在编译期合并生成与用户过渡类型，确定最大过渡对象尺寸。 */
    typedef touchgfx::meta::select_type_maxsize< UserDefinedTransitionTypes >::type MaxUserTransitionType;

    typedef touchgfx::meta::TypeList< MaxGeneratedTransitionType,
            touchgfx::meta::TypeList< MaxUserTransitionType,
                            touchgfx::meta::Nil
                            > > CombinedTransitionTypes;
    typedef touchgfx::meta::select_type_maxsize< CombinedTransitionTypes >::type MaxTransitionType;

    /**
     * @brief 返回首次调用时构造的函数静态前端存储。
     * @return 静态实例的借用引用，覆盖后续前端使用期，调用方不得销毁。
     * @note 首次构造会建立模型、应用并请求起始页面；后续调用返回同一实例。
     */
    static FrontendHeap& getInstance()
    {
        static FrontendHeap instance;
        return instance;
    }

    /** @brief 编译期定容的单个Presenter、View和过渡对象存储分区。 */
    touchgfx::Partition< CombinedPresenterTypes, 1 > presenters;
    touchgfx::Partition< CombinedViewTypes, 1 > views;
    touchgfx::Partition< CombinedTransitionTypes, 1 > transitions;
    /** @brief 本实例拥有的模型与应用对象，应用引用本模型及前端存储。 */
    Model model;
    FrontendApplication app;

private:
    /**
     * @brief 使用本实例分区构造前端基类，再构造应用并请求生成起始页。
     * @note 仅由getInstance首次访问触发，页面实际切换由框架后续处理。
     */
    FrontendHeap() : FrontendHeapBase(presenters, views, transitions, app),
                     app(model, *this)
    {
        gotoStartScreen(app);
    }
};

#endif // FRONTENDHEAP_HPP
