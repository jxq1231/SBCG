/**
 * @file        app_main.c
 * @brief       协调单板网络、时间观测、温度与音频服务的初始化和前台调度。
 * @details     向各服务提供有界串口日志出口，按既定顺序初始化与轮询；
 *              DHT11起始低电平期间暂存日志并延后音频等观测，继续处理网络。
 * @note        由Core/Src/main.c在CubeMX外设初始化后调用；不创建RTOS任务，
 *              不承担IRQ处理、GUI绘制或双板校时。
 */
#include "app_main.h"
#include "BSP/bsp_console.h"
#include "Network/ethernet_monitor.h"
#include "Temperature/temperature_service.h"
#include "Console/temperature_console.h"
#include "Audio/codec_monitor.h"
#ifdef SBCG_USE_AUDIO
#include "Audio/audio_capture.h"
#endif
#ifdef SBCG_USE_AUDIO_ANCHOR
#include "Audio/audio_time_anchor.h"
#endif
#ifdef SBCG_USE_AUDIO_MAP
#include "Audio/audio_time_map_monitor.h"
#endif
#ifdef SBCG_USE_LWIP
#include "lwip.h"
#include "Network/udp_heartbeat.h"
#include "Network/udp_probe.h"
#include "Network/udp_time_exchange.h"
#include "Sync/ptp_clock.h"
#include "Sync/ptp_rx.h"
#include "Sync/ptp_tx.h"
#include "Sync/sync_observation.h"
#endif

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/**
 * ==================== 模块：构建标识与前台日志状态 ====================
 * 作用：按编译功能组合标识固件，维护心跳与16行、单行256字节的日志队列。
 * 调用关系：下方初始化、日志回调及前台排空共用；IRQ不访问该队列。
 */
#if defined(SBCG_USE_AUDIO_MAP)
#define APP_FIRMWARE_ID "SBCG-M3.2-MAP"
#elif defined(SBCG_USE_AUDIO_ANCHOR)
#define APP_FIRMWARE_ID "SBCG-M3.2-ANCHOR"
#elif defined(SBCG_USE_AUDIO)
#define APP_FIRMWARE_ID "SBCG-M3.2-INDEX"
#elif defined(SBCG_USE_LWIP)
#define APP_FIRMWARE_ID "SBCG-M3.0-CODEC"
#else
#define APP_FIRMWARE_ID "SBCG-M1.1"
#endif
#define APP_HEARTBEAT_PERIOD_MS 1000U
#define APP_LOG_LINE_CAPACITY 256U
#define APP_LOG_QUEUE_CAPACITY 16U
/* BOOT/heartbeat remain short; allow longer service logs without larger locals. */
#define APP_STATUS_LINE_CAPACITY 160U

static uint32_t last_heartbeat_ms;
static uint32_t heartbeat_seq;
/* Also observable in a debugger if the UART cannot send any text. */
static volatile uint32_t app_tx_error_count;
/* UART is synchronous. Preserve legacy logs while the nonblocking DHT start
   pulse is active, without pausing Ethernet/PTP processing or changing IRQs. */
static char pending_lines[APP_LOG_QUEUE_CAPACITY][APP_LOG_LINE_CAPACITY];
static uint16_t pending_lengths[APP_LOG_QUEUE_CAPACITY];
static uint32_t pending_head, pending_count;

/**
 * @brief   在DHT起始低电平结束后，尝试发送队首的一行日志。
 * @return  无返回值（void）。
 * @note    输入来自本模块待发队列；无数据或start-low期间不发送。
 *          串口写失败累计错误但仍移除该行，每次调用最多处理一行。
 */
static void FlushOneLine(void)
{
    if (pending_count == 0U || TemperatureService_IsStartLowActive()) { return; }
    if (!BSP_Console_Write(pending_lines[pending_head], pending_lengths[pending_head]))
    {
        ++app_tx_error_count;
    }
    pending_head = (pending_head + 1U) % APP_LOG_QUEUE_CAPACITY;
    --pending_count;
}

