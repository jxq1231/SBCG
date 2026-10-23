/**
 * @file        bsp_ethernet.h
 * @brief       LAN8742 PHY 身份与链路状态的只读板级观测。
 * @details     声明 PHY 身份/协商信息、链路 DOWN/NEGOTIATING/UP 状态及只读查询接口；speed_mbps 单位 Mbps，
 *          phy_addr 为 MDIO 地址。
 * @note        CubeMX/LwIP 拥有 ETH 初始化及 DMA 启停；本模块拒绝 PHY 写操作，不复位 PHY，也不改变协商或 MAC
 *          配置。
 */

#ifndef SBCG_BSP_ETHERNET_H
#define SBCG_BSP_ETHERNET_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 只读 PHY 访问结果；OK 与当前链路是否连接无直接等价关系。 */
typedef enum
{
    BSP_ETH_OK,
    BSP_ETH_NOT_READY,
    BSP_ETH_NOT_FOUND,
    BSP_ETH_IO_ERROR,
    BSP_ETH_ID_MISMATCH
} BSP_EthernetStatus;

/** @brief MDIO 地址、两个 PHY 身份寄存器和自动协商使能位；不改变 PHY 配置。 */
typedef struct
{
    uint32_t phy_addr;
    uint16_t phy_id1;
    uint16_t phy_id2;
    bool autoneg_enabled;
} BSP_EthernetInfo;

/** @brief PHY 观测链路状态：断开、协商中或已连接。 */
typedef enum
{
    BSP_ETH_LINK_DOWN,
    BSP_ETH_LINK_NEGOTIATING,
    BSP_ETH_LINK_UP
} BSP_EthernetLinkState;

/** @brief 一次链路观测；仅 UP 状态的 speed_mbps（Mbps）和 full_duplex 有连接参数意义。 */
typedef struct
{
    BSP_EthernetLinkState state;
    uint32_t speed_mbps;
    bool full_duplex;
} BSP_EthernetLink;

/* Main context only, after generated ETH initialization. Does not start frame DMA. */
/**
 * @brief   建立 LAN8742 只读观测视图并校验身份和自动协商使能状态。
 * @param   info 调用者拥有的非空输出；无空指针检查。先清零并把 phy_addr 置 UINT32_MAX，失败可能保留已读取的部
 *          分信息。
 * @return  OK 表示 PHY 身份和控制寄存器读取有效；其它区分 ETH 未就绪、未发现、MDIO 失败及型号不符。
 * @note    仅前台、生成 ETH 初始化后调用，不启动报文 DMA。LwIP 构建借用其已发现的 LAN8742 对象副本并替换副本
 *          读写回调；非 LwIP 才进行地址发现。
 */
BSP_EthernetStatus BSP_Ethernet_Init(BSP_EthernetInfo *info);
/**
 * @brief   读取 PHY 当前链路、速率和双工状态。
 * @param   link 调用者拥有的非空输出；先清零，无空指针检查。失败时清零结果不代表确认链路断开。
 * @return  OK 表示链路状态解码成功；NOT_READY 表示尚未建立观测视图；IO_ERROR 会使视图失效，须重新初始化。
 * @note    仅前台；LAN8742 组件双读 BSR 处理锁存低位，报告 DOWN/NEGOTIATING/UP，速率单位 Mbps。不改变 MAC 或
 *          PHY 的状态。
 */
BSP_EthernetStatus BSP_Ethernet_ReadLink(BSP_EthernetLink *link);

#endif
