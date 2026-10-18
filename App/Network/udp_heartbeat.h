#ifndef SBCG_UDP_HEARTBEAT_H
#define SBCG_UDP_HEARTBEAT_H

#include <stdint.h>

typedef void (*UDPHeartbeat_Log)(const char *line, int length);
/* Main context only, after MX_LWIP_Init. uid_words is copied. */
void UDPHeartbeat_Init(uint32_t now_ms, const uint32_t uid_words[3], UDPHeartbeat_Log log);
void UDPHeartbeat_Process(uint32_t now_ms);

#endif
