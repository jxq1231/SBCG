/**
 * @file        bsp_console.h
 * @brief       USART1 控制台同步发送及中断接收队列适配。
 * @details     声明系统信息、同步发送、字节流事件及接收统计接口；core_clock_hz 单位 Hz，UID 为三个原始字，错
 *          误统计为饱和 uint32_t 计数。
 * @note        依赖 CubeMX 已初始化的 huart1 和 HAL tick；发送禁止 ISR，接收无需 DMA。环形数组 256 字节，其中
 *          一个槽留空，实际可排队 255 字节。
 */

#ifndef SBCG_BSP_CONSOLE_H
#define SBCG_BSP_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 系统 HCLK 频率（Hz）与三个原始 UID 字，供前台日志和设备识别使用。 */
typedef struct
{
    uint32_t core_clock_hz;
    uint32_t uid_words[3];
} BSP_ConsoleInfo;

/**
 * @brief   读取现有 HAL 系统毫秒计数。
 * @return  HAL_GetTick 的 uint32_t 毫秒值，会自然回绕。
 * @note    供前台调度和模块 tick 回调使用；不是 PTP 纳秒时间，也不更改 SysTick 配置。
 */
uint32_t BSP_Console_GetTickMs(void);
/**
 * @brief   读取 HCLK 频率与三个 MCU 唯一标识字。
 * @param   info 调用者拥有的输出；必须非空，函数无空指针检查。core_clock_hz 单位 Hz，uid_words 保留三个原始
 *          32 位 UID 字。
 * @return  无返回值。
 * @note    App_Init 在生成初始化之后读取，用于启动日志和 UID 策略；不配置系统时钟。
 */
void BSP_Console_GetInfo(BSP_ConsoleInfo *info);
/* Main context only: returns false on HAL failure; no internal retry. */
/**
 * @brief   通过控制台 UART 同步发送给定字节序列。
 * @param   data 调用者拥有的只读发送缓冲；须满足 HAL_UART_Transmit 的有效指针要求，返回前保持有效，函数不保存
 *          指针。
 * @param   length 要发送的字节数，uint16_t；不会自动补换行或 NUL。
 * @return  true 表示 HAL_UART_Transmit 返回 HAL_OK；false 表示 HAL 失败，不内部重试。
 * @note    仅主循环，禁止 ISR；25 ms 为 HAL 传输参数，依赖运行中的 HAL tick。同步调用返回后无 DMA/IRQ 持有缓
 *          冲，上层负责记录失败和重试策略。
 */
bool BSP_Console_Write(const char *data, uint16_t length);

/** @brief 前台读结果：NONE 无字节、BYTE 输出字节、GAP 丢失并清空排队字节。 */
typedef enum
{
    BSP_CONSOLE_RX_NONE,
    BSP_CONSOLE_RX_BYTE,
    BSP_CONSOLE_RX_GAP
} BSP_ConsoleRxResult;

/** @brief 从最近 RxInit 起累计的饱和错误、队列溢出和重挂失败计数。 */
typedef struct
{
    uint32_t errors;
    uint32_t overflows;
    uint32_t rearm_errors;
} BSP_ConsoleRxStats;

/* Main context only. GAP invalidates the partial command and all queued bytes.
 * HAL callbacks only enqueue bytes/mark loss/rearm; no parsing or UART TX. */
/**
 * @brief   清空控制台接收状态并启动单字节 UART 中断接收。
 * @return  true 表示终止旧接收且重新挂接成功；false 表示终止或挂接失败。
 * @note    仅主循环初始化路径；短 PRIMASK 区保留原屏蔽状态。Abort 失败时不启用重试，挂接失败时已启用，后续
 *          RxProcess 可重试。
 */
bool BSP_Console_RxInit(void);
/**
 * @brief   在前台按 HAL 毫秒计数重试未挂接的控制台接收。
 * @return  无返回值。
 * @note    主循环重复调用，以无符号差值处理 tick 回绕，每隔至少 10 ms 检查一次；只有 rx_enabled 且未挂接时重
 *          试，不解析命令。
 */
void BSP_Console_RxProcess(void);
/**
 * @brief   从中断生产、前台消费的控制台环形队列取一个字节或丢失事件。
 * @param   out 调用者拥有的单字节输出；NULL 返回 NONE。仅返回 BYTE 时写入，其余情况保持原值。
 * @return  BYTE 表示有效字节；GAP 表示流丢失且已丢弃全部排队字节；NONE 表示无可读字节。
 * @note    仅主循环调用；返回 GAP 后上层应放弃未完成命令，防止跨丢失拼接。队列操作使用保存/恢复 PRIMASK 的短
 *          临界区。
 */
BSP_ConsoleRxResult BSP_Console_Read(uint8_t *out);
/**
 * @brief   复制控制台接收错误、溢出和重挂失败计数。
 * @param   out 调用者拥有的输出结构；NULL 时直接返回，不清零。
 * @return  无返回值。
 * @note    仅前台读取；复制时短暂屏蔽中断，计数为饱和值，不在此清除。
 */
void BSP_Console_GetRxStats(BSP_ConsoleRxStats *out);

#endif
