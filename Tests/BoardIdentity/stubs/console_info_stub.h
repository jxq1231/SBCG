/**
 * @file        console_info_stub.h
 * @brief       为身份 BSP 测试提供可选择的控制台 UID 输入。
 * @details     仅声明测试输入设置接口；被测 BSP 仍调用实际 bsp_console.h 的声明。
 * @note        主机测试专用，不注册固件硬件或替代寄存器。
 */
#ifndef SBCG_TEST_CONSOLE_INFO_STUB_H
#define SBCG_TEST_CONSOLE_INFO_STUB_H

#include <stdint.h>

/**
 * @brief   设置下一次控制台信息读取返回的 UID。
 * @param   uid_words 调用者拥有的三个只读 UID 字；必须非 NULL，立即复制。
 * @return  无返回值。
 * @note    不保存输入指针，不访问实际 MCU。
 */
void ConsoleInfoStub_SetUid(const uint32_t uid_words[3]);

#endif
