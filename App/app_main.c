/**
 * @file        app_main.c
 * @brief       协调单板网络、时间观测、温度与音频服务的初始化和前台调度。
 * @details     向各服务提供有界串口日志出口，按既定顺序初始化与轮询；
 *              DHT11起始低电平期间暂存日志并延后音频等观测，继续处理网络。
 * @note        由Core/Src/main.c在CubeMX外设初始化后调用；不创建RTOS任务，
 *              不承担IRQ处理；维护前台日志队列及分类诊断，页面/校时/PPS交由各独立服务。
 */
#include "app_main.h"
#ifdef SBCG_USE_RESET_DIAGNOSTIC
#include "BSP/bsp_reset.h"
#endif
#ifdef SBCG_USE_TOUCHGFX
#include "UI/touchgfx_service.h"
#include "BSP/bsp_touchgfx_display.h"
#elif defined(SBCG_USE_LCD)
#include "UI/status_service.h"
#endif
#ifdef SBCG_USE_SDRAM
#include "BSP/bsp_sdram.h"
#endif
#ifdef SBCG_USE_SOUND_TIME
#include "Detector/sound_event_time.h"
#include "Network/udp_sound_event.h"
#include "Measure/measurement_result.h"
#endif
#ifdef SBCG_USE_PPS
#include "Sync/pps_service.h"
#endif
#include "BSP/bsp_console.h"
#ifdef SBCG_USE_BOARD_SYNC
#include "Sync/time_sync.h"
#endif
#ifdef SBCG_USE_BOARD_IDENTITY
#include "BSP/bsp_board_identity.h"
#endif
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
#ifdef SBCG_USE_CHIRP
#include "Detector/chirp_service.h"
#endif
#ifdef SBCG_USE_LWIP
#include "lwip.h"
#include "Network/udp_heartbeat.h"
#include "Network/udp_probe.h"
#include "Network/udp_time_exchange.h"
#include "Network/udp_chirp_capture.h"
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
 * 作用：按编译功能组合标识固件，维护心跳与有界日志队列；常规16行，事件时间版本32行，每行256字节。
 * 调用关系：下方初始化、日志回调及前台排空共用；IRQ不访问该队列。
 */
#if defined(SBCG_USE_CHIRP)
#define APP_FIRMWARE_ID "SBCG-M4.2-PCMCAP"
#elif defined(SBCG_USE_TOUCHGFX) && defined(SBCG_USE_RESET_DIAGNOSTIC)
#define APP_FIRMWARE_ID "SBCG-U3.1-RESET"
#elif defined(SBCG_USE_TOUCHGFX)
#define APP_FIRMWARE_ID "SBCG-U3.1-TOUCHGFX"
#elif defined(SBCG_USE_LCD)
#define APP_FIRMWARE_ID "SBCG-U2-STATUS-R2"
#elif defined(SBCG_USE_SDRAM)
#define APP_FIRMWARE_ID "SBCG-U1-SDRAM-R1"
#elif defined(SBCG_USE_SOUND_TIME)
#define APP_FIRMWARE_ID "SBCG-M4.1-TIME-R1"
#elif defined(SBCG_USE_SOUND_DETECTOR)
#define APP_FIRMWARE_ID "SBCG-M4.1-CAND-R1"
#elif defined(SBCG_USE_BOARD_SYNC)
#ifdef SBCG_USE_PPS
#define APP_FIRMWARE_ID "SBCG-M2.3-PPS"
#else
#define APP_FIRMWARE_ID "SBCG-M2.3-SYNC"
#endif
#elif defined(SBCG_USE_BOARD_IDENTITY)
#define APP_FIRMWARE_ID "SBCG-M2.3-ROLE"
#elif defined(SBCG_USE_AUDIO_MAP)
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
/* Diagnostic-only common event domain until the synchronized time-domain service exists.
   Both boards use the same constant, so this is NOT a claim of PTP synchronization. */
