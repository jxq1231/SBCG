#ifndef SBCG_PTP_TX_H
#define SBCG_PTP_TX_H
#include <stdint.h>
typedef void (*PtpTx_Log)(const char *line, int length);
/* NO_SYS main context, after the PTP clock and RX observer initialize. */
void PtpTx_Init(uint32_t now_ms, const uint32_t uid_words[3], PtpTx_Log log);
void PtpTx_Process(uint32_t now_ms);
#endif
