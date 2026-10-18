#include "bsp_ethernet.h"
#include "main.h"
#include "lan8742.h"

#include <string.h>

#define LAN8742_EXPECTED_ID1 0x0007U
#define LAN8742_EXPECTED_ID2 0xC130U
#define LAN8742_MODEL_MASK   0xFFF0U

extern ETH_HandleTypeDef heth;

static lan8742_Object_t phy;
static bool io_failed;
static bool phy_ready;

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

static int32_t PHY_IO_Write(uint32_t address, uint32_t reg, uint32_t value)
{
    /* The component requires a write callback; this observation-only adapter
       deliberately rejects writes, including reset/autonegotiation changes. */
    (void)address;
    (void)reg;
    (void)value;
    return -1;
}

static int32_t PHY_IO_GetTick(void)
{
    return (int32_t)HAL_GetTick();
}

BSP_EthernetStatus BSP_Ethernet_Init(BSP_EthernetInfo *info)
{
    phy_ready = false;
    memset(info, 0, sizeof(*info));
    info->phy_addr = UINT32_MAX;
    if ((heth.Instance == NULL) || (heth.gState != HAL_ETH_STATE_READY))
    {
        return BSP_ETH_NOT_READY;
    }

    memset(&phy, 0, sizeof(phy));
    io_failed = false;
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