#define APP_SOUND_EVENT_DIAGNOSTIC_DOMAIN 1U
#define APP_LOG_LINE_CAPACITY 256U
#ifdef SBCG_USE_SOUND_TIME
/* 新观察行会增加周期日志突发；复用原队列，逐轮只发一行，避免占满DMA半块窗口。 */
#define APP_LOG_QUEUE_CAPACITY 32U
#else
#define APP_LOG_QUEUE_CAPACITY 16U
#endif
/* 诊断心跳含日志原因与队列状态，满宽uint32字段仍须完整放入同一行。 */
#ifdef SBCG_USE_CHIRP
#define APP_STATUS_LINE_CAPACITY APP_LOG_LINE_CAPACITY
#else
#define APP_STATUS_LINE_CAPACITY 160U
#endif

static uint32_t last_heartbeat_ms;
static uint32_t heartbeat_seq;
/* CHIRP构建：迄今最大前台循环总时长及同一轮AudioCapture/UI/其余耗时，仅诊断。 */
#ifdef SBCG_USE_CHIRP
static uint32_t max_loop_ms,max_loop_audio_ms,max_loop_ui_ms,max_loop_other_ms;
static uint32_t max_loop_flush_ms;
#ifdef SBCG_USE_AUDIO
static uint32_t last_profile_ms;
#endif
#endif
/* Also observable in a debugger if the UART cannot send any text. */
static volatile uint32_t app_tx_error_count;
/* 与原TX_ERRORS相同的uint32自然回绕累计；队列深度和峰值单位为行。 */
static uint32_t log_full_count,log_write_count,log_bad_count,log_high_water;
/* UART is synchronous. Preserve legacy logs while the nonblocking DHT start
   pulse is active, without pausing Ethernet/PTP processing or changing IRQs. */
static char pending_lines[APP_LOG_QUEUE_CAPACITY][APP_LOG_LINE_CAPACITY];
static uint16_t pending_lengths[APP_LOG_QUEUE_CAPACITY];
static uint32_t pending_head, pending_count;
#ifdef SBCG_USE_SDRAM
static BSP_SdramStatus sdram_reported_status;
static uint32_t sdram_last_report_ms;
#endif
#ifdef SBCG_USE_BOARD_IDENTITY
static bool app_identity_configured;
#endif

/**
 * @brief   在DHT起始低电平结束后，尝试发送队首的一行日志。
 * @return  无返回值（void）。
 * @note    输入来自本模块待发队列；无数据或start-low期间不发送。
 *          串口写失败累计TX_ERRORS和LOG_WRITE但仍移除该行，每次调用最多处理一行。
 */
static void FlushOneLine(void)
{
    if (pending_count == 0U || TemperatureService_IsStartLowActive()) { return; }
    if (!BSP_Console_Write(pending_lines[pending_head], pending_lengths[pending_head]))
    {
        ++app_tx_error_count;
        ++log_write_count;
    }
    pending_head = (pending_head + 1U) % APP_LOG_QUEUE_CAPACITY;
    --pending_count;
}

/**
 * @brief   复制或直接发送前台日志；事件时间版本统一入队，其他版本沿用温度时序保护。
 * @param   line 有效ASCII日志缓冲区；由调用者持有，必要时本函数同步复制。
 * @param   length 待发字节数，合法范围1～255，不包括字符串结尾的空字符。
 * @return  无返回值（void）。
 * @note    各服务以前台回调调用；长度非法、队满、写失败分别累计LOG_BAD/FULL/WRITE及原TX_ERRORS。
 *          此处未检查line是否为NULL，调用方必须保证长度范围内的内存有效。
 */
