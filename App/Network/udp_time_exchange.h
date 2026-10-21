#ifndef SBCG_UDP_TIME_EXCHANGE_H
#define SBCG_UDP_TIME_EXCHANGE_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t (*UDPTimeExchange_Tick)(void);
typedef void (*UDPTimeExchange_Log)(const char *text, int length);

/* NO_SYS main context only. Failure leaves an unknown wrapper chain untouched. */
bool UDPTimeExchange_Deinit(void);
bool UDPTimeExchange_Init(uint32_t now_ms, const uint32_t uid_words[3],
                          UDPTimeExchange_Tick tick, UDPTimeExchange_Log log);
void UDPTimeExchange_Process(uint32_t now_ms);

#endif
