/**
 * @file        bsp_console.c
 * @brief       USART1 控制台同步发送及中断接收队列适配。
 * @details     前台同步发送日志；USART1 IRQ 将单字节写入环形队列并标记丢失，前台读取 BYTE/GAP/NONE 和统计，再
 *          交给命令层解析。
 * @note        依赖 CubeMX 已初始化的 huart1 和 HAL tick；发送禁止 ISR，接收无需 DMA。环形数组 256 字节，其中
 *          一个槽留空，实际可排队 255 字节。
 */

#include "bsp_console.h"
#include "main.h"
#include <stddef.h>

/* Only this adapter knows the physical UART chosen in CubeMX. */
extern UART_HandleTypeDef huart1;
#define CONSOLE_UART_HANDLE huart1
#define CONSOLE_TX_TIMEOUT_MS 25U
#define CONSOLE_RX_CAPACITY 256U
#define CONSOLE_RX_RETRY_MS 10U

/* Single IRQ producer / main consumer. Brief PRIMASK sections preserve the
 * caller's mask and protect pop/flush/rearm, never a UART wait or TX operation.
 * Volatile slots and indices publish each byte in order; no DMA owns these. */
static volatile uint8_t rx_bytes[CONSOLE_RX_CAPACITY];
static volatile uint32_t rx_head, rx_tail;
static volatile bool rx_lost, rx_armed, rx_enabled;
static uint8_t rx_byte;
static uint32_t rx_retry_ms;
static volatile BSP_ConsoleRxStats rx_stats;

/**
 * @brief   将控制台统计计数饱和递增，避免回绕。
 * @param   counter 必须非空的模块内 volatile 计数器；原地更新，不转移所有权。
 * @return  无返回值。
 * @note    由 UART IRQ 或屏蔽中断的前台路径调用；UINT32_MAX 时保持不变。
 */
static void saturate(volatile uint32_t *counter)
{
    if (*counter != UINT32_MAX) { ++*counter; }
}

/**
 * @brief   给控制台 UART 重新挂接一个字节的中断接收。
 * @return  true 表示 HAL 接收已挂接；false 会累计重挂失败并标记字节流丢失。
 * @note    由 UART 回调或受保护的前台路径调用；HAL 暂借模块静态 rx_byte，完成后回调入队，不使用 DMA。
 */
static bool arm_receive(void)
{
    rx_armed = HAL_UART_Receive_IT(&CONSOLE_UART_HANDLE, &rx_byte, 1U) == HAL_OK;
    if (!rx_armed)
    {
        saturate(&rx_stats.rearm_errors);
        rx_lost = true;
    }
    return rx_armed;
}

/**
 * @brief   清空控制台接收状态并启动单字节 UART 中断接收。
 * @return  true 表示终止旧接收且重新挂接成功；false 表示终止或挂接失败。
 * @note    仅主循环初始化路径；短 PRIMASK 区保留原屏蔽状态。Abort 失败时不启用重试，挂接失败时已启用，后续
 *          RxProcess 可重试。
 */
bool BSP_Console_RxInit(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    rx_enabled = rx_armed = false;
    rx_head = rx_tail = 0U;
    rx_lost = false;
    rx_stats.errors = rx_stats.overflows = rx_stats.rearm_errors = 0U;
    rx_retry_ms = HAL_GetTick();
    bool ok = HAL_UART_AbortReceive(&CONSOLE_UART_HANDLE) == HAL_OK;
    if (ok) { rx_enabled = true; ok = arm_receive(); }
    else { saturate(&rx_stats.errors); }
    __set_PRIMASK(mask);
    return ok;
}

/**
 * @brief   在前台按 HAL 毫秒计数重试未挂接的控制台接收。
 * @return  无返回值。
 * @note    主循环重复调用，以无符号差值处理 tick 回绕，每隔至少 10 ms 检查一次；只有 rx_enabled 且未挂接时重
 *          试，不解析命令。
 */
void BSP_Console_RxProcess(void)
{
    uint32_t now_ms = HAL_GetTick();
    if ((uint32_t)(now_ms - rx_retry_ms) < CONSOLE_RX_RETRY_MS) { return; }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    rx_retry_ms = now_ms;
    if (rx_enabled && !rx_armed) { (void)arm_receive(); }
    __set_PRIMASK(mask);
}

/**
 * @brief   从中断生产、前台消费的控制台环形队列取一个字节或丢失事件。
 * @param   out 调用者拥有的单字节输出；NULL 返回 NONE。仅返回 BYTE 时写入，其余情况保持原值。
 * @return  BYTE 表示有效字节；GAP 表示流丢失且已丢弃全部排队字节；NONE 表示无可读字节。
 * @note    仅主循环调用；返回 GAP 后上层应放弃未完成命令，防止跨丢失拼接。队列操作使用保存/恢复 PRIMASK 的短
 *          临界区。
 */
