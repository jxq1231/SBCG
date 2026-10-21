#ifndef SBCG_BSP_DHT11_H
#define SBCG_BSP_DHT11_H

#include <stdint.h>

typedef enum
{
    BSP_DHT11_OK,
    BSP_DHT11_INIT_ERROR,
    BSP_DHT11_LINE_BUSY,
    BSP_DHT11_START_WINDOW,
    BSP_DHT11_TIMEOUT,
    BSP_DHT11_PULSE_ERROR,
    BSP_DHT11_CLOCK_ERROR,
    BSP_DHT11_ARGUMENT
} BSP_Dht11Status;

/* Main-loop only. Uses generated open-drain GPIO and the core DWT counter;
   never reconfigures GPIO, interrupts, clocks, TIM, DMA or the PTP clock. */
BSP_Dht11Status BSP_DHT11_Init(void);
/* Start only pulls low. Capture releases after a valid 18..30ms low interval,
   receives a bounded frame and leaves output unchanged on failure. */
BSP_Dht11Status BSP_DHT11_Start(void);
BSP_Dht11Status BSP_DHT11_Capture(uint8_t frame[5]);
void BSP_DHT11_Abort(void);

#endif
