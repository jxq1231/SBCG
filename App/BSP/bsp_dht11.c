#include "bsp_dht11.h"
#include "main.h"
#include "Temperature/temperature_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifndef BSP_DHT11_READ_CYCLES
#define BSP_DHT11_READ_CYCLES() (DWT->CYCCNT)
#endif

/* DWT is a CPU timing resource here, not a measurement/synchronization clock.
   Do not reset CYCCNT: other debugger/core observations may be using it. */
#define DWT_UNLOCK_KEY UINT32_C(0xC5ACCE55)
#define CLOCK_STALL_POLLS 128U
static bool ready, started;
static uint32_t cycles_per_us, low_earliest_cycle, low_latest_cycle, frame_started_cycle;

static bool GeneratedModeValid(void)
{
    const uint32_t pin=DHT11_DATA_Pin;
    if(pin==0U || pin>0x8000U || (pin & (pin-1U))!=0U) { return false; }
    unsigned int position=0U;
    while((pin>>position)!=1U) { ++position; }
    return ((DHT11_DATA_GPIO_Port->MODER>>(position*2U))&3U)==1U &&
        (DHT11_DATA_GPIO_Port->OTYPER & pin)!=0U &&
        ((DHT11_DATA_GPIO_Port->PUPDR>>(position*2U))&3U)==0U;
}

BSP_Dht11Status BSP_DHT11_Init(void)
{
    ready=false; started=false;
    if(!GeneratedModeValid()) { return BSP_DHT11_INIT_ERROR; }
    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port,DHT11_DATA_Pin,GPIO_PIN_SET);
    if(SystemCoreClock<1000000U || SystemCoreClock>216000000U ||
       SystemCoreClock%1000000U!=0U || (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk)!=0U)
    { return BSP_DHT11_INIT_ERROR; }
    cycles_per_us=SystemCoreClock/1000000U;
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR=DWT_UNLOCK_KEY; /* Cortex-M7 DWT software lock access key. */
    DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    const uint32_t before=BSP_DHT11_READ_CYCLES();
    for(unsigned int i=0;i<CLOCK_STALL_POLLS;++i)
    {
        if(BSP_DHT11_READ_CYCLES()!=before)
        { ready=true; return BSP_DHT11_OK; }
    }
    return BSP_DHT11_CLOCK_ERROR;
}

void BSP_DHT11_Abort(void)
{
    if(ready || started)
    { HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port,DHT11_DATA_Pin,GPIO_PIN_SET); }
    started=false;
}

BSP_Dht11Status BSP_DHT11_Start(void)
{
    if(!ready) { return BSP_DHT11_INIT_ERROR; }
    if(started || HAL_GPIO_ReadPin(DHT11_DATA_GPIO_Port,DHT11_DATA_Pin)!=GPIO_PIN_SET)
    { BSP_DHT11_Abort(); return BSP_DHT11_LINE_BUSY; }
    /* Bracket the write: an IRQ between the edge and a single timestamp must
       not hide a start pulse longer than the sensor permits. */
    low_earliest_cycle=BSP_DHT11_READ_CYCLES();
    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port,DHT11_DATA_Pin,GPIO_PIN_RESET);
    low_latest_cycle=BSP_DHT11_READ_CYCLES(); started=true;
    return BSP_DHT11_OK;
}

/* Only GPIO and cycle reads are substituted in native waveform tests. These
   deadlines bound protocol waiting; IRQs stay enabled. A long preemption can
   exceed the wall-time budget and is rejected on the next cycle check. */
static BSP_Dht11Status WaitLevel(GPIO_PinState level,uint32_t *edge_cycle)
{
    const uint32_t wait_start=BSP_DHT11_READ_CYCLES();
    uint32_t previous=wait_start;
    unsigned int stalled=0U;
    while(true)
    {
        const GPIO_PinState observed=HAL_GPIO_ReadPin(DHT11_DATA_GPIO_Port,DHT11_DATA_Pin);
        /* Check time after the GPIO read too: an IRQ inside that read must
           not let even the final edge bypass a protocol deadline. */
        const uint32_t current=BSP_DHT11_READ_CYCLES();
        if(current==previous)
        {
            if(++stalled>=CLOCK_STALL_POLLS) { return BSP_DHT11_CLOCK_ERROR; }
        }
        else { stalled=0U; previous=current; }
        if((uint32_t)(current-frame_started_cycle)>=TEMP_CAPTURE_TIMEOUT_US*cycles_per_us)
        { return BSP_DHT11_TIMEOUT; }
        if((uint32_t)(current-wait_start)>=TEMP_EDGE_TIMEOUT_US*cycles_per_us)
        { return BSP_DHT11_TIMEOUT; }
        if(observed==level)
        { *edge_cycle=current; return BSP_DHT11_OK; }
    }
}

