#ifndef SBCG_UDP_PROBE_H
#define SBCG_UDP_PROBE_H
#include <stdint.h>

typedef void (*UDPProbe_Log)(const char *line, int length);
/* Main context only, after MX_LWIP_Init. The log callback must stay valid. */
void UDPProbe_Init(uint32_t now_ms, const uint32_t uid_words[3], UDPProbe_Log log);
void UDPProbe_Process(uint32_t now_ms);
#endif