static void SendLine(const char *line, int length)
{
    if ((length <= 0) || ((size_t)length >= APP_LOG_LINE_CAPACITY))
    {
        ++app_tx_error_count;
        ++log_bad_count;
        return;
    }

#ifdef SBCG_USE_SOUND_TIME
    const bool enqueue = true;
#else
    const bool enqueue = TemperatureService_IsStartLowActive() || pending_count != 0U;
#endif
    if (enqueue)
    {
        if (pending_count == APP_LOG_QUEUE_CAPACITY)
        {
            ++app_tx_error_count;
            ++log_full_count;
            return;
        }
        const uint32_t tail = (pending_head + pending_count) % APP_LOG_QUEUE_CAPACITY;
        memcpy(pending_lines[tail], line, (size_t)length);
        pending_lengths[tail] = (uint16_t)length;
        ++pending_count;
        if(pending_count>log_high_water){log_high_water=pending_count;}
        return;
    }

    if (!BSP_Console_Write(line, (uint16_t)length))
    {
        ++app_tx_error_count;
        ++log_write_count;
    }
}

#ifdef SBCG_USE_BOARD_IDENTITY
/**
 * @brief   发布本次启动选定的角色与网络身份配置，未知角色明确禁用业务。
 * @param   profile 来自BSP的只读启动配置；NULL表示未配置，不保存指针。
 * @return  无返回值（void）。
 * @note    仅App_Init前台调用。地址为配置声明，真实网卡身份须由实物收包/ARP另验；
 *          SYNC始终为0，本增量尚未校时。使用有界256字节日志缓冲。
 */
static void EmitBoardIdentity(const BoardIdentityProfile *profile)
{
    if (profile == NULL)
    {
        static const char disabled[] = "$BOARD,ROLE=UNCONFIGURED,NET=DISABLED,SYNC=0\r\n";
        SendLine(disabled, (int)(sizeof disabled - 1U));
        return;
    }
    char line[APP_LOG_LINE_CAPACITY];
    const int length = snprintf(line, sizeof line,
        "$BOARD,ROLE=%s,IP=%u.%u.%u.%u,MAC=%02X:%02X:%02X:%02X:%02X:%02X,"
        "PEER_IP=%u.%u.%u.%u,SYNC=0\r\n",
        profile->role == BOARD_ROLE_A ? "A" : "B",
        (unsigned)profile->local_ip[0], (unsigned)profile->local_ip[1],
        (unsigned)profile->local_ip[2], (unsigned)profile->local_ip[3],
        (unsigned)profile->local_mac[0], (unsigned)profile->local_mac[1],
        (unsigned)profile->local_mac[2], (unsigned)profile->local_mac[3],
        (unsigned)profile->local_mac[4], (unsigned)profile->local_mac[5],
        (unsigned)profile->peer_ip[0], (unsigned)profile->peer_ip[1],
        (unsigned)profile->peer_ip[2], (unsigned)profile->peer_ip[3]);
    SendLine(line, length);
}
#endif

#ifdef SBCG_USE_SDRAM
/**
 * @brief 发布有界SDRAM诊断，测试中最多1Hz、终态只发一次。
 * @param now_ms 当前前台毫秒计数，允许回绕。
 * @return 无返回值；只读取BSP副本并调用日志队列，不访问目标SDRAM。
 */
static void ReportSdram(uint32_t now_ms)
{
    BSP_SdramSnapshot s;
    BSP_SDRAM_GetSnapshot(&s);
    if (s.status == sdram_reported_status &&
        (s.status != BSP_SDRAM_TESTING || (uint32_t)(now_ms - sdram_last_report_ms) < 1000U)) { return; }
    const char *name = "ERROR";
    switch (s.status) {
        case BSP_SDRAM_TESTING: name = "TESTING"; break;
        case BSP_SDRAM_PASS: name = "PASS"; break;
        case BSP_SDRAM_CONFIG_ERROR: name = "CONFIG_ERROR"; break;
        case BSP_SDRAM_COMMAND_ERROR: name = "COMMAND_ERROR"; break;
        case BSP_SDRAM_TIMEOUT: name = "TIMEOUT"; break;
        case BSP_SDRAM_REFRESH_ERROR: name = "REFRESH_ERROR"; break;
        case BSP_SDRAM_MEMORY_ERROR: name = "MEMORY_ERROR"; break;
        case BSP_SDRAM_READY: name = "READY"; break;
        default: break;
    }
    char line[APP_LOG_LINE_CAPACITY];
    const int length = snprintf(line, sizeof line,
        "$SDRAM,STATUS=%s,PHASE=%u,PASSES=%" PRIu32 ",CHECKED_BYTES=%" PRIu32
        ",ELAPSED_MS=%" PRIu32 ",CMD=%" PRIu32 ",ERR_OFFSET=%08" PRIX32
        ",EXPECTED=%08" PRIX32 ",ACTUAL=%08" PRIX32 "\r\n",
        name, (unsigned)s.phase, s.pass, s.verified_bytes, s.elapsed_ms, s.command_index,
        s.error_offset_bytes, s.expected, s.actual);
    SendLine(line, length);
    sdram_reported_status = s.status; sdram_last_report_ms = now_ms;
}
#endif

