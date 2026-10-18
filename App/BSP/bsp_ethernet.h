#ifndef SBCG_BSP_ETHERNET_H
#define SBCG_BSP_ETHERNET_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    BSP_ETH_OK,
    BSP_ETH_NOT_READY,
    BSP_ETH_NOT_FOUND,
    BSP_ETH_IO_ERROR,
    BSP_ETH_ID_MISMATCH
} BSP_EthernetStatus;

typedef struct
{
    uint32_t phy_addr;
    uint16_t phy_id1;
    uint16_t phy_id2;
    bool autoneg_enabled;
} BSP_EthernetInfo;

typedef enum
{
    BSP_ETH_LINK_DOWN,
    BSP_ETH_LINK_NEGOTIATING,
    BSP_ETH_LINK_UP
} BSP_EthernetLinkState;

typedef struct
{
    BSP_EthernetLinkState state;
    uint32_t speed_mbps;
    bool full_duplex;
} BSP_EthernetLink;

/* Main context only, after CubeMX MX_ETH_Init. Does not start frame DMA. */
BSP_EthernetStatus BSP_Ethernet_Init(BSP_EthernetInfo *info);
BSP_EthernetStatus BSP_Ethernet_ReadLink(BSP_EthernetLink *link);

#endif
