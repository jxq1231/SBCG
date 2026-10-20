#ifndef SBCG_BSP_PTP_H
#define SBCG_BSP_PTP_H

#include <stdint.h>

typedef enum
{
    BSP_PTP_OK,
    BSP_PTP_NOT_READY,
    BSP_PTP_BAD_CLOCK,
    BSP_PTP_HAL_ERROR,
    BSP_PTP_INIT_TIMEOUT,
    BSP_PTP_BAD_CONFIG,
    BSP_PTP_BAD_SNAPSHOT
} BSP_PtpStatus;

typedef struct
{
    uint32_t seconds;
    uint32_t nanoseconds;
} BSP_PtpTime;

/* Main context after MX_LWIP_Init, with HAL tick running. Local time only. */
BSP_PtpStatus BSP_PTP_Init(void);
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *time);
/* Last completed frame cache: use synchronously before reading another frame. */
BSP_PtpStatus BSP_PTP_ReadRx(BSP_PtpTime *time);

#endif
