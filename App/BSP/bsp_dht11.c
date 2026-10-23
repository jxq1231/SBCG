/**
 * @file        bsp_dht11.c
 * @brief       DHT11 开漏启动脉冲和基于 DWT 的五字节波形捕获。
 * @details     先验证 CubeMX GPIO，启用但不清零 DWT 周期计数；前台分离 Start 与 Capture，以边沿和整帧预算检查
 *          响应/数据脉宽，成功后输出原始帧。
 * @note        使用 DHT11_DATA（当前 PG7）与外部上拉；不改 GPIO 模式、TIM、NVIC 或 PTP。中断保持开启，长抢占
 *          可使捕获失败；校验和及数值语义由温度模块处理。
 */

#include "bsp_dht11.h"
#include "main.h"
#include "Temperature/temperature_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifndef BSP_DHT11_READ_CYCLES
/**
 * @brief 读取 DWT 原始周期计数；原生波形测试可替换此宏。
 * @return 当前 uint32_t 周期值，自然回绕，不重置计数。
 * @note 依赖 Init 启用 CYCCNT；仅读取寄存器，不产生延时或清标志副作用。
 */
#define BSP_DHT11_READ_CYCLES() (DWT->CYCCNT)
#endif

/* DWT is a CPU timing resource here, not a measurement/synchronization clock.
   Do not reset CYCCNT: other debugger/core observations may be using it. */
#define DWT_UNLOCK_KEY UINT32_C(0xC5ACCE55)
#define CLOCK_STALL_POLLS 128U
static bool ready, started;
static uint32_t cycles_per_us, low_earliest_cycle, low_latest_cycle, frame_started_cycle;

/**
 * @brief   只读检查 DHT11 引脚是否为单引脚、开漏输出且无内部上下拉。
 * @return  true 表示实际 GPIO 寄存器符合本模块使用条件；false 表示拒绝初始化。
 * @note    依赖 CubeMX 生成的 DHT11_DATA 映射，不改 GPIO 模式；外部上拉是否存在不能由此检查证明。
 */
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

/**
 * @brief   验证已生成 GPIO 与内核频率并启用 DWT 周期计数。
 * @return  OK 表示引脚/时钟约束成立且 CYCCNT 在有限轮询内递增；其它为初始化或计数时钟错误。
 * @note    仅前台、GPIO 初始化后调用；释放开漏线，按整数 MHz 换算 cycles_per_us，不重置 CYCCNT，不重配
 *          GPIO/TIM/PTP。后续脉宽判定依赖核心频率保持一致。
 */
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

/**
 * @brief   释放 DHT11 开漏数据线并清除启动脉冲状态。
 * @return  无返回值。
 * @note    仅前台使用；ready 或 started 时将输出置高以释放线路，否则仅清 started。不中断已进入 Capture 的同步
 *          执行。
 */
void BSP_DHT11_Abort(void)
{
    if(ready || started)
    { HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port,DHT11_DATA_Pin,GPIO_PIN_SET); }
    started=false;
}

/**
 * @brief   检查空闲线路并开始 DHT11 主机低电平启动脉冲。
 * @return  OK 表示已拉低；未初始化或线路忙时返回相应错误，线路忙会调用 Abort。
 * @note    仅前台；记录 GPIO 写入前后的 DWT 周期边界，函数不等待 18..30 ms，由温度服务稍后调用 Capture。
 */
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
/**
 * @brief   轮询等待目标数据电平并检查单边沿及整帧超时。
 * @param   level 期望观测到的 GPIO 电平。
 * @param   edge_cycle 必须非空的输出周期计数指针；成功写入采样时的 DWT 周期，失败保持原值，不保留指针。
 * @return  OK 表示观测到目标电平；TIMEOUT 表示单边沿或整帧到期；CLOCK_ERROR 表示连续轮询计数停滞。
 * @note    Capture 的前台同步路径；中断保持开启，因此长抢占可导致下一次周期检查失败。无符号周期差允许一次计数
 *          回绕，协议窗口由 temperature_config.h 给出。
 */
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

/**
 * @brief   按当前 cycles_per_us 判断脉宽是否位于闭区间。
 * @param   start 起始 DWT 周期计数。
 * @param   end 结束 DWT 周期计数，使用 uint32_t 差处理回绕。
 * @param   min_us 允许的最小宽度，单位微秒。
 * @param   max_us 允许的最大宽度，单位微秒。
 * @return  true 表示脉宽在给定范围内；false 表示超出。
 * @note    仅在初始化成功后的帧捕获路径使用；周期计数并非同步时间戳。
 */
static bool WidthValid(uint32_t start,uint32_t end,uint32_t min_us,uint32_t max_us)
{
    const uint32_t cycles=(uint32_t)(end-start);
    return cycles>=min_us*cycles_per_us && cycles<=max_us*cycles_per_us;
}

/**
 * @brief   按响应与 40 位脉冲宽度解码 DHT11 原始五字节帧。
 * @param   frame 必须非空且至少五字节的调用者输出；完整波形成功后才复制，失败保持原内容。
 * @return  OK 表示波形及尾部低脉冲合格；其它返回等待、时钟或脉宽错误。
 * @note    前台同步执行，使用局部 received 暂存并按高位在前组装；此处不做校验和、温湿度范围或小数语义检查，交
 *          给上层帧解析。
 */
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

/**
 * @brief   在有效启动低电平窗口释放线路并同步捕获完整 DHT11 波形。
 * @param   frame 调用者拥有的五字节输出；NULL 返回 ARGUMENT 并 Abort。失败不改帧数据，成功输出原始五字节。
 * @return  OK 表示波形捕获完成；其它区分状态、18..30 ms 窗口、超时、计数停滞和脉宽错误。
 * @note    仅前台，须先成功 Start；中断保持开启，单边沿/整帧预算分别为 150 us/6000 us，长 IRQ 抢占仍会延长实
 *          际返回耗时。所有退出路径调用 Abort，已初始化或持有线路时释放数据线，不保证数据校验通过。
 */
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