/**
 * @brief   直接发送服务日志，或在温度时序保护/队列未排空时复制到有界队列。
 * @param   line 有效ASCII日志缓冲区；由调用者持有，必要时本函数同步复制。
 * @param   length 待发字节数，合法范围1～255，不包括字符串结尾的空字符。
 * @return  无返回值（void）。
 * @note    各服务以前台回调方式调用；长度非法、队满或串口失败累计TX_ERRORS。
 *          此处未检查line是否为NULL，调用方必须保证长度范围内的内存有效。
 */
static void SendLine(const char *line, int length)
{
    if ((length <= 0) || ((size_t)length >= APP_LOG_LINE_CAPACITY))
    {
        ++app_tx_error_count;
        return;
    }

    if (TemperatureService_IsStartLowActive() || pending_count != 0U)
    {
        if (pending_count == APP_LOG_QUEUE_CAPACITY)
        {
            ++app_tx_error_count;
            return;
        }
        const uint32_t tail = (pending_head + pending_count) % APP_LOG_QUEUE_CAPACITY;
        memcpy(pending_lines[tail], line, (size_t)length);
        pending_lengths[tail] = (uint16_t)length;
        ++pending_count;
        return;
    }

    if (!BSP_Console_Write(line, (uint16_t)length))
    {
        ++app_tx_error_count;
    }
}

/**
 * ==================== 模块：应用初始化 ====================
 * 作用：发布BOOT并建立网络/PTP、温度、音频观测和串口命令入口。
 * 调度顺序：外设生成初始化之后执行；先拆外层网络包装，再按依赖顺序安装。
 * 调用关系：main调用一次；保留既有重复初始化保护，不在这里改外设配置。
 */
/**
 * @brief   读取板卡身份并按依赖顺序初始化当前构建启用的应用服务。
 * @return  无返回值（void）。
 * @note    依赖已初始化的UART/I2C/LwIP/SAI等资源；服务通过SendLine报告状态。
 *          UDPTimeExchange外层包装所有权不能安全拆除时立即返回，避免覆盖别人的钩子。
 */
void App_Init(void)
{
#ifdef SBCG_USE_LWIP
    /* Remove the outer wrapper before the existing observers capture pointers. */
    if (!UDPTimeExchange_Deinit())
    {
        static const char error[] = "$TS_INIT,RESULT=OWNERSHIP_ERROR\r\n";
        SendLine(error, (int)(sizeof(error) - 1U));
        return;
    }
#endif
    BSP_ConsoleInfo info;
    char line[APP_STATUS_LINE_CAPACITY];

    heartbeat_seq = 0U;
    app_tx_error_count = 0U;
    pending_head = pending_count = 0U;
    BSP_Console_GetInfo(&info);
    const int length = snprintf(line, sizeof(line),
        "$BOOT,FW=" APP_FIRMWARE_ID ",HCLK_HZ=%" PRIu32
        ",UID_W0=%08" PRIX32 ",UID_W1=%08" PRIX32 ",UID_W2=%08" PRIX32 "\r\n",
        info.core_clock_hz, info.uid_words[0], info.uid_words[1], info.uid_words[2]);
    SendLine(line, length);
    last_heartbeat_ms = BSP_Console_GetTickMs();
    EthernetMonitor_Init(last_heartbeat_ms, SendLine);
#ifdef SBCG_USE_LWIP
    UDPHeartbeat_Init(last_heartbeat_ms, info.uid_words, SendLine);
    UDPProbe_Init(last_heartbeat_ms, info.uid_words, SendLine);
    PtpClock_Init(BSP_Console_GetTickMs(), SendLine);
    PtpRx_Init(BSP_Console_GetTickMs(), SendLine);
    PtpTx_Init(BSP_Console_GetTickMs(), info.uid_words, SendLine);
    SyncObservation_Init(BSP_Console_GetTickMs(), SendLine);
    if (!UDPTimeExchange_Init(BSP_Console_GetTickMs(), info.uid_words,
                             BSP_Console_GetTickMs, SendLine))
    {
        static const char error[] = "$TS_INIT,RESULT=INIT_ERROR\r\n";
        SendLine(error, (int)(sizeof(error) - 1U));
    }
#endif
    TemperatureService_Init(BSP_Console_GetTickMs(), info.uid_words, SendLine);
    TemperatureConsole_Init(SendLine);
#ifdef SBCG_USE_AUDIO
    AudioCapture_Init(BSP_Console_GetTickMs(), SendLine);
#endif
#ifdef SBCG_USE_AUDIO_ANCHOR
    AudioTimeAnchor_Init(BSP_Console_GetTickMs(), SendLine);
#endif
#ifdef SBCG_USE_AUDIO_MAP
    AudioTimeMapMonitor_Init(SendLine);
#endif
    CodecMonitor_Init(BSP_Console_GetTickMs(), SendLine);
    if (!BSP_Console_RxInit())
    {
        static const char error[] = "$CONSOLE,INIT=ERROR\r\n";
        SendLine(error, (int)sizeof error - 1);
    }
    FlushOneLine();
}

