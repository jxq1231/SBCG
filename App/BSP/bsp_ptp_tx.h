#ifndef SBCG_BSP_PTP_TX_H
#define SBCG_BSP_PTP_TX_H
#include "bsp_ptp.h"

/* Main context, synchronous single-buffer TX only. One Begin/End at a time. */
BSP_PtpStatus BSP_PTP_TxClean(void);
BSP_PtpStatus BSP_PTP_TxBegin(const uint8_t *buffer, uint16_t length);
BSP_PtpStatus BSP_PTP_TxEnd(BSP_PtpTime *time);

#endif
