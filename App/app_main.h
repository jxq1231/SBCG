/**
 * @file        app_main.h
 * @brief       提供应用初始化、主循环推进及模式观测接口。
 * @details     将Core的硬件启动流程与App的多模块调度隔离，不暴露外设句柄。
 * @note        由Core/Src/main.c使用；应先初始化外设，再初始化应用，最后循环处理。
 */
#ifndef SBCG_APP_MAIN_H
#define SBCG_APP_MAIN_H
#include "Measure/clap_ranging.h"
#ifdef SBCG_USE_MODE_OBSERVATION
#include "Mode/mode_service.h"
#endif
#ifdef __cplusplus
extern "C" {
#endif

/** @brief 复制独立击掌展示状态，不消费队列。@param out 非NULL调用方输出，仅前台使用。@return 无返回值。 */
void App_GetClapSnapshot(ClapRangingSnapshot *out);

#ifdef SBCG_USE_MODE_OBSERVATION
/**
 * @brief 为本次启动设置一次模式代次。
 * @param boot 调用者提供的非零启动代次，同一板跨启动须严格递增。
 * @return 角色已配置且首次接收有效代次为true。
 * @note App_Init后前台调用，供诊断控制或主机夹具接入。
 */
bool App_ModeSetBoot(uint64_t boot);

/**
 * @brief 向模式观测状态机提交请求。
 * @param mode 请求模式，取RANGE、CLAP、WAVE或CALIBRATION。
 * @return A板接收有效请求为true，B板拒绝。
 * @note 前台调用。请求被接收后仍需握手，观测结果不驱动业务调度。
 */
bool App_ModeRequest(ModeKind mode);

/**
 * @brief 复制当前模式观测状态。
 * @param out 调用者持有的输出快照，为NULL时不执行操作。
 * @return 无返回值。
 */
void App_GetModeSnapshot(ModeSnapshot *out);

#ifdef SBCG_MODE_TEST_CONTROL
/** @brief 通过应用日志输出一次当前模式快照。@return 无返回值。 */
void App_ReportMode(void);
#endif
#endif

/* Call once after CubeMX peripheral initialization, then from the main loop. */
/**
 * @brief   在CubeMX外设初始化后建立应用服务及其依赖关系。
 * @return  无返回值（void）。
 * @note    输入来自已初始化的板级资源；具体启动顺序和重复初始化限制见实现。
 */
void App_Init(void);
/**
 * @brief   推进网络、温度、音频观测、模式控制和串口命令的一轮前台处理。
 * @return  无返回值（void）。
 * @note    依赖App_Init建立的状态；由main循环反复调用，不代表固定周期任务。
 */
void App_Process(void);

#ifdef __cplusplus
}
#endif

#endif