/**
 * ==================== 模块：应用初始化 ====================
 * 作用：发布BOOT/身份，建立网络/PTP闭环、温度、音频观测和串口命令入口。
 * 调度顺序：外设生成初始化之后执行；先拆外层网络包装，再按依赖顺序安装。
 * 调用关系：main调用一次；保留既有重复初始化保护，不在这里改外设配置。
 */
#ifdef SBCG_USE_RESET_DIAGNOSTIC
/**
 * @brief BOOT后输出本次早期捕获的原始复位状态，不推断唯一根因。
 * @return 无返回值；使用原有有界前台日志队列，未捕获明确记CAPTURED=0。
 * @note FLAGS为CSR高七位，位可同时置位；CLEAR_OK表示旧标志已清除，供下一次启动比较。
 */
static void ReportReset(void)
{
    BSP_ResetSnapshot reset = {0};
    const bool captured = BSP_Reset_GetSnapshot(&reset);
    char line[APP_LOG_LINE_CAPACITY];
    const uint32_t flags = reset.reset_flags;
    const int length = snprintf(line, sizeof line,
        "$RESET,CSR=%08" PRIX32 ",FLAGS=%08" PRIX32 ",PIN=%u,POR=%u,BOR=%u,SFT=%u"
        ",IWDG=%u,WWDG=%u,LPWR=%u,CLEAR_OK=%u,CAPTURED=%u\r\n",
        reset.raw_csr, flags, (unsigned)((flags >> 26U) & 1U),
        (unsigned)((flags >> 27U) & 1U), (unsigned)((flags >> 25U) & 1U),
        (unsigned)((flags >> 28U) & 1U), (unsigned)((flags >> 29U) & 1U),
        (unsigned)((flags >> 30U) & 1U), (unsigned)((flags >> 31U) & 1U),
        captured && reset.clear_ok ? 1U : 0U, captured ? 1U : 0U);
    SendLine(line, length);
}
#endif

#if defined(SBCG_USE_LWIP) && defined(SBCG_USE_CHIRP)
/**
 * @brief 将ChirpService候选附近PCM切片转交UDP下传；sink仅前台借用一次。
 * @param slice 非NULL只读切片，仅本调用有效，不保存指针。
 * @return 无返回值。
 */
static void ChirpCaptureToUdp(const ChirpCaptureSlice *slice)
{(void)UDPChirpCapture_Send(slice);}
#endif

/**
 * @brief 读取身份并按依赖顺序初始化，诊断版本在BOOT后排队一次复位副本。
 * @return 无返回值。
 * @note 依赖生成初始化；先拆最外层PTP再拆UDPTimeExchange，所有权失败则返回。
 *       前台队列每轮至多发送一行，避免周期突发阻塞PCM归还；不自行触发复位。
 */
/**
 * @brief 配对结果→唯一测量结果；无有效温度/声速时明确拒绝，不显示距离。
 * @param pair 非NULL只读配对结果，仅本次调用有效。
 * @return 无返回值；不读PCM，不修改网络/配对状态。
 */
#if defined(SBCG_USE_SOUND_TIME) && defined(SBCG_USE_BOARD_IDENTITY)
static void PublishMeasurement(const SoundPairResult *pair)
{
    TemperatureSnapshot temperature;
    const uint32_t now_ms=BSP_Console_GetTickMs();
    TemperatureService_GetSnapshot(now_ms,&temperature);
    (void)MeasurementResult_PublishPair(pair,&temperature,now_ms);
}
#endif