static bool WidthValid(uint32_t start,uint32_t end,uint32_t min_us,uint32_t max_us)
{
    const uint32_t cycles=(uint32_t)(end-start);
    return cycles>=min_us*cycles_per_us && cycles<=max_us*cycles_per_us;
}

static BSP_Dht11Status CaptureFrame(uint8_t frame[5])
{
    uint8_t received[5]={0};
    uint32_t low,high,next_low;
    BSP_Dht11Status status=WaitLevel(GPIO_PIN_RESET,&low);
    if(status!=BSP_DHT11_OK) { return status; }
    status=WaitLevel(GPIO_PIN_SET,&high);
    if(status!=BSP_DHT11_OK) { return status; }
    if(!WidthValid(low,high,TEMP_RESP_MIN_US,TEMP_RESP_MAX_US))
    { return BSP_DHT11_PULSE_ERROR; }
    status=WaitLevel(GPIO_PIN_RESET,&next_low);
    if(status!=BSP_DHT11_OK) { return status; }
    if(!WidthValid(high,next_low,TEMP_RESP_MIN_US,TEMP_RESP_MAX_US))
    { return BSP_DHT11_PULSE_ERROR; }
    low=next_low;
    for(unsigned int i=0;i<40U;++i)
    {
        status=WaitLevel(GPIO_PIN_SET,&high);
        if(status!=BSP_DHT11_OK) { return status; }
        if(!WidthValid(low,high,TEMP_BIT_LOW_MIN_US,TEMP_BIT_LOW_MAX_US))
        { return BSP_DHT11_PULSE_ERROR; }
        status=WaitLevel(GPIO_PIN_RESET,&next_low);
        if(status!=BSP_DHT11_OK) { return status; }
        const bool zero=WidthValid(high,next_low,TEMP_ZERO_MIN_US,TEMP_ZERO_MAX_US);
        const bool one=WidthValid(high,next_low,TEMP_ONE_MIN_US,TEMP_ONE_MAX_US);
        if(!zero && !one) { return BSP_DHT11_PULSE_ERROR; }
        if(one) { received[i/8U]|=(uint8_t)(1U<<(7U-i%8U)); }
        low=next_low;
    }
    status=WaitLevel(GPIO_PIN_SET,&high);
    if(status!=BSP_DHT11_OK) { return status; }
    if(!WidthValid(low,high,TEMP_BIT_LOW_MIN_US,TEMP_BIT_LOW_MAX_US))
    { return BSP_DHT11_PULSE_ERROR; }
    memcpy(frame,received,sizeof(received));
    return BSP_DHT11_OK;
}

BSP_Dht11Status BSP_DHT11_Capture(uint8_t frame[5])
{
    if(frame==NULL) { BSP_DHT11_Abort(); return BSP_DHT11_ARGUMENT; }
    if(!ready || !started) { BSP_DHT11_Abort(); return BSP_DHT11_INIT_ERROR; }
    const uint32_t now=BSP_DHT11_READ_CYCLES();
    if((uint32_t)(now-low_latest_cycle)<TEMP_START_MIN_US*cycles_per_us ||
       (uint32_t)(now-low_earliest_cycle)>TEMP_START_MAX_US*cycles_per_us)
    { BSP_DHT11_Abort(); return BSP_DHT11_START_WINDOW; }
    /* Writing SET releases an open drain; the external resistor raises DATA. */
    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port,DHT11_DATA_Pin,GPIO_PIN_SET);
    started=false; frame_started_cycle=BSP_DHT11_READ_CYCLES();
    if((uint32_t)(frame_started_cycle-low_earliest_cycle)>TEMP_START_MAX_US*cycles_per_us)
    { BSP_DHT11_Abort(); return BSP_DHT11_START_WINDOW; }
    const BSP_Dht11Status status=CaptureFrame(frame);
    BSP_DHT11_Abort();
    return status;
}
