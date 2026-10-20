#ifndef SBCG_PTP_CLOCK_H
#define SBCG_PTP_CLOCK_H

#include <stdint.h>

typedef void (*PtpClock_Log)(const char *line, int length);

/* Local MAC clock observation, never a declaration of synchronization. */
void PtpClock_Init(uint32_t now_ms, PtpClock_Log log);
void PtpClock_Process(uint32_t now_ms);

#endif
