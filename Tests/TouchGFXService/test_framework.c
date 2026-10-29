/**
 * @file        test_framework.c
 * @brief       将真实NoOS入口桩放到独立编译单元，验证GNU链接包装的实际路由。
 * @details     记录初始化与处理次数，并允许测试在框架处理期间注入错误。
 * @note        仅主机测试，不执行TouchGFX或硬件代码。
 */
#include <assert.h>
extern unsigned framework_inits, framework_calls, bsp_inits;
extern void Test_FrameworkStep(void);
/** @brief 检查BSP先就绪并记录框架初始化。@return 无返回值。 */
void MX_TouchGFX_Init(void) { assert(bsp_inits==1U); ++framework_inits; }
/** @brief 记录真实入口调用并执行测试注入。@return 无返回值，不等待VSYNC。 */
void MX_TouchGFX_Process(void) { assert(framework_inits==1U); ++framework_calls; Test_FrameworkStep(); }
