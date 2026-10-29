/**
 * @file        bsp_board_identity.h
 * @brief       声明由控制台系统信息准备本机网络身份的 BSP 接口。
 * @details     提供 IPv4 参数准备、只读身份缓存访问和 ETH 初始化前的 MAC 复制。
 * @note        仅启动/前台串行调用；不写硬件寄存器，不执行 HAL_ETH_Init 或 LwIP 初始化。
 */
#ifndef SBCG_BSP_BOARD_IDENTITY_H
#define SBCG_BSP_BOARD_IDENTITY_H

#include "board_identity.h"

/**
 * @brief   读取本机 UID 并准备当前网络身份及 IPv4 参数。
 * @param   ip 调用者拥有的四字节 IPv4 输出；必须非 NULL，失败时清零。
 * @param   mask 调用者拥有的四字节掩码输出；必须非 NULL，失败时清零。
 * @param   gw 调用者拥有的四字节网关输出；必须非 NULL，失败时清零。
 * @return  true 表示实际 UID 匹配且缓存就绪；false 表示缓存失效且所有非 NULL 地址输出清零。
 * @note    在 LwIP/ETH 初始化前串行调用；通过 BSP_Console_GetInfo 读取 UID，每次调用重新准备并使旧结果失效。
 */
bool BSP_BoardIdentity_ConfigureIp(uint8_t ip[4], uint8_t mask[4], uint8_t gw[4]);

/**
 * @brief   获取最近一次成功准备的只读身份。
 * @return  成功准备后返回模块拥有的静态 profile；未准备或准备失败返回 NULL。
 * @note    指针不得修改或释放，内容仅保证在下一次 ConfigureIp 调用前有效；不用于并发 IRQ 访问。
 */
const BoardIdentityProfile *BSP_BoardIdentity_Get(void);

/**
 * @brief   将已准备身份的六字节 MAC 复制到调用者缓冲区。
 * @param   mac 调用者拥有的六字节输出；NULL 返回 false，未准备时非 NULL 输出清零。
 * @return  true 表示复制成功，false 表示参数为空或身份未准备。
 * @note    供 HAL_ETH_Init 前串行调用，不写硬件，不改变准备状态，不保存输出指针。
 */
bool BSP_BoardIdentity_CopyMac(uint8_t mac[6]);

#endif
