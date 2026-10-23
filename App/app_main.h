/**
 * @file        app_main.h
 * @brief       对外提供应用初始化与主循环推进两个入口。
 * @details     将Core的硬件启动流程与App的多模块调度隔离，不暴露外设句柄。
 * @note        由Core/Src/main.c使用；应先初始化外设，再初始化应用，最后循环处理。
 */
#ifndef SBCG_APP_MAIN_H
#define SBCG_APP_MAIN_H

/* Call once after CubeMX peripheral initialization, then from the main loop. */
/**
 * @brief   在CubeMX外设初始化后建立应用服务及其依赖关系。
 * @return  无返回值（void）。
 * @note    输入来自已初始化的板级资源；具体启动顺序和重复初始化限制见实现。
 */
void App_Init(void);
/**
 * @brief   推进网络、温度、音频观测和串口命令的一轮前台处理。
 * @return  无返回值（void）。
 * @note    依赖App_Init建立的状态；由main循环反复调用，不代表固定周期任务。
 */
void App_Process(void);

#endif