void App_Init(void)
{
#ifdef SBCG_USE_BOARD_SYNC
    /* 最外层PTP包装必须先安全卸载；失败时不动原网络/音频服务。 */
    if (!TimeSync_Deinit())
    {
        static const char error[] = "$SYNC_INIT,RESULT=OWNERSHIP_ERROR\r\n";
        SendLine(error, (int)(sizeof(error) - 1U));
        return;
    }
#endif
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
    log_full_count=log_write_count=log_bad_count=log_high_water=0U;
    pending_head = pending_count = 0U;
    BSP_Console_GetInfo(&info);
    const int length = snprintf(line, sizeof(line),
        "$BOOT,FW=" APP_FIRMWARE_ID ",HCLK_HZ=%" PRIu32
        ",UID_W0=%08" PRIX32 ",UID_W1=%08" PRIX32 ",UID_W2=%08" PRIX32 "\r\n",
        info.core_clock_hz, info.uid_words[0], info.uid_words[1], info.uid_words[2]);
    SendLine(line, length);
#ifdef SBCG_USE_RESET_DIAGNOSTIC
    ReportReset();
#endif
    last_heartbeat_ms = BSP_Console_GetTickMs();
#ifdef SBCG_USE_BOARD_IDENTITY
    const BoardIdentityProfile *profile = BSP_BoardIdentity_Get();
    app_identity_configured = profile != NULL &&
        (profile->role == BOARD_ROLE_A || profile->role == BOARD_ROLE_B) &&
        memcmp(profile->uid_words, info.uid_words, sizeof info.uid_words) == 0;
    EmitBoardIdentity(app_identity_configured ? profile : NULL);
    if (!app_identity_configured) { return; }
#endif
#ifdef SBCG_USE_SDRAM
    /* FMC/MPU已由main生成初始化。芯片上电等待在音频DMA启动前完成。 */
    sdram_reported_status = BSP_SDRAM_OFF;
#ifdef SBCG_USE_LCD
    const bool sdram_initialized = BSP_SDRAM_InitDisplay();
#else
    const bool sdram_initialized = BSP_SDRAM_Init();
#endif
    BSP_SdramSnapshot sdram_info;
    BSP_SDRAM_GetSnapshot(&sdram_info);
    char sdram_line[APP_LOG_LINE_CAPACITY];
    const int sdram_length = snprintf(sdram_line, sizeof sdram_line,
        "$SDRAM_INIT,RESULT=%s,BASE=%08" PRIX32 ",BYTES=%" PRIu32
        ",SDCLK_HZ=%" PRIu32 ",CAS=3,REFRESH=%" PRIu32 "\r\n",
        sdram_initialized ? "OK" : "ERROR", BSP_SDRAM_BASE, BSP_SDRAM_BYTES,
        sdram_info.sdclk_hz, sdram_info.refresh_count);
    SendLine(sdram_line, sdram_length);
    ReportSdram(BSP_Console_GetTickMs());
#endif
#ifdef SBCG_USE_TOUCHGFX
    /* 音频DMA启动前关闭显示并清空双缓冲，稍后框架初始化复用READY结果。 */
    (void)BSP_TouchGFXDisplay_Init();
#endif
    EthernetMonitor_Init(last_heartbeat_ms, SendLine);
#ifdef SBCG_USE_LWIP
    UDPHeartbeat_Init(last_heartbeat_ms, info.uid_words, SendLine);
    UDPProbe_Init(last_heartbeat_ms, info.uid_words, SendLine);
    UDPChirpCapture_Init(last_heartbeat_ms, SendLine);
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
#ifdef SBCG_USE_BOARD_SYNC
    if (!TimeSync_Init(BSP_Console_GetTickMs(), profile, BSP_Console_GetTickMs, SendLine))
    {
        static const char error[] = "$SYNC_INIT,RESULT=INIT_ERROR\r\n";
        SendLine(error, (int)(sizeof(error) - 1U));
    }
#endif
#endif
    TemperatureService_Init(BSP_Console_GetTickMs(), info.uid_words, SendLine);
    TemperatureConsole_Init(SendLine);
#ifdef SBCG_USE_AUDIO
    AudioCapture_Init(BSP_Console_GetTickMs(), SendLine);
#endif
#if defined(SBCG_USE_LWIP) && defined(SBCG_USE_CHIRP)
    /* 候选附近End验证PCM快照经UDP下传；Init已清状态，注册sink不改变检测结果。 */
    ChirpService_SetCaptureSink(ChirpCaptureToUdp);
#endif
#ifdef SBCG_USE_AUDIO_ANCHOR
    AudioTimeAnchor_Init(BSP_Console_GetTickMs(), SendLine);
#endif
#ifdef SBCG_USE_AUDIO_MAP
    AudioTimeMapMonitor_Init(SendLine);
#ifdef SBCG_USE_SOUND_TIME
    SoundEventTime_Init(SendLine);
#ifdef SBCG_USE_BOARD_IDENTITY
    MeasurementResult_Init(BSP_Console_GetTickMs());
    if (!UDPSoundEvent_Init(BSP_Console_GetTickMs(), profile,
                            BSP_Console_GetTickMs, SendLine))
    {
        static const char error[] = "$SOUND_EVENT_INIT,RESULT=INIT_ERROR\r\n";
        SendLine(error, (int)(sizeof(error) - 1U));
    }
    else { UDPSoundEvent_SetResultSink(PublishMeasurement); }
#endif
#endif
#endif
    CodecMonitor_Init(BSP_Console_GetTickMs(), SendLine);
    if (!BSP_Console_RxInit())
    {
        static const char error[] = "$CONSOLE,INIT=ERROR\r\n";
        SendLine(error, (int)sizeof error - 1);
    }
#ifdef SBCG_USE_TOUCHGFX
    /* 双缓冲已预备；此处仅初始化框架，首帧可见后由前台开背光。 */
    UI_TouchGFX_Init(BSP_Console_GetTickMs(), SendLine);
#elif defined(SBCG_USE_LCD)
    /* 所有业务就绪后只建立显示服务；首帧由后续主循环分片完成。 */
    UI_StatusService_Init(BSP_Console_GetTickMs(), SendLine);
#endif
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
 *          CHIRP构建额外累计最大一轮LOOP_MS及同轮AUDIO/UI/OTHER分解；LOOP与Chirp
 *          GAP_MS采样点不同，只作同一调度周期的近似对照，不用于相减推断未归因时间。
 */
void App_Process(void)
{
#ifdef SBCG_USE_BOARD_IDENTITY
    /* 未知UID时LwIP尚未初始化；只保留BOOT/角色诊断，不触碰网卡或其它业务。 */
    if (!app_identity_configured) { return; }
#endif
#ifdef SBCG_USE_CHIRP
    /* 同循环归因：记录本轮入口及AudioCapture/UI耗时，结束时只在最大循环更新分解。 */
    const uint32_t loop_begin_ms = BSP_Console_GetTickMs();
    uint32_t loop_audio_ms = 0U, loop_ui_ms = 0U;
#endif
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
#ifdef SBCG_USE_CHIRP
            "$HEARTBEAT,SEQ=%" PRIu32 ",TICK_MS=%" PRIu32 ",TX_ERRORS=%" PRIu32
            ",LOG_FULL=%" PRIu32 ",LOG_WRITE=%" PRIu32 ",LOG_BAD=%" PRIu32
            ",LOG_Q=%" PRIu32 ",LOG_HWM=%" PRIu32
            ",LOOP_MS=%" PRIu32 ",AUDIO_MS=%" PRIu32 ",UI_MS=%" PRIu32
            ",OTHER_MS=%" PRIu32 "\r\n",
            heartbeat_seq,now_ms,(uint32_t)app_tx_error_count,log_full_count,
            log_write_count,log_bad_count,pending_count,log_high_water,
            max_loop_ms,max_loop_audio_ms,max_loop_ui_ms,max_loop_other_ms);
#else
            "$HEARTBEAT,SEQ=%" PRIu32 ",TICK_MS=%" PRIu32 ",TX_ERRORS=%" PRIu32 "\r\n",
            heartbeat_seq, now_ms, (uint32_t)app_tx_error_count);
