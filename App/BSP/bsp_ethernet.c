/**
 * @file        bsp_ethernet.c
 * @brief       LAN8742 PHY 身份与链路状态的只读板级观测。
 * @details     通过 HAL MDIO 与 ST LAN8742 组件校验身份、读取协商和速率；LwIP 构建使用其 PHY 对象的私有副本，
 *          非 LwIP 构建才扫描地址。
 * @note        CubeMX/LwIP 拥有 ETH 初始化及 DMA 启停；本模块拒绝 PHY 写操作，不复位 PHY，也不改变协商或 MAC
 *          配置。
 */

#include "bsp_ethernet.h"
#include "main.h"
#include "lan8742.h"

#include <string.h>

#define LAN8742_EXPECTED_ID1 0x0007U
#define LAN8742_EXPECTED_ID2 0xC130U
#define LAN8742_MODEL_MASK   0xFFF0U

extern ETH_HandleTypeDef heth;
#ifdef SBCG_USE_LWIP
/* The generated interface owns discovery and ETH start/stop. */
extern lan8742_Object_t LAN8742;
#endif

static lan8742_Object_t phy;
static bool io_failed;
static bool phy_ready;

#ifndef SBCG_USE_LWIP
/**
 * @brief   为非 LwIP 的 PHY 观测适配器准备运行时 MDIO 分频。
 * @return  0 表示可访问；-1 表示 MDIO 仍忙并记录 io_failed。
 * @note    仅非 SBCG_USE_LWIP 构建需要，ETH 已由 CubeMX 初始化；不清正在执行的 MDIO 事务，不改 RCC 或引脚。
 */
static int32_t PHY_IO_Init(void)
{
    /* Do not clear a pending transaction when retrying a failed scan. */
    if (READ_BIT(heth.Instance->MACMIIAR, ETH_MACMIIAR_MB) != 0U)
    {
        io_failed = true;
        return -1;
    }
    /* Runtime MDIO bus divider only; RCC/pins remain CubeMX-owned. */
    HAL_ETH_SetMDIOClockRange(&heth);
    return 0;
}
#endif

/**
 * @brief   通过 HAL 的 MDIO 接口读取 PHY 寄存器。
 * @param   address PHY 的 MDIO 地址，由组件发现/保存。
 * @param   reg PHY 寄存器编号。
 * @param   value 组件提供的非空输出地址，成功由 HAL 写入寄存器值；失败时不得使用，适配器不保存指针。
 * @return  0 表示 HAL 读取成功；-1 表示 MDIO 忙或 HAL 失败，并置 io_failed。
 * @note    前台只读观测路径；先检查 MB，避免一次超时后扫描其它地址时反复等待。
 */
static int32_t PHY_IO_Read(uint32_t address, uint32_t reg, uint32_t *value)
{
    /* A HAL timeout can leave MB set. Avoid another full timeout for each
       remaining address in the ST component's discovery loop. */
    if ((READ_BIT(heth.Instance->MACMIIAR, ETH_MACMIIAR_MB) != 0U) ||
        (HAL_ETH_ReadPHYRegister(&heth, address, reg, value) != HAL_OK))
    {
        io_failed = true;
        return -1;
    }
    return 0;
}

/**
 * @brief   拒绝 PHY 组件的全部寄存器写请求。
 * @param   address 未使用的 PHY 地址，仅用于匹配组件回调签名。
 * @param   reg 未使用的寄存器编号。
 * @param   value 未使用的待写值。
 * @return  固定返回 -1，表示此只读适配器不支持写入。
 * @note    不会复位 PHY、启动协商或改网络所有者的配置。
 */
static int32_t PHY_IO_Write(uint32_t address, uint32_t reg, uint32_t value)
{
    /* The component requires a write callback; this observation-only adapter
       deliberately rejects writes, including reset/autonegotiation changes. */
    (void)address;
    (void)reg;
    (void)value;
    return -1;
}

#ifndef SBCG_USE_LWIP
/**
 * @brief   向非 LwIP PHY 组件提供 HAL 毫秒计数。
 * @return  HAL_GetTick 转为 int32_t 的结果，按组件接口约定传递原计数。
 * @note    仅非 SBCG_USE_LWIP 构建注册，不新建定时器。
 */
static int32_t PHY_IO_GetTick(void)
{
    return (int32_t)HAL_GetTick();
}
#endif

/**
 * @brief   建立 LAN8742 只读观测视图并校验身份和自动协商使能状态。
 * @param   info 调用者拥有的非空输出；无空指针检查。先清零并把 phy_addr 置 UINT32_MAX，失败可能保留已读取的部
 *          分信息。
 * @return  OK 表示 PHY 身份和控制寄存器读取有效；其它区分 ETH 未就绪、未发现、MDIO 失败及型号不符。
 * @note    仅前台、生成 ETH 初始化后调用，不启动报文 DMA。LwIP 构建借用其已发现的 LAN8742 对象副本并替换副本
 *          读写回调；非 LwIP 才进行地址发现。
 */
