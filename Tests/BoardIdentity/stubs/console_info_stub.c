/**
 * @file        console_info_stub.c
 * @brief       为身份 BSP 测试替换控制台信息读取的硬件来源。
 * @details     保存测试设置的 UID，并通过现有 BSP_Console_GetInfo 声明输出。
 * @note        仅主机链接使用，不包含 HAL 或寄存器访问。
 */
#include "console_info_stub.h"
#include "BSP/bsp_console.h"

#include <string.h>

static uint32_t selected_uid[3];

/**
 * @brief   复制测试选择的三个 UID 字。
 * @param   uid_words 必须非 NULL 的只读数组；返回后不再使用其存储。
 * @return  无返回值。
 * @note    为随后 BSP_Console_GetInfo 提供输入。
 */
void ConsoleInfoStub_SetUid(const uint32_t uid_words[3])
{
    memcpy(selected_uid, uid_words, sizeof(selected_uid));
}

/**
 * @brief   输出测试 UID 和占位 HCLK。
 * @param   info 调用者拥有的非 NULL 输出结构，完全写入；UID 来源于测试设置。
 * @return  无返回值。
 * @note    替代同名硬件读取函数，HCLK 不参与身份判定。
 */
void BSP_Console_GetInfo(BSP_ConsoleInfo *info)
{
    info->core_clock_hz = 200000000U;
    memcpy(info->uid_words, selected_uid, sizeof(selected_uid));
}