#endif
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
#ifdef SBCG_USE_BOARD_SYNC
    /* 使用本轮温度状态：DATA拉低时延后主动PTP发送和硬件校正。 */
    TimeSync_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#ifdef SBCG_USE_PPS
    /* DHT拉低只禁止新目标排程；安全截止及已到达目标的消费每轮仍须执行。 */
    PpsService_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#endif
#endif
    CodecMonitor_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#ifdef SBCG_USE_AUDIO
#ifdef SBCG_USE_CHIRP
    const uint32_t audio_begin_ms = BSP_Console_GetTickMs();
#endif
    AudioCapture_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#ifdef SBCG_USE_CHIRP
    loop_audio_ms = BSP_Console_GetTickMs() - audio_begin_ms;
#endif
#endif
#ifdef SBCG_USE_AUDIO_ANCHOR
    AudioTimeAnchor_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#endif
#ifdef SBCG_USE_SOUND_TIME
    /* 单次消费锚点，同一副本分发给旧自检与候选模型，DHT低态不发布。 */
    if(!TemperatureService_IsStartLowActive())
    {
        AudioTimeAnchorObservation observation;
        if(AudioTimeAnchor_TakeObservation(&observation))
        {
            const uint32_t observation_ms=BSP_Console_GetTickMs();
            AudioTimeMapMonitor_Observe(observation_ms,&observation);
            SoundEventTime_Observe(BSP_Console_GetTickMs(),&observation);
        }
        BSP_AudioSnapshot snapshot;
        BSP_Audio_GetSnapshot(&snapshot);
        SoundEventTime_Process(BSP_Console_GetTickMs(),&snapshot);
        /* Diagnostic event publishing: identity/time-domain are supplied by App,
           but this constant domain is not a PTP sync or formal measurement claim. */
        SoundEventTimeEvent notice;
        while (SoundEventTime_TakeEvent(&notice))
        {
#ifdef SBCG_USE_BOARD_IDENTITY
            const BoardIdentityProfile *event_profile = BSP_BoardIdentity_Get();
            if (event_profile == NULL) { break; }
            SoundEventRecord event = {0};
            event.valid = true;
            event.type = SOUND_EVENT_CHIRP;
            event.quality = 100U;
            event.side = event_profile->role == BOARD_ROLE_A ? SOUND_SIDE_A : SOUND_SIDE_B;
            event.time_domain = APP_SOUND_EVENT_DIAGNOSTIC_DOMAIN;
            event.session = notice.session;
            event.sequence = notice.sequence;
            event.time_ns = notice.time_ns;
            (void)UDPSoundEvent_Send(BSP_Console_GetTickMs(), &event);
#else
            (void)notice;
#endif
        }
        UDPSoundEvent_Process(BSP_Console_GetTickMs());
    }