BSP_ConsoleRxResult BSP_Console_Read(uint8_t *out)
{
    if (out == NULL) { return BSP_CONSOLE_RX_NONE; }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    BSP_ConsoleRxResult result = BSP_CONSOLE_RX_NONE;
    if (rx_lost)
    {
        rx_tail = rx_head;
        rx_lost = false;
        result = BSP_CONSOLE_RX_GAP;
    }
    else if (rx_tail != rx_head)
    {
        *out = rx_bytes[rx_tail];
        rx_tail = (rx_tail + 1U) % CONSOLE_RX_CAPACITY;
        result = BSP_CONSOLE_RX_BYTE;
    }
    __set_PRIMASK(mask);
    return result;
}

/**
 * @brief   复制控制台接收错误、溢出和重挂失败计数。
 * @param   out 调用者拥有的输出结构；NULL 时直接返回，不清零。
 * @return  无返回值。
 * @note    仅前台读取；复制时短暂屏蔽中断，计数为饱和值，不在此清除。
 */
void BSP_Console_GetRxStats(BSP_ConsoleRxStats *out)
{
    if (out == NULL) { return; }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    *out = rx_stats;
    __set_PRIMASK(mask);
}

/**
 * @brief   把控制台 UART 完成的单字节入队并安排下一次接收。
 * @param   uart HAL 提供的借用 UART 句柄；只处理已启用的控制台 huart1，其余忽略。
 * @return  无返回值。
 * @note    USART1 IRQ 上下文；有 HAL 错误时标记丢失并留给 ErrorCallback 处理，避免重挂清除 ErrorCode。队列满
 *          时记录溢出；不解析命令、不发送日志。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart != &CONSOLE_UART_HANDLE || !rx_enabled) { return; }
    rx_armed = false;
    /* HAL invokes this callback before ErrorCallback for RXNE+error. Do not
     * rearm here: Receive_IT clears ErrorCode and would hide HAL's ORE abort. */
    if (uart->ErrorCode != HAL_UART_ERROR_NONE) { rx_lost = true; return; }
    if (!rx_lost)
    {
        uint32_t next = (rx_head + 1U) % CONSOLE_RX_CAPACITY;
        if (next == rx_tail)
        { saturate(&rx_stats.overflows); rx_lost = true; }
        else { rx_bytes[rx_head] = rx_byte; rx_head = next; }
    }
    (void)arm_receive();
}

/**
 * @brief   记录控制台字节流中断并按 HAL 接收状态尝试重新挂接。
 * @param   uart HAL 提供的借用 UART 句柄；只处理已启用的控制台 huart1，其余忽略。
 * @return  无返回值。
 * @note    USART1 IRQ 上下文；依据 RxState 判断原单字节事务是否仍活动，错误使前台收到 GAP。失败重挂由
 *          RxProcess 在前台重试。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != &CONSOLE_UART_HANDLE || !rx_enabled) { return; }
    saturate(&rx_stats.errors);
    rx_lost = true;
    /* Blocking ORE ends reception before this callback. For a nonblocking
     * error without RXNE the existing one-byte transfer is still active. */
    rx_armed = uart->RxState != HAL_UART_STATE_READY;
    if (!rx_armed) { (void)arm_receive(); }
}

/**
 * @brief   读取现有 HAL 系统毫秒计数。
 * @return  HAL_GetTick 的 uint32_t 毫秒值，会自然回绕。
 * @note    供前台调度和模块 tick 回调使用；不是 PTP 纳秒时间，也不更改 SysTick 配置。
 */
uint32_t BSP_Console_GetTickMs(void)
{
    return HAL_GetTick();
}

/**
 * @brief   读取 HCLK 频率与三个 MCU 唯一标识字。
 * @param   info 调用者拥有的输出；必须非空，函数无空指针检查。core_clock_hz 单位 Hz，uid_words 保留三个原始
 *          32 位 UID 字。
 * @return  无返回值。
 * @note    App_Init 在生成初始化之后读取，用于启动日志和 UID 策略；不配置系统时钟。
 */
void BSP_Console_GetInfo(BSP_ConsoleInfo *info)
{
    info->core_clock_hz = HAL_RCC_GetHCLKFreq();
    info->uid_words[0] = HAL_GetUIDw0();
    info->uid_words[1] = HAL_GetUIDw1();
    info->uid_words[2] = HAL_GetUIDw2();
}

/**
 * @brief   通过控制台 UART 同步发送给定字节序列。
 * @param   data 调用者拥有的只读发送缓冲；须满足 HAL_UART_Transmit 的有效指针要求，返回前保持有效，函数不保存
 *          指针。
 * @param   length 要发送的字节数，uint16_t；不会自动补换行或 NUL。
 * @return  true 表示 HAL_UART_Transmit 返回 HAL_OK；false 表示 HAL 失败，不内部重试。
 * @note    仅主循环，禁止 ISR；25 ms 为 HAL 传输参数，依赖运行中的 HAL tick。同步调用返回后无 DMA/IRQ 持有缓
 *          冲，上层负责记录失败和重试策略。
 */
bool BSP_Console_Write(const char *data, uint16_t length)
{
    /* HAL consumes the stack buffer before returning. No DMA/IRQ owns it.
       At 115200 8N1, a 255-byte line takes less than 23 ms.
       The timeout relies on the existing SysTick; never call from an ISR. */
    return HAL_UART_Transmit(&CONSOLE_UART_HANDLE, (const uint8_t *)data,
                             length, CONSOLE_TX_TIMEOUT_MS) == HAL_OK;
}
