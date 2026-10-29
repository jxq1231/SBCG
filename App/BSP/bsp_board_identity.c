/**
 * @file        bsp_board_identity.c
 * @brief       使用现有控制台信息读取接口准备并缓存本机网络身份。
 * @details     每次准备先失效旧缓存，解析真实 UID 后才输出 IPv4；MAC 仅从成功缓存复制。
 * @note        在 LwIP/ETH 初始化前串行调用，无硬件写入、IRQ、DMA 或 cache 维护。
 */
#include "bsp_board_identity.h"
#include "bsp_console.h"

#include <stddef.h>
#include <string.h>

static BoardIdentityProfile prepared_profile;
static bool identity_prepared;

/**
 * @brief   使旧身份失效，并从现有控制台信息中的实际 UID 重新准备 IPv4。
 * @param   ip 调用者拥有的四字节输出，NULL 时失败；失败清零非 NULL 输出。
 * @param   mask 调用者拥有的四字节掩码输出，NULL 时失败；失败清零非 NULL 输出。
 * @param   gw 调用者拥有的四字节网关输出，NULL 时失败；失败清零非 NULL 输出。
 * @return  true 表示 UID 匹配且地址及缓存均就绪；false 表示未准备。
 * @note    仅启动/前台串行调用，不保留输出指针，不触发网络或时钟写入。
 */
bool BSP_BoardIdentity_ConfigureIp(uint8_t ip[4], uint8_t mask[4], uint8_t gw[4])
{
    BSP_ConsoleInfo info;

    identity_prepared = false;
    memset(&prepared_profile, 0, sizeof(prepared_profile));
    if (ip != NULL) { memset(ip, 0, 4U); }
    if (mask != NULL) { memset(mask, 0, 4U); }
    if (gw != NULL) { memset(gw, 0, 4U); }
    if (ip == NULL || mask == NULL || gw == NULL)
    {
        return false;
    }

    BSP_Console_GetInfo(&info);
    if (!BoardIdentity_Resolve(&BoardIdentity_DefaultMap, info.uid_words, &prepared_profile))
    {
        return false;
    }

    memcpy(ip, prepared_profile.local_ip, sizeof(prepared_profile.local_ip));
    memcpy(mask, prepared_profile.netmask, sizeof(prepared_profile.netmask));
    memcpy(gw, prepared_profile.gateway, sizeof(prepared_profile.gateway));
    identity_prepared = true;
    return true;
}

/**
 * @brief   读取当前已准备身份的只读静态缓存。
 * @return  准备成功返回模块缓存地址，否则返回 NULL。
 * @note    调用者不得修改或释放；内容可被下一次 ConfigureIp 清零或替换，仅串行访问。
 */
const BoardIdentityProfile *BSP_BoardIdentity_Get(void)
{
    return identity_prepared ? &prepared_profile : NULL;
}

/**
 * @brief   输出已准备身份的 MAC，未准备时清零输出。
 * @param   mac 调用者拥有的六字节输出；NULL 返回 false 且不访问输出。
 * @return  true 表示完整复制；false 表示 NULL 参数或身份未准备。
 * @note    不改变缓存状态，不调用 HAL，不保存缓冲指针。
 */
bool BSP_BoardIdentity_CopyMac(uint8_t mac[6])
{
    if (mac == NULL)
    {
        return false;
    }
    if (!identity_prepared)
    {
        memset(mac, 0, 6U);
        return false;
    }

    memcpy(mac, prepared_profile.local_mac, sizeof(prepared_profile.local_mac));
    return true;
}