#elif defined(SBCG_USE_AUDIO_MAP)
    AudioTimeMapMonitor_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#endif
#ifdef SBCG_USE_SDRAM
#ifndef SBCG_USE_LCD
    /* 专用U1诊断模式：归还音频后测试最多128字；不得与显示模式共用内存。 */
    BSP_SDRAM_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#endif
    if (!TemperatureService_IsStartLowActive()) { ReportSdram(BSP_Console_GetTickMs()); }
#endif
#ifdef SBCG_USE_TOUCHGFX
    /**
     * ==================== 模块：TouchGFX状态页前台调度 ====================
     * 作用：更新约1Hz只读文本，并消费LTDC发布的VSYNC执行一次框架入口。
     * 调度顺序：PCM唯一消费者已归还缓冲；DHT起始低态跳过整次UI工作。
     * 调用关系：主循环进入，不在LTDC/音频DMA/UART回调绘图。
     */
#ifdef SBCG_USE_CHIRP
    const uint32_t ui_begin_ms = BSP_Console_GetTickMs();
#endif
    UI_TouchGFX_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#ifdef SBCG_USE_CHIRP
    loop_ui_ms = BSP_Console_GetTickMs() - ui_begin_ms;
#endif
#elif defined(SBCG_USE_LCD)
    /**
     * ==================== 模块：状态页前台绘制 ====================
     * 作用：使用业务只读副本绘制最多八行，完成后台帧后交给LTDC换页。
     * 调度顺序：音频唯一PCM消费者已归还；DHT起始低态整次延后。
     * 调用关系：主循环进入本段；不在DMA/SAI/UART回调绘图。
     */