/**
 * ==================== 模块：主循环协同调度 ====================
 * 作用：先处理LwIP收包/定时器/链路，再执行网络和本地时钟观测、温度与音频服务。
 * 调度顺序：温度服务先更新当前DATA状态，后续模块据此门控，末尾最多排空一行日志。
 * 调用关系：由main的while(1)反复调用；各服务自行节流，不代表固定频率任务。
 */
/**
 * @brief   推进一轮前台处理、周期状态输出及延后日志发送。
 * @return  无返回值（void）。
 * @note    输入为HAL毫秒计数、网络报文、IRQ队列及各服务状态；主循环周期取决于
 *          实际处理耗时。网络RAW回调在本轮LwIP处理内同步执行，音频/UART IRQ可抢占前台。
 */
void App_Process(void)
{
#ifdef SBCG_USE_LWIP
    /* Every loop, before logs: service RX, protocol timers and link changes. */
    MX_LWIP_Process();
#endif
    const uint32_t now_ms = BSP_Console_GetTickMs();
#ifdef SBCG_USE_LWIP
    UDPHeartbeat_Process(now_ms);
#endif
    if ((uint32_t)(now_ms - last_heartbeat_ms) >= APP_HEARTBEAT_PERIOD_MS)
    {
        /* Skip missed periods instead of flooding after a delayed loop. */
        last_heartbeat_ms = now_ms;
        ++heartbeat_seq;
        char line[APP_STATUS_LINE_CAPACITY];
        const int length = snprintf(line, sizeof(line),
            "$HEARTBEAT,SEQ=%" PRIu32 ",TICK_MS=%" PRIu32 ",TX_ERRORS=%" PRIu32 "\r\n",
            heartbeat_seq, now_ms, (uint32_t)app_tx_error_count);
        SendLine(line, length);
    }
    EthernetMonitor_Process(BSP_Console_GetTickMs());
#ifdef SBCG_USE_LWIP
    UDPProbe_Process(BSP_Console_GetTickMs());
    PtpClock_Process(BSP_Console_GetTickMs());
    PtpRx_Process(BSP_Console_GetTickMs());
    PtpTx_Process(BSP_Console_GetTickMs());
    UDPTimeExchange_Process(BSP_Console_GetTickMs());
    SyncObservation_Process(BSP_Console_GetTickMs());
#endif
    TemperatureService_Process(BSP_Console_GetTickMs());
    TemperatureConsole_Process(BSP_Console_GetTickMs());
    CodecMonitor_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#ifdef SBCG_USE_AUDIO
    AudioCapture_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#endif
#ifdef SBCG_USE_AUDIO_ANCHOR
    AudioTimeAnchor_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#endif
#ifdef SBCG_USE_AUDIO_MAP
    AudioTimeMapMonitor_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#endif
    FlushOneLine();
}