BSP_EthernetStatus BSP_Ethernet_Init(BSP_EthernetInfo *info)
{
    phy_ready = false;
    memset(info, 0, sizeof(*info));
    info->phy_addr = UINT32_MAX;
    if ((heth.Instance == NULL) ||
        ((heth.gState != HAL_ETH_STATE_READY) && (heth.gState != HAL_ETH_STATE_STARTED)))
    {
        return BSP_ETH_NOT_READY;
    }

    io_failed = false;
#ifdef SBCG_USE_LWIP
    if ((LAN8742.Is_Initialized == 0U) || (LAN8742.DevAddr > 31U))
    {
        return BSP_ETH_NOT_READY;
    }
    /* Observe the discovered device through a private, read-only view. Do not
       reset the owner object, rescan the bus, or change its IO callbacks. */
    phy = LAN8742;
    phy.IO.ReadReg = PHY_IO_Read;
    phy.IO.WriteReg = PHY_IO_Write;
#else
    memset(&phy, 0, sizeof(phy));
    lan8742_IOCtx_t io = {
        .Init = PHY_IO_Init, .DeInit = NULL,
        .WriteReg = PHY_IO_Write, .ReadReg = PHY_IO_Read,
        .GetTick = PHY_IO_GetTick
    };
    if (LAN8742_RegisterBusIO(&phy, &io) != LAN8742_STATUS_OK)
    {
        return BSP_ETH_IO_ERROR;
    }
    if (LAN8742_Init(&phy) != LAN8742_STATUS_OK)
    {
        return io_failed ? BSP_ETH_IO_ERROR : BSP_ETH_NOT_FOUND;
    }
#endif

    info->phy_addr = phy.DevAddr;
    uint32_t id1, id2, control;
    if ((PHY_IO_Read(phy.DevAddr, LAN8742_PHYI1R, &id1) != 0) ||
        (PHY_IO_Read(phy.DevAddr, LAN8742_PHYI2R, &id2) != 0))
    {
        return BSP_ETH_IO_ERROR;
    }
    info->phy_id1 = (uint16_t)id1;
    info->phy_id2 = (uint16_t)id2;
    /* An unpopulated bus may return FFFF and match SMR at address 31. */
    if (((id1 == 0xFFFFU) && (id2 == 0xFFFFU)) ||
        ((id1 == 0U) && (id2 == 0U)))
    {
        return BSP_ETH_NOT_FOUND;
    }
    if ((id1 != LAN8742_EXPECTED_ID1) ||
        ((id2 & LAN8742_MODEL_MASK) != LAN8742_EXPECTED_ID2))
    {
        return BSP_ETH_ID_MISMATCH;
    }
    if (PHY_IO_Read(phy.DevAddr, LAN8742_BCR, &control) != 0)
    {
        return BSP_ETH_IO_ERROR;
    }
    info->autoneg_enabled = (control & LAN8742_BCR_AUTONEGO_EN) != 0U;
    phy_ready = true;
    return BSP_ETH_OK;
}

/**
 * @brief   读取 PHY 当前链路、速率和双工状态。
 * @param   link 调用者拥有的非空输出；先清零，无空指针检查。失败时清零结果不代表确认链路断开。
 * @return  OK 表示链路状态解码成功；NOT_READY 表示尚未建立观测视图；IO_ERROR 会使视图失效，须重新初始化。
 * @note    仅前台；LAN8742 组件双读 BSR 处理锁存低位，报告 DOWN/NEGOTIATING/UP，速率单位 Mbps。不改变 MAC 或
 *          PHY 的状态。
 */
BSP_EthernetStatus BSP_Ethernet_ReadLink(BSP_EthernetLink *link)
{
    memset(link, 0, sizeof(*link));
    if (!phy_ready)
    {
        return BSP_ETH_NOT_READY;
    }
    /* The ST component reads BSR twice to handle latched-low link status. */
    const int32_t state = LAN8742_GetLinkState(&phy);
    switch (state)
    {
        case LAN8742_STATUS_LINK_DOWN:
            link->state = BSP_ETH_LINK_DOWN;
            break;
        case LAN8742_STATUS_AUTONEGO_NOTDONE:
            link->state = BSP_ETH_LINK_NEGOTIATING;
            break;
        case LAN8742_STATUS_100MBITS_FULLDUPLEX:
        case LAN8742_STATUS_100MBITS_HALFDUPLEX:
        case LAN8742_STATUS_10MBITS_FULLDUPLEX:
        case LAN8742_STATUS_10MBITS_HALFDUPLEX:
            link->state = BSP_ETH_LINK_UP;
            link->speed_mbps = ((state == LAN8742_STATUS_100MBITS_FULLDUPLEX) ||
                (state == LAN8742_STATUS_100MBITS_HALFDUPLEX)) ? 100U : 10U;
            link->full_duplex = (state == LAN8742_STATUS_100MBITS_FULLDUPLEX) ||
                (state == LAN8742_STATUS_10MBITS_FULLDUPLEX);
            break;
        default:
            phy_ready = false;
            return BSP_ETH_IO_ERROR;
    }
    return BSP_ETH_OK;
}