#ifdef SBCG_USE_CHIRP
    const uint32_t ui_begin_ms = BSP_Console_GetTickMs();
#endif
    UI_StatusService_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#ifdef SBCG_USE_CHIRP
    loop_ui_ms = BSP_Console_GetTickMs() - ui_begin_ms;
#endif
#endif
#ifdef SBCG_USE_CHIRP
    const uint32_t flush_begin_ms = BSP_Console_GetTickMs();
#endif
    FlushOneLine();
#ifdef SBCG_USE_CHIRP
    const uint32_t loop_flush_ms = BSP_Console_GetTickMs() - flush_begin_ms;
    /* 只保留迄今最大一轮的分解；LOOP_MS与Chirp GAP_MS采样点不同，不能相减推断。 */
    const uint32_t loop_ms = BSP_Console_GetTickMs() - loop_begin_ms;
    if (loop_ms > max_loop_ms) {
        max_loop_ms = loop_ms;
        max_loop_audio_ms = loop_audio_ms;
        max_loop_ui_ms = loop_ui_ms;
        max_loop_flush_ms = loop_flush_ms;
        const uint32_t known = loop_audio_ms + loop_ui_ms;
        max_loop_other_ms = loop_ms > known ? loop_ms - known : 0U;
    }
#if defined(SBCG_USE_AUDIO)
    /* 每5秒输出低频率A3分解：最大循环的OTHER/Flush与AudioCapture内部最大轮。 */
    if ((uint32_t)(BSP_Console_GetTickMs() - last_profile_ms) >= 5000U) {
        last_profile_ms = BSP_Console_GetTickMs();
        AudioCaptureProfile audio_profile;
        AudioCapture_GetProfile(&audio_profile);
        char line[256];
        int length = snprintf(line, sizeof(line),
            "$PROFILE,LOOP_MS=%" PRIu32 ",AUDIO_MS=%" PRIu32 ",UI_MS=%" PRIu32
            ",OTHER_MS=%" PRIu32 ",FLUSH_MS=%" PRIu32 "\r\n",
            max_loop_ms, max_loop_audio_ms, max_loop_ui_ms, max_loop_other_ms,
            max_loop_flush_ms);
        if (length > 0 && (size_t)length < sizeof(line)) { SendLine(line, length); }
        length = snprintf(line, sizeof(line),
            "$AUDIO_PROFILE,TOTAL_MS=%" PRIu32 ",STAT_MS=%" PRIu32 ",SD_STAGE_MS=%" PRIu32
            ",SD_PROC_MS=%" PRIu32 ",CH_STAGE_MS=%" PRIu32 ",CH_COMP_MS=%" PRIu32
            ",CH_PROC_MS=%" PRIu32 ",IO_MS=%" PRIu32 ",LOG_MS=%" PRIu32
            ",OTHER_MS=%" PRIu32 "\r\n", audio_profile.total_ms, audio_profile.stats_ms,
            audio_profile.sound_stage_ms, audio_profile.sound_process_ms,
            audio_profile.chirp_stage_ms, audio_profile.chirp_complete_ms,
            audio_profile.chirp_process_ms, audio_profile.io_ms, audio_profile.log_ms,
            audio_profile.other_ms);
        if (length > 0 && (size_t)length < sizeof(line)) { SendLine(line, length); }
    }
#endif
#endif
}
