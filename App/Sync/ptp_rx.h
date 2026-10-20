#ifndef SBCG_PTP_RX_H
#define SBCG_PTP_RX_H

#include <stdint.h>

typedef void (*PtpRx_Log)(const char *line, int length);
/* NO_SYS main context, after MX_LWIP_Init and PtpClock_Init. */
void PtpRx_Init(uint32_t now_ms, PtpRx_Log log);
/* Call once for a validated v1 request, before releasing its pbuf. */
void PtpRx_Observe(const uint8_t request[28]);
void PtpRx_Process(uint32_t now_ms);

#endif
