#ifndef SBCG_ETHERNET_MONITOR_H
#define SBCG_ETHERNET_MONITOR_H

#include <stdint.h>

typedef void (*EthernetMonitor_Log)(const char *line, int length);

void EthernetMonitor_Init(uint32_t now_ms, EthernetMonitor_Log log_line);
void EthernetMonitor_Process(uint32_t now_ms);

#endif
