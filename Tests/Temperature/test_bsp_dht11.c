/* Only GPIO and cycle reads are replaced; the real capture implementation is
   included so all polling, deadlines and byte assembly run on the host. */
#include "main.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

GPIO_TypeDef native_gpio;
NativeDwt native_dwt;
NativeCoreDebug native_debug;
uint32_t SystemCoreClock;
static bool running, hold_low, driven_low;
static uint32_t released_cycle, reads, writes, response_us, zero_us, one_us;
static uint8_t wire[5];
static uint32_t write_delay_us, final_read_delay_us;
static uint32_t NativeCycles(void)
{
    if(running && (native_dwt.CTRL & DWT_CTRL_CYCCNTENA_Msk))
    { native_dwt.CYCCNT += 200U; }
    return native_dwt.CYCCNT;
}
#define BSP_DHT11_READ_CYCLES() NativeCycles()
#include "BSP/bsp_dht11.c"

void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState state)
{
    assert(port==&native_gpio && pin==DHT11_DATA_Pin); ++writes;
    if(state==GPIO_PIN_SET)
    {
        native_gpio.ODR|=pin;
        if(driven_low) { released_cycle=native_dwt.CYCCNT; }
        driven_low=false;
    }
    else { native_gpio.ODR&=~pin; driven_low=true; native_dwt.CYCCNT+=write_delay_us*200U; }
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port,uint16_t pin)
{
    assert(port==&native_gpio && pin==DHT11_DATA_Pin); ++reads;
    if(driven_low || hold_low) { return GPIO_PIN_RESET; }
    if(!response_us) { return GPIO_PIN_SET; }
    uint32_t us=(uint32_t)(native_dwt.CYCCNT-released_cycle)/200U;
    if(us<30U) { return GPIO_PIN_SET; } us-=30U;
    if(us<response_us) { return GPIO_PIN_RESET; } us-=response_us;
    if(us<80U) { return GPIO_PIN_SET; } us-=80U;
    for(unsigned int i=0;i<40U;++i)
    {
        if(us<50U) { return GPIO_PIN_RESET; } us-=50U;
        uint32_t width=(wire[i/8U] & (1U<<(7U-i%8U))) ? one_us:zero_us;
        if(us<width) { return GPIO_PIN_SET; } us-=width;
    }
    if(us>=50U) { native_dwt.CYCCNT+=final_read_delay_us*200U; return GPIO_PIN_SET; }
    return GPIO_PIN_RESET;
}
static void Reset(void)
{
    native_gpio=(GPIO_TypeDef){.MODER=1U<<14U,.OTYPER=DHT11_DATA_Pin,.ODR=DHT11_DATA_Pin};
    native_dwt=(NativeDwt){0}; native_debug=(NativeCoreDebug){0};
    SystemCoreClock=200000000U; running=true; hold_low=driven_low=false;
    released_cycle=reads=writes=response_us=write_delay_us=final_read_delay_us=0U; zero_us=26U; one_us=70U;
    memcpy(wire,(uint8_t[]){53,0,24,4,81},5);
}
static BSP_Dht11Status Capture(uint32_t low_us,uint8_t out[5])
{
    response_us=0U; assert(BSP_DHT11_Start()==BSP_DHT11_OK);
    native_dwt.CYCCNT+=low_us*200U;
    response_us=80U;
    return BSP_DHT11_Capture(out);
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    Reset(); assert(BSP_DHT11_Init()==BSP_DHT11_OK);
    uint8_t out[5]={1,2,3,4,5};
    assert(Capture(20000U,out)==BSP_DHT11_OK && memcmp(out,wire,5)==0 && !driven_low);
    puts("PASS: real bounded waveform capture assembles all five MSB-first bytes");

    for(unsigned int scenario=0;scenario<6U;++scenario)
    {
        Reset();
        if(scenario==0U) { native_gpio.MODER=0U; }
        if(scenario==1U) { native_gpio.OTYPER=0U; }
        if(scenario==2U) { SystemCoreClock=0U; }
        if(scenario==3U) { SystemCoreClock=200000001U; }
        if(scenario==4U) { native_dwt.CTRL=DWT_CTRL_NOCYCCNT_Msk; }
        if(scenario==5U) { running=false; }
        assert(BSP_DHT11_Init()!=BSP_DHT11_OK);
        assert(BSP_DHT11_Start()==BSP_DHT11_INIT_ERROR);
    }
    puts("PASS: absent GPIO setup, invalid frequency or unavailable/stalled DWT fails initialization");

    Reset(); assert(BSP_DHT11_Init()==BSP_DHT11_OK); hold_low=true;
    assert(BSP_DHT11_Start()==BSP_DHT11_LINE_BUSY && !driven_low);
    hold_low=false; assert(BSP_DHT11_Start()==BSP_DHT11_OK); BSP_DHT11_Abort(); assert(!driven_low);
    memset(out,0xA5,5); assert(BSP_DHT11_Capture(out)!=BSP_DHT11_OK && out[0]==0xA5);
    puts("PASS: busy/aborted bus never starts an unowned frame or changes prior output");

    for(unsigned int i=0;i<2U;++i)
    {
        Reset(); assert(BSP_DHT11_Init()==BSP_DHT11_OK); memset(out,0xA5,5);
        assert(Capture(i==0U ? 17000U:31000U,out)==BSP_DHT11_START_WINDOW);
        assert(!driven_low && out[0]==0xA5);
    }
    puts("PASS: too-short or delayed start releases bus and rejects out-of-window low time");

    Reset(); assert(BSP_DHT11_Init()==BSP_DHT11_OK); native_dwt.CYCCNT=UINT32_MAX-1000U;
    assert(Capture(20000U,out)==BSP_DHT11_OK && memcmp(out,wire,5)==0);
    puts("PASS: low timing and complete capture survive unsigned cycle counter wrap");

    for(unsigned int i=0;i<3U;++i)
    {
        Reset(); assert(BSP_DHT11_Init()==BSP_DHT11_OK); memset(out,0xA5,5);
        assert(BSP_DHT11_Start()==BSP_DHT11_OK); native_dwt.CYCCNT+=4000000U;
        if(i==1U) { hold_low=true; }
        if(i==2U) { response_us=80U; zero_us=50U; }
        BSP_Dht11Status status=BSP_DHT11_Capture(out);
        assert(status==(i==2U ? BSP_DHT11_PULSE_ERROR:BSP_DHT11_TIMEOUT));
        assert(!driven_low && out[0]==0xA5 && reads<10000U);
    }
    puts("PASS: no response, stuck low and ambiguous bit width are bounded and preserve output");

    Reset(); assert(BSP_DHT11_Init()==BSP_DHT11_OK); assert(BSP_DHT11_Start()==BSP_DHT11_OK);
    native_dwt.CYCCNT+=4000000U; running=false; response_us=80U;
    assert(BSP_DHT11_Capture(out)==BSP_DHT11_CLOCK_ERROR && !driven_low);
    Reset(); assert(BSP_DHT11_Init()==BSP_DHT11_OK); assert(BSP_DHT11_Start()==BSP_DHT11_OK);
    assert(BSP_DHT11_Capture(NULL)==BSP_DHT11_ARGUMENT && !driven_low);
    puts("PASS: frozen clock and null destination terminate safely without a polling hang");
    Reset(); assert(BSP_DHT11_Init()==BSP_DHT11_OK); write_delay_us=31000U;
    assert(Capture(20000U,out)==BSP_DHT11_START_WINDOW && !driven_low);
    puts("PASS: long preemption inside GPIO write cannot hide an overlong start low");
    for(unsigned int i=0;i<2U;++i)
    {
        Reset(); assert(BSP_DHT11_Init()==BSP_DHT11_OK);
        final_read_delay_us=i==0U ? 200U:6000U;
        memset(out,0xA5,5);
        assert(Capture(20000U,out)==BSP_DHT11_TIMEOUT && out[0]==0xA5 && !driven_low);
    }
    puts("PASS: final GPIO read interrupted past edge/frame deadlines cannot publish a late frame");
    Reset(); assert(BSP_DHT11_Init()==BSP_DHT11_OK); memset(wire,0xFF,5); one_us=100U;
    memset(out,0xA5,5);
    assert(Capture(20000U,out)==BSP_DHT11_TIMEOUT && out[0]==0xA5 && !driven_low);
    puts("PASS: individually permitted slow bits cannot extend capture beyond the global deadline");
    return 0;
}
