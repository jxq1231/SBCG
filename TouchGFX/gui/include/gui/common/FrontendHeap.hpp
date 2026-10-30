/**
 * @file        FrontendHeap.hpp
 * @brief       声明TouchGFX前端单例及编译期确定的页面、Presenter和过渡存储。
 * @details     类型列表选出最大对象尺寸，各Partition固定容纳一个对象；完整前端存于专用SDRAM NOLOAD区域。
 * @note        首次getInstance必须晚于显示SDRAM READY；显式清零后原位构造，不申请运行堆，不接触DMA内存。
 */
#ifndef FRONTENDHEAP_HPP
#define FRONTENDHEAP_HPP

#include <gui_generated/common/FrontendHeapBase.hpp>
#include <cstdint>
#include <cstring>
#include <new>

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
     * @brief 显示SDRAM就绪后首次调用，清零并原位构造定容前端。
     * @return 静态实例的借用引用，覆盖后续前端使用期，调用方不得销毁。
     * @note 仅由UI服务在BSP显示READY后通过touchgfx_init前台调用；NOLOAD数据不能依赖启动BSS清零。
     *       独立链接区域位于两个帧缓冲之后，链接器限制8KiB；不改变SDRAM或缓存配置。
     */
    static FrontendHeap& getInstance()
    {
        alignas(32) static uint8_t storage[sizeof(FrontendHeap)]
            __attribute__((section(".TouchGFXState")));
        static FrontendHeap* instance = nullptr;
        if (instance == nullptr)
        {
            std::memset(storage, 0, sizeof storage);
            instance = new (static_cast<void*>(storage)) FrontendHeap();
        }
        return *instance;
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
