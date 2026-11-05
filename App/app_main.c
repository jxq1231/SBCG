/**
 * @file        app_main.c
 * @brief       协调单板网络、时间观测、温度与音频服务的初始化和前台调度。
 * @details     向各服务提供有界串口日志出口，按既定顺序初始化与轮询；
 *              DHT11起始低电平期间暂存日志并延后音频等观测，继续处理网络。
 *              模式控制仅用于观察双板握手，与测距、拍掌和波形调度独立。
 * @note        由Core/Src/main.c在CubeMX外设初始化后调用；不创建RTOS任务，
 *              不承担IRQ处理；维护前台日志队列及分类诊断，页面/校时/PPS交由各独立服务。
 */
#include "app_main.h"
#ifdef SBCG_USE_ADVANCED_UI
#include "Network/clap_wire.h"
#include "UI/waveform_service.h"
/** @brief 前台独占击掌权威状态，与标准音缓存分离；不与DMA共享。 */
static ClapRanging clap_ranging __attribute__((section(".ItcmState"),aligned(8)));
/** @brief 五秒诊断窗内波形前台更新最大耗时，单位ms，不参与调度。 */
static uint32_t wave_update_max_ms;
/** @brief 只读复制击掌展示，不推进时间。@param out 非NULL调用方存储。@return 无返回值。 */
void App_GetClapSnapshot(ClapRangingSnapshot *out){ClapRanging_GetSnapshot(&clap_ranging,out);}
#endif
#ifdef SBCG_USE_CALIBRATION_CONTROL
#include "Calibration/calibration_control.h"
/** @brief 已记录的前台标定请求完成序号，只按用户动作输出一次诊断。 */
static uint32_t calibration_reported;
#endif
#ifdef SBCG_ITCM_PROBE
#include "BSP/bsp_itcm_probe.h"
#endif
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
#ifdef SBCG_USE_BOOT_COUNTER
#include "BSP/bsp_boot_counter.h"
#endif
#ifdef SBCG_USE_SOUND_TIME
#include "Detector/sound_event_time.h"
#include "Detector/sound_detector_service.h"
#include "Network/udp_sound_event.h"
#include "Measure/measurement_result.h"
#include "Detector/standard_sequence.h"
#include "Detector/event_classifier.h"
#include "Measure/measurement_gate.h"
#include "Calibration/calibration.h"
#include "Measure/result_exchange_state.h"
#include "BSP/bsp_ptp.h"
#endif
#ifdef SBCG_USE_PPS
#include "Sync/pps_service.h"
#endif
#include "BSP/bsp_console.h"
#ifdef SBCG_USE_BOARD_SYNC
#include "Sync/time_sync.h"
#include "Sync/time_domain.h"
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
#define APP_FIRMWARE_ID "SBCG-A2B-RANGE-R1"
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
#define APP_RESULT_REFRESH_MS 500U
#if defined(SBCG_USE_SOUND_TIME) && defined(SBCG_USE_BOARD_IDENTITY)
static StandardSequence standard_sequence;
static bool standard_sequence_detected;
/** @brief CPU-only前台会话/确认和独立对端音频身份；Init显式清零。 */
typedef struct {
    RangingSession session;
    RangingExchangeState exchange;
    uint32_t local_audio_session,peer_audio_session;
    uint64_t peer_source_ns;
    uint32_t observed_drops,recover_after_processed;
    bool gap_pending;
} RangingAppState;
static RangingAppState ranging_app __attribute__((section(".ItcmState"),aligned(32)));
#endif
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
static uint32_t prof_lwip_max_ms,prof_temp_max_ms,prof_sound_max_ms;
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
{
    /* 同一捕获切片既下传PC诊断，也留一份波形储备供两板互相关精化时差。 */
#ifdef SBCG_USE_SOUND_TIME
    UDPSoundEvent_OnCapture(slice);
#endif
    (void)UDPChirpCapture_Send(slice);
}
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
/**
 * @brief 读取实际PTP共同时间并复核本机代际，失败不伪造源时间。
 * @param ns 非NULL输出，成功单位ns；失败写0。
 * @param epoch 非NULL本机代际输出，不要求与对端数字相同。
 * @return 两次代际一致且读时成功为true，仅前台读取已有BSP。
 */
static bool ReadRangingClock(uint64_t *ns,uint32_t *epoch)
{
    BSP_PtpTime clock={0};
    const uint32_t before=BSP_PTP_GetEpoch();
    const bool ok=BSP_PTP_Read(&clock)==BSP_PTP_OK;
    *epoch=BSP_PTP_GetEpoch();*ns=0U;
    if(!ok || before==0U || before!=*epoch || clock.nanoseconds>=1000000000U)return false;
    *ns=(uint64_t)clock.seconds*UINT64_C(1000000000)+clock.nanoseconds;
    return *ns!=0U;
}

/**
 * @brief 在收发和UI之前持续驱动状态会话与真实源期限。
 * @param now_ms 当前HAL毫秒；音频快照之后另取时校验回调年龄。
 * @return 无返回值；不发送报文、不读取PCM，不改变PTP/PPS配置。
 * @note 启用标定UI时，先核对真实测量新鲜度，再执行用户确认动作；系数变化按既有合同撤值等待新声音。
 */
static void RefreshRanging(uint32_t now_ms)
{
    const BoardIdentityProfile *profile=BSP_BoardIdentity_Get();
    if(profile==NULL)return;
    TimeDomainSnapshot domain;TimeDomain_Get(&domain);
    EthernetMonitorSnapshot network;EthernetMonitor_GetSnapshot(now_ms,&network);
    BSP_AudioSnapshot audio;BSP_Audio_GetSnapshot(&audio);
    const uint32_t audio_now=BSP_Console_GetTickMs();
    const bool local_changed=ranging_app.local_audio_session!=0U &&
        audio.session!=ranging_app.local_audio_session;
    /* DROP是累计计数：变化时撤值，等变化之后一个完整End成功再允许重新握手。
       保留计数供观测；会话改变时独立建立基线，不靠重启DMA清除历史。 */
    if(local_changed || ranging_app.local_audio_session==0U ||
       audio.dropped_blocks!=ranging_app.observed_drops)
    {
        ranging_app.observed_drops=audio.dropped_blocks;
        ranging_app.recover_after_processed=audio.processed_blocks;
        ranging_app.gap_pending=audio.dropped_blocks!=0U;
    }
    else if(ranging_app.gap_pending &&
            audio.processed_blocks!=ranging_app.recover_after_processed)
    { ranging_app.gap_pending=false; }
    const bool stream_ok=audio.running && audio.session!=0U && audio.callback_count!=0U &&
        (uint32_t)(audio_now-audio.last_callback_ms)<1000U && audio.error_count==0U &&
        audio.hal_error==0U && audio.dma_error==0U && audio.start_error==0U &&
        audio.index_faults==0U && !ranging_app.gap_pending;
    uint64_t common_ns;uint32_t clock_epoch;
    const bool clock_ok=ReadRangingClock(&common_ns,&clock_epoch);
    ranging_app.local_audio_session=audio.session;
    const bool qualified=domain.valid && network.valid && network.link_up && stream_ok && !local_changed;
    RangingSession_Update(&ranging_app.session,qualified,domain.id,clock_epoch,
        clock_ok,common_ns,now_ms);
    if(RangingSession_TakeContextChanged(&ranging_app.session))
    {
        ranging_app.peer_audio_session=0U;ranging_app.peer_source_ns=0U;
        RangingExchange_Init(&ranging_app.exchange);
    }
    RangingSessionSnapshot session;RangingSession_Get(&ranging_app.session,&session);
    const MeasurementContext context={
        .authority=profile->role==BOARD_ROLE_A,
        .domain_valid=qualified && session.established,
        .stream_valid=stream_ok,.clock_valid=clock_ok,
        .time_domain=domain.id,.authority_epoch=session.authority_epoch,
        .local_clock_epoch=clock_epoch,
        .a_session=profile->role==BOARD_ROLE_A?audio.session:ranging_app.peer_audio_session,
        .b_session=profile->role==BOARD_ROLE_B?audio.session:ranging_app.peer_audio_session,
        .source_floor_ns=session.source_floor_ns};
    TemperatureSnapshot temperature;TemperatureService_GetSnapshot(now_ms,&temperature);
    CalibrationSnapshot calibration;Calibration_Get(&calibration);
    if(context.authority)MeasurementResult_SetCalibration(calibration.enabled?calibration.offset_mm:0,calibration.version);
    MeasurementResult_Update(&context,&temperature,now_ms,common_ns);
#ifdef SBCG_USE_CALIBRATION_CONTROL
    MeasurementCalibrationSample calibration_sample;
    const bool sample_available=MeasurementResult_GetCalibrationSample(&calibration_sample);
    MeasurementPresentationSnapshot calibration_presentation;
    MeasurementResult_GetPresentation(&calibration_presentation);
    if(CalibrationControl_Process(context.authority,sample_available?&calibration_sample:NULL,
        calibration_presentation.calibration_offset_mm,calibration_presentation.calibration_version))
    {
        Calibration_Get(&calibration);
        MeasurementResult_SetCalibration(calibration.enabled?calibration.offset_mm:0,calibration.version);
        MeasurementResult_Update(&context,&temperature,now_ms,common_ns);
    }
    CalibrationControlSnapshot control;CalibrationControl_GetSnapshot(&control);
    if(control.completed!=calibration_reported)
    {
        calibration_reported=control.completed;
        char line[240];
        const int length=snprintf(line,sizeof line,
            "$CALIBRATION,ACTION=%u,RESULT=%u,SAVED=%u,ENABLED=%u,OFFSET_MM=%" PRId32
            ",APPLIED_MM=%" PRId32 ",VERSION=%u,SAMPLE_ID=%" PRIu32 "\r\n",
            (unsigned)control.action,(unsigned)control.result,control.calibration.calibrated?1U:0U,
            control.calibration.enabled?1U:0U,control.calibration.offset_mm,control.applied_offset_mm,
            (unsigned)control.applied_version,sample_available?calibration_sample.measurement_id:0U);
        if(length>0 && (size_t)length<sizeof line)SendLine(line,length);
    }
#endif
#ifdef SBCG_USE_ADVANCED_UI
    ClapRanging_Update(&clap_ranging,profile->role,context.domain_valid && stream_ok && clock_ok,
        &session,clock_ok?common_ns:0U,now_ms,&temperature,&calibration);
    const uint32_t wave_begin_ms=BSP_Console_GetTickMs();
    WaveformService_Update(profile->role,context.domain_valid && stream_ok && clock_ok,
        &session,clock_ok?common_ns:0U,now_ms);
    const uint32_t wave_elapsed_ms=BSP_Console_GetTickMs()-wave_begin_ms;
    if(wave_elapsed_ms>wave_update_max_ms)wave_update_max_ms=wave_elapsed_ms;
#endif
}

#ifdef SBCG_USE_ADVANCED_UI
/** @brief 固定对端短包一次解码后分发；仅匹配角色消费。@param data 借用载荷。@param length 已分派长度。@return 无返回值。 */
static void HandleAuxiliary(const uint8_t *data,uint16_t length)
{
    const BoardIdentityProfile *profile=BSP_BoardIdentity_Get();
    const uint32_t now=BSP_Console_GetTickMs();RefreshRanging(now);
    if(memcmp(data,"CLP1",4)==0 && profile->role==BOARD_ROLE_B)
    { ClapResultMessage result;if(ClapWire_DecodeResult(data,length,&result))
        (void)ClapRanging_OnMessage(&clap_ranging,&result); }
    else if(memcmp(data,"CLA1",4)==0 && profile->role==BOARD_ROLE_A)
    { ClapAckMessage ack;if(ClapWire_DecodeAck(data,length,&ack))
        (void)ClapRanging_OnAck(&clap_ranging,&ack); }
    else if(memcmp(data,"WVP2",4)==0 && profile->role==BOARD_ROLE_B)
    { WaveformPlan plan;if(WaveformPlanWire_Decode(data,length,&plan))
        (void)WaveformService_OnPlan(&plan,now); }
}
/** @brief 前台发送击掌结果/确认和共同波形计划，失败不补发突发流。@param now_ms 当前毫秒。@return 无返回值。 */
static void ServiceAuxiliary(uint32_t now_ms)
{
    uint8_t data[CLAP_RESULT_WIRE_BYTES];ClapResultMessage result;ClapAckMessage ack;WaveformPlan plan;
    if(ClapRanging_TakeTx(&clap_ranging,now_ms,&result) && ClapWire_EncodeResult(&result,data,CLAP_RESULT_WIRE_BYTES))
        (void)UDPSoundEvent_SendAuxiliary(data,CLAP_RESULT_WIRE_BYTES);
    if(ClapRanging_TakeAck(&clap_ranging,&ack) && ClapWire_EncodeAck(&ack,data,CLAP_ACK_WIRE_BYTES))
        (void)UDPSoundEvent_SendAuxiliary(data,CLAP_ACK_WIRE_BYTES);
    if(WaveformService_TakePlan(&plan) && WaveformPlanWire_Encode(&plan,data,WAVEFORM_PLAN_WIRE_BYTES))
        (void)UDPSoundEvent_SendAuxiliary(data,WAVEFORM_PLAN_WIRE_BYTES);
}
#endif

/**
 * @brief 接收固定对端的状态握手；message仅本次借用，健康读时先于处理。
 * @return 无返回值；握手结果由主循环末尾的RefreshRanging统一写入上下文。
 */
static void HandleRangingSession(const RangingSessionMessage *message)
{
    const uint32_t now=BSP_Console_GetTickMs();
    RefreshRanging(now);
    (void)RangingSession_OnMessage(&ranging_app.session,message,now);
}

/**
 * @brief 从已校验对端事件建立音频session身份；旧源不得自行重置当前上下文。
 * @param event UDP层核对端点/角色/共同域后的只读副本，仅本次调用有效。
 * @return 无返回值；不从pair采用session，不发送报文。
 */
static void HandlePeerSound(const SoundEventRecord *event)
{
    if(event==NULL || !event->valid || event->session==0U)return;
    const uint32_t now=BSP_Console_GetTickMs();RefreshRanging(now);
    RangingSessionSnapshot session;RangingSession_Get(&ranging_app.session,&session);
    uint64_t common_ns;uint32_t epoch;
    if(!session.established || event->time_domain!=session.domain ||
       event->time_ns<session.source_floor_ns || !ReadRangingClock(&common_ns,&epoch) ||
       (event->time_ns>common_ns && event->time_ns-common_ns>UINT64_C(2000000)) ||
       event->time_ns<=ranging_app.peer_source_ns)return;
    ranging_app.peer_source_ns=event->time_ns;
    ranging_app.peer_audio_session=event->session;
}
/**
 * @brief 处理对端域报文：B采用A令牌并回ACK；A只接受同令牌ACK。
 * @param message 非NULL已通过UDP来源/长度/角色校验的域消息，仅本次有效。
 * @return 无返回值；不修改PPS/TimeSync资格。
 */
static void HandleDomainMessage(const DomainMessage *message)
{
    if(message==NULL)return;
    const BoardIdentityProfile *profile=BSP_BoardIdentity_Get();
    if(profile==NULL)return;
    const uint32_t now=BSP_Console_GetTickMs();
    if(profile->role==BOARD_ROLE_B && message->type==DOMAIN_MESSAGE_REQUEST &&
       message->role==BOARD_ROLE_A)
    {
        if(TimeDomain_OnPeerRequest(message->token,now))
        { (void)UDPSoundEvent_SendDomainAck(now,message->token); }
    }
    else if(profile->role==BOARD_ROLE_A && message->type==DOMAIN_MESSAGE_ACK &&
            message->role==BOARD_ROLE_B && message->state==DOMAIN_LOCAL_VALID)
    {
        /* B重启会把ACK序号重置为1；这时必须恢复当前结果重发。 */
        (void)TimeDomain_OnPeerAck(message->token,now);
    }
}
/**
 * @brief A每500ms至多尝试一次REQ并检查对端超时；B只检查超时（ACK由回调发）。
 * @param now_ms 当前HAL毫秒。
 * @return 无返回值；在事件取出、发送及配对处理前同步UDP有效域。
 * @note 撤域或换域会清除UDP旧候选；DOMAIN握手仍通过既有独立路径收发。
 */
static void ServiceDomain(uint32_t now_ms)
{
    TimeDomain_Tick(now_ms);
    const BoardIdentityProfile *profile=BSP_BoardIdentity_Get();
    if(profile==NULL){UDPSoundEvent_SetTimeDomain(0U);return;}
    if(profile->role==BOARD_ROLE_A)
    {
        MeasurementGate_Tick(now_ms);
    }
    TimeDomainSnapshot domain;
    TimeDomain_Get(&domain);
    UDPSoundEvent_SetTimeDomain(domain.valid?domain.id:0U);
    static bool last_domain_valid;
    if(profile->role==BOARD_ROLE_A && domain.valid!=last_domain_valid)
    {
        if(domain.valid){
            MeasurementGate_OnDomainValid(now_ms);
            RangingExchange_Init(&ranging_app.exchange);
        }
        else MeasurementGate_OnDomainInvalid(now_ms);
        last_domain_valid=domain.valid;
    }
    /* 先取走门控请求，再刷新本拍状态：会话换代只重置资格，不撤掉换代后的新结果；
       3秒静默才软撤显示值，失域/断流仍由Update硬撤销。 */
    MeasurementGateSnapshot gate;
    MeasurementGate_Get(&gate);
    (void)MeasurementGate_TakeInvalidation();
    RefreshRanging(now_ms);
    MeasurementPresentationSnapshot visible;
    MeasurementResult_GetPresentation(&visible);
    if(profile->role==BOARD_ROLE_A && gate.state==MEASUREMENT_GATE_SILENCE &&
       visible.has_value && visible.source_age_ms>=MEASUREMENT_GATE_SILENCE_MS)
        MeasurementResult_WithdrawForGate(now_ms);
    static bool last_valid;
    static uint32_t last_domain_log_ms;
    if(domain.valid!=last_valid)
    {
        last_valid=domain.valid;
        char line[160];
        const int length=snprintf(line,sizeof line,
            "$DOMAIN,ROLE=%s,VALID=%u,LOCAL=%u,PEER=%u,TOKEN=%" PRIu32
            ",PEER_AGE_MS=%" PRIu32 "\r\n",
            profile->role==BOARD_ROLE_A?"A":"B",domain.valid?1U:0U,
            domain.local_valid?1U:0U,domain.peer_seen?1U:0U,
            domain.id,domain.peer_age_ms);
        SendLine(line,length);
        last_domain_log_ms=now_ms;
    }
    (void)last_domain_log_ms;
    if(profile->role!=BOARD_ROLE_A)return;
    if(!domain.local_valid || domain.id==0U)return;
    static uint32_t last_request_ms;
    if((uint32_t)(now_ms-last_request_ms)>=500U)
    {
        /* 失败也占用本次节拍，防止链路未就绪时按前台轮次重试。 */
        last_request_ms=now_ms;
        (void)UDPSoundEvent_SendDomainRequest(now_ms,domain.id);
    }
}
/**
 * @brief 将64位有符号数格式化为十进制文本，避免不同C库的printf长度修饰差异。
 * @param value 待格式化值。
 * @param out 至少24字节调用方缓冲，函数写入NUL结尾文本。
 * @return 无返回值；不保留指针。
 */
static void FormatSigned64(int64_t value,char *out)
{
    if(out==NULL)return;
    uint64_t magnitude=value<0?(uint64_t)(-(value+1))+1U:(uint64_t)value;
    char digits[21];uint8_t count=0U;
    do{digits[count++]=(char)('0'+(magnitude%10U));magnitude/=10U;}while(magnitude!=0U&&count<sizeof digits);
    uint8_t pos=0U;if(value<0)out[pos++]='-';
    while(count!=0U)out[pos++]=digits[--count];
    out[pos]='\0';
}

/**
 * @brief 将64位无符号数格式化为十进制文本，避免不同C库的printf长度修饰差异。
 * @param value 待格式化值。
 * @param out 至少24字节调用方缓冲，函数写入NUL结尾文本。
 * @return 无返回值；不保留指针。
 */
static void FormatUnsigned64(uint64_t value,char *out)
{
    if(out==NULL)return;
    char digits[21];uint8_t count=0U;
    do{digits[count++]=(char)('0'+(value%10U));value/=10U;}while(value!=0U&&count<sizeof digits);
    uint8_t pos=0U;
    while(count!=0U)out[pos++]=digits[--count];
    out[pos]='\0';
}

#ifdef SBCG_USE_MODE_OBSERVATION
/**
 * ==================== 模块：双板模式观测 ====================
 * 作用：复用现有UDP端点同步模式并记录状态变化，仅供观测。
 * 调度顺序：接收回调暂存报文，DHT允许且本轮音频归还与原业务处理后消费并发送。
 * 调用关系：App_Init注册入口，App_Process推进，诊断命令可设置代次和A板请求。
 */
/* 稳定后每5秒输出一次快照，避免只能靠命令观察稳态租约。 */
#define MODE_REPORT_PERIOD_MS 5000U
static ModeSnapshot last_mode_report;
static bool mode_reported;
static uint32_t last_mode_report_ms;

/** @brief 将本次启动代次交给模式状态机。@param boot 非零且跨启动递增。@return 接受为true。 */
bool App_ModeSetBoot(uint64_t boot)
{
    return ModeService_SetBoot(boot);
}

/** @brief 提交A板诊断模式请求。@param mode 待请求模式。@return 状态机接收为true。 */
bool App_ModeRequest(ModeKind mode)
{
    return ModeService_Request(mode);
}

/** @brief 读取模式观测快照。@param out 调用者输出，允许NULL。@return 无返回值。 */
void App_GetModeSnapshot(ModeSnapshot *out)
{
    ModeService_GetSnapshot(out);
}

/**
 * @brief 输出一条模式观测记录，不触发状态变化。
 * @param mode 非NULL前台快照，仅本次调用读取。
 * @return 无返回值。
 * @note 复用无符号64位格式化，避免精简C库丢失启动代次的高位。
 */
static void LogModeSnapshot(const ModeSnapshot *mode)
{
    char a_boot[24], b_boot[24], mode_seq[24];
    FormatUnsigned64(mode->a_boot, a_boot);
    FormatUnsigned64(mode->b_boot, b_boot);
    FormatUnsigned64(mode->mode_seq, mode_seq);
    char line[APP_LOG_LINE_CAPACITY];
    const int length = snprintf(line, sizeof line,
        "$MODE,ROLE=%c,REQUESTED=%u,EFFECTIVE=%u,STATE=%u,A_BOOT=%s,B_BOOT=%s,"
        "SEQ=%s,REASON=%u,ATTEMPTS=%" PRIu32 "\r\n",
        mode->role == BOARD_ROLE_A ? 'A' : 'B', (unsigned)mode->requested,
        (unsigned)mode->effective, (unsigned)mode->state, a_boot, b_boot, mode_seq,
        (unsigned)mode->reason, mode->attempts);
    SendLine(line, length);
}

#ifdef SBCG_MODE_TEST_CONTROL
/** @brief 响应前台MODE?命令，读取并记录当前状态。@return 无返回值。 */
void App_ReportMode(void)
{
    ModeSnapshot snapshot;
    App_GetModeSnapshot(&snapshot);
    LogModeSnapshot(&snapshot);
}
#endif

/**
 * @brief 在DHT允许时消费模式报文并推进握手，变化或周期到期时输出诊断。
 * @param now_ms 本地HAL毫秒，支持无符号回绕。
 * @return 无返回值。
 * @note 不使用PTP时间判断租约，不更改测距、音频或UI资格。本次启动代次登记前只输出首条；
 *       登记后每MODE_REPORT_PERIOD_MS输出一次稳态快照，重发次数变化不单独产生日志。
 */
static void ServiceModeObservation(uint32_t now_ms)
{
    EthernetMonitorSnapshot network;
    EthernetMonitor_GetSnapshot(now_ms, &network);
    ModeService_Process(now_ms, network.valid && network.link_up, UDPSoundEvent_SendModeControl);
    ModeSnapshot snapshot;
    ModeService_GetSnapshot(&snapshot);
    const bool booted = snapshot.a_boot != 0U || snapshot.b_boot != 0U;
    const bool periodic = booted &&
        (uint32_t)(now_ms - last_mode_report_ms) >= MODE_REPORT_PERIOD_MS;
    if (!mode_reported || periodic || snapshot.state != last_mode_report.state ||
        snapshot.reason != last_mode_report.reason || snapshot.requested != last_mode_report.requested ||
        snapshot.effective != last_mode_report.effective || snapshot.a_boot != last_mode_report.a_boot ||
        snapshot.b_boot != last_mode_report.b_boot || snapshot.mode_seq != last_mode_report.mode_seq)
    {
        mode_reported = true;
        last_mode_report = snapshot;
        last_mode_report_ms = now_ms;
        LogModeSnapshot(&snapshot);
    }
}

#ifdef SBCG_USE_BOOT_COUNTER
/**
 * @brief   读取并登记本板跨掉电启动代次，输出一条启动诊断。
 * @return  无返回值。
 * @note    仅App_Init启动阶段调用一次；失败时保持模式服务未登记，串口保留稳定原因。
 */
static void LoadBootGeneration(void)
{
    BSP_BootCounterResult counter;
    const BSP_BootCounterStatus status = BSP_BootCounter_Next(&counter);
    char value[24];
    FormatUnsigned64(counter.value, value);
    char line[APP_LOG_LINE_CAPACITY];
    const int length = snprintf(line, sizeof line,
        "$BOOTGEN,SOURCE=FLASH,STATUS=%s,VALUE=%s,SECTOR=%u,SLOT=%u,ERASED=%u\r\n",
        BSP_BootCounter_StatusName(status), value, (unsigned)counter.sector,
        (unsigned)counter.slot, counter.erased ? 1U : 0U);
    SendLine(line, length);
    if (status == BSP_BOOT_COUNTER_OK)
    {
        (void)ModeService_SetBoot(counter.value);
    }
}
#endif
#endif

/** @brief 将观测决策枚举转成稳定短标签；未知值返回UNKNOWN。 */
static const char *RangeDecisionName(MeasurementDecision decision)
{
    switch(decision)
    {
    case MEASUREMENT_DECISION_ACCEPT:return "ACCEPT";
    case MEASUREMENT_DECISION_REJECT_INVALID:return "INVALID";
    case MEASUREMENT_DECISION_REJECT_TEMPERATURE:return "TEMP";
    case MEASUREMENT_DECISION_REJECT_RANGE:return "RANGE";
    case MEASUREMENT_DECISION_REJECT_OUTLIER:return "OUTLIER";
    case MEASUREMENT_DECISION_INVALIDATE:return "INVALIDATE";
    case MEASUREMENT_DECISION_REMOTE_ACCEPT:return "REMOTE";
    default:return "NONE";
    }
}

/**
 * @brief 输出一次真实tracker决策；源身份和结果链接分行，避免256字节截断。
 * @param pair 调用内借用的pair，NULL不输出；拒绝不把其源绑到旧结果。
 * @param temperature 保留接口，温补字段取同一次诊断副本而非重复计算。
 * @param reason 前台短原因，不能为空。
 * @param formal 当前真实正式资格，PROXY不得为true。
 * @param new_measurement 是否产生新的独立源ID；HOLD/拒绝/温补为false。
 * @return 无返回值；日志满/非法长度由统一出口累计，不改变业务状态。
 */
static void LogRangeDecision(const SoundPairResult *pair,const TemperatureSnapshot *temperature,
                             const char *reason,bool formal,bool new_measurement)
{
    (void)temperature;
    if(pair==NULL)return;
    MeasurementDiagnosticsSnapshot diag;MeasurementResult_GetDiagnostics(&diag);
    MeasurementPresentationSnapshot p;MeasurementResult_GetPresentation(&p);
    char raw_ns[24],source[24];FormatSigned64(pair->delta_ns,raw_ns);
    FormatUnsigned64(p.source_common_ns,source);
    char line[APP_LOG_LINE_CAPACITY];
    int length=snprintf(line,sizeof line,
        "$RANGE_DECISION,SCHEMA=2,SEQ=%" PRIu32 ",DOMAIN=%" PRIu32
        ",A_SESS=%" PRIu32 ",A_SEQ=%" PRIu32 ",B_SESS=%" PRIu32 ",B_SEQ=%" PRIu32
        ",RAW_NS=%s,DEC=%s,REASON=%s\r\n",
        diag.sequence,pair->time_domain,pair->a_session,pair->a_sequence,
        pair->b_session,pair->b_sequence,raw_ns,RangeDecisionName(diag.decision),reason);
    SendLine(line,length);
    length=snprintf(line,sizeof line,
        "$RANGE_LINK,SCHEMA=2,SEQ=%" PRIu32 ",DOMAIN=%" PRIu32 ",EPOCH=%" PRIu32
        ",REV=%" PRIu32 ",ID=%" PRIu32 ",NEW=%u,FORMAL=%u,PROV=%u,SRC_NS=%s\r\n",
        diag.sequence,p.time_domain,p.authority_clock_epoch,p.state_revision,p.measurement_id,
        new_measurement?1U:0U,formal?1U:0U,p.provisional?1U:0U,source);
    SendLine(line,length);
    length=snprintf(line,sizeof line,
        "$RANGE_DISTANCE,SCHEMA=2,SEQ=%" PRIu32 ",PAIR_MM=%" PRIu32 ",TEMP_MM=%" PRIu32
        ",CORR_MM=%" PRId32 ",PUB_MM=%" PRId32 ",STBL_CNT=%u,STBL0=%" PRId32
        ",STBL1=%" PRId32 ",STBL2=%" PRId32 ",STBL3=%" PRId32 ",STBL4=%" PRId32 "\r\n",
        diag.sequence,pair->distance_mm,diag.temperature_distance_mm,diag.corrected_mm,
        diag.published_mm,(unsigned)diag.stable_count,diag.stable_mm[0],diag.stable_mm[1],
        diag.stable_mm[2],diag.stable_mm[3],diag.stable_mm[4]);
    SendLine(line,length);
}

/**
 * @brief 将当前上下文的独立pair先交给tracker，标准序列仅决定发布资格。
 * @param pair 只读输入，仅调用内有效；普通拒绝不清已确认历史。
 * @return 无返回值；不执行旧中值发布或MRES发送，不改变识别阈值。
 */
static void PublishMeasurement(const SoundPairResult *pair)
{
    const BoardIdentityProfile *profile=BSP_BoardIdentity_Get();
    if(profile==NULL || profile->role!=BOARD_ROLE_A)return;
    const uint32_t now=BSP_Console_GetTickMs();RefreshRanging(now);
    TemperatureSnapshot temperature;TemperatureService_GetSnapshot(now,&temperature);
#ifdef SBCG_USE_ADVANCED_UI
    if(pair->type==SOUND_EVENT_CLAP)
    {
        const bool published=ClapRanging_PublishPair(&clap_ranging,pair);
        ClapRangingSnapshot clap;ClapRanging_GetSnapshot(&clap_ranging,&clap);
        char line[180];const int n=snprintf(line,sizeof line,
            "$CLAP_RESULT,VALID=%u,ID=%" PRIu32 ",MM=%" PRId32 ",DIR=%u,QUALITY=%u\r\n",
            published?1U:0U,clap.result.id,clap.has_value?clap.result.distance_mm:0,
            (unsigned)clap.result.direction,(unsigned)clap.result.quality);
        if(n>0 && (size_t)n<sizeof line)SendLine(line,n);
        return;
    }
#endif
    StandardSequenceSnapshot sequence;StandardSequence_Get(&standard_sequence,&sequence);
    MeasurementGateSnapshot gate;MeasurementGate_Get(&gate);
    uint64_t common_ns;uint32_t epoch;
    const bool read_ok=ReadRangingClock(&common_ns,&epoch);
    const bool qualified=sequence.detected && sequence.quality>=50U &&
        MeasurementGate_CanPublish(&gate);
    MeasurementPresentationSnapshot before;MeasurementResult_GetPresentation(&before);
    const RangingDecision decision=MeasurementResult_PublishTrackedPair(pair,&temperature,
        qualified,now,read_ok?common_ns:0U);
    MeasurementPresentationSnapshot after;MeasurementResult_GetPresentation(&after);
    const bool new_result=after.has_value && after.measurement_id!=before.measurement_id;
    LogRangeDecision(pair,&temperature,decision==RANGING_DECISION_ACCEPT?"TRACKED":
        decision==RANGING_DECISION_CANDIDATE?"CANDIDATE":"REJECTED",after.formal,new_result);
    if(new_result && qualified)(void)MeasurementGate_OnMeasurement(now);
}

/** @brief 把RST2状态数值映射到本地展示状态。 */
static MeasurementPresentationState PresentationFromWire(uint8_t state)
{
    switch(state)
    {
    case RANGING_STATE_TRACKING:return MEASUREMENT_PRESENTATION_TRACKING;
    case RANGING_STATE_HOLD:return MEASUREMENT_PRESENTATION_HOLD;
    case RANGING_STATE_REACQUIRE:return MEASUREMENT_PRESENTATION_REACQUIRE;
    case RANGING_STATE_STALE:return MEASUREMENT_PRESENTATION_STALE;
    case RANGING_STATE_FAULT:return MEASUREMENT_PRESENTATION_FAULT;
    default:return MEASUREMENT_PRESENTATION_ACQUIRING;
    }
}

/** @brief B接收A的RST2：缓存同一共同域展示状态并回RAK2。 */
static void PublishRemoteRangingStatus(const RangingStatusMessage *message)
{
    if(message==NULL)return;
    const BoardIdentityProfile *profile=BSP_BoardIdentity_Get();
    if(profile==NULL || profile->role!=BOARD_ROLE_B)return;
    TimeDomainSnapshot domain;
    TimeDomain_Get(&domain);
    if(!domain.valid || domain.id==0U || domain.id!=message->domain)return;
    const uint32_t now=BSP_Console_GetTickMs();
    RefreshRanging(now);
    MeasurementPresentationSnapshot presentation={0};
    presentation.state=PresentationFromWire(message->state);
    presentation.has_value=(message->flags&RANGING_STATUS_FLAG_HAS_VALUE)!=0U;
    presentation.fresh=(message->flags&RANGING_STATUS_FLAG_FRESH)!=0U;
    presentation.formal=(message->flags&RANGING_STATUS_FLAG_FORMAL)!=0U;
    presentation.provisional=presentation.has_value && !presentation.formal;
    presentation.filtered_delta_ns=message->filtered_delta_ns;
    presentation.distance_mm=message->distance_mm;
    presentation.direction=message->direction;
    presentation.quality=message->quality;
    presentation.evidence_kind=message->evidence_kind;
    presentation.measurement_id=message->measurement_id;
    presentation.state_revision=message->state_revision;
    presentation.time_domain=message->domain;
    presentation.authority_clock_epoch=message->authority_clock_epoch;
    presentation.temperature_version=message->temperature_version;
    presentation.sound_speed_mm_s=message->sound_speed_mm_s;
    presentation.calibration_version=message->calibration_version;
    presentation.calibration_offset_mm=message->calibration_offset_mm;
    presentation.reason=message->reason;
    presentation.source_key_crc32=message->source_key_crc32;
    presentation.detection_version=message->detection_version;
    presentation.source_common_ns=message->source_common_ns;
    presentation.fresh_deadline_ns=message->fresh_deadline_ns;
    presentation.display_deadline_ns=message->display_deadline_ns;
    const bool accepted=MeasurementResult_PublishRemotePresentation(
        &presentation,domain.id,now);
#ifdef SBCG_USE_ADVANCED_UI
    /* RST2与CLP1共用有序标定合同：迟到的旧状态不能把旧系数结果带回来。 */
    if(accepted)
        ClapRanging_ObserveCalibration(&clap_ranging,message->calibration_version,
            message->calibration_offset_mm);
#endif
    RangingStatusAck ack={accepted?RANGING_STATUS_ACK_OK:RANGING_STATUS_ACK_REJECT,
        message->domain,message->authority_clock_epoch,message->state_revision,
        message->measurement_id};
    (void)UDPSoundEvent_SendRangingAck(now,&ack);
}

/** @brief A只确认当前已发出的精确四元组；ack仅借用，旧ACK不续租。 */
static void HandleRangingAck(const RangingStatusAck *ack)
{
    const BoardIdentityProfile *profile=BSP_BoardIdentity_Get();
    if(profile!=NULL && profile->role==BOARD_ROLE_A)
        (void)RangingExchange_OnAck(&ranging_app.exchange,ack);
}

/** @brief 逐字段复制唯一权威展示，不在重传时重算期限；message为调用方输出。 */
static bool BuildRangingStatusMessage(RangingStatusMessage *message,uint32_t now_ms)
{
    (void)now_ms;
    MeasurementPresentationSnapshot p;MeasurementResult_GetPresentation(&p);
    RangingSessionSnapshot session;RangingSession_Get(&ranging_app.session,&session);
    if(message==NULL || !session.established || p.time_domain!=session.domain ||
       p.authority_clock_epoch!=session.authority_epoch || p.state_revision==0U)return false;
    memset(message,0,sizeof *message);
    message->domain=p.time_domain;message->authority_clock_epoch=p.authority_clock_epoch;
    message->state_revision=p.state_revision;message->measurement_id=p.has_value?p.measurement_id:0U;
    message->state=p.state==MEASUREMENT_PRESENTATION_TRACKING?RANGING_STATE_TRACKING:
        p.state==MEASUREMENT_PRESENTATION_HOLD?RANGING_STATE_HOLD:
        p.state==MEASUREMENT_PRESENTATION_REACQUIRE?RANGING_STATE_REACQUIRE:
        p.state==MEASUREMENT_PRESENTATION_STALE?RANGING_STATE_STALE:
        p.state==MEASUREMENT_PRESENTATION_FAULT?RANGING_STATE_FAULT:RANGING_STATE_ACQUIRING;
    message->direction=p.has_value?p.direction:0U;message->quality=p.quality;
    message->temperature_version=p.temperature_version;message->sound_speed_mm_s=p.sound_speed_mm_s;
    message->distance_mm=p.has_value?p.distance_mm:0;
    message->filtered_delta_ns=p.has_value?p.filtered_delta_ns:0;
    message->source_common_ns=p.source_common_ns;message->fresh_deadline_ns=p.fresh_deadline_ns;
    message->display_deadline_ns=p.display_deadline_ns;
    message->calibration_version=p.calibration_version;message->calibration_offset_mm=p.calibration_offset_mm;
    message->reason=p.reason;message->source_key_crc32=p.source_key_crc32;
    message->detection_version=p.detection_version;message->evidence_kind=p.evidence_kind;
    if(p.has_value)message->flags|=RANGING_STATUS_FLAG_HAS_VALUE;
    if(p.fresh)message->flags|=RANGING_STATUS_FLAG_FRESH;
    if(p.formal && p.evidence_kind!=RANGING_EVIDENCE_PROXY)
        message->flags|=RANGING_STATUS_FLAG_IDENTITY_VERIFIED|RANGING_STATUS_FLAG_FORMAL;
    return true;
}

/** @brief 按精确状态确认/500ms失败退避发送；不修改源ID与期限。 */
static void ServiceAuthoritativeStatus(uint32_t now_ms)
{
    const BoardIdentityProfile *profile=BSP_BoardIdentity_Get();
    if(profile==NULL || profile->role!=BOARD_ROLE_A)return;
    RangingStatusMessage message;
    if(!BuildRangingStatusMessage(&message,now_ms))return;
    const RangingStatusAck key={RANGING_STATUS_ACK_OK,message.domain,message.authority_clock_epoch,
        message.state_revision,message.measurement_id};
    if(!RangingExchange_ShouldSend(&ranging_app.exchange,now_ms,&key))return;
    const bool sent=UDPSoundEvent_SendRangingStatus(now_ms,&message);
    RangingExchange_MarkAttempt(&ranging_app.exchange,now_ms,&key,sent);
}

#if defined(SBCG_USE_SOUND_TIME) && defined(SBCG_USE_BOARD_IDENTITY)
static uint32_t last_temperature_send_ms;
/**
 * @brief A板按1Hz把当前有效温度/版本共享给B板。
 * @param now_ms 当前HAL毫秒，无符号差支持回绕。
 * @return 无返回值；只在A角色、温度有效且链路可用时发送，失败下一轮重试。
 */
static void ShareTemperature(uint32_t now_ms)
{
    const BoardIdentityProfile *profile=BSP_BoardIdentity_Get();
    if(profile==NULL || profile->role!=BOARD_ROLE_A)return;
    if((uint32_t)(now_ms-last_temperature_send_ms)<1000U)return;
    last_temperature_send_ms=now_ms;
    TemperatureSnapshot temperature;
    TemperatureService_GetSnapshot(now_ms,&temperature);
    if(temperature.valid)(void)UDPSoundEvent_SendTemperature(now_ms,&temperature);
}
/**
 * @brief B板消费A板共享温度，更新本地显示的远程温度缓存。
 * @param message 本次接收到的非NULL共享温度消息，返回后不保留指针。
 * @return 无返回值；非法字段由TemperatureService_SetRemote拒绝。
 */
static void ApplySharedTemperature(const TemperatureWireMessage *message)
{
    if(message==NULL)return;
    const BoardIdentityProfile *profile=BSP_BoardIdentity_Get();
    if(profile==NULL || profile->role!=BOARD_ROLE_B)return;
    (void)TemperatureService_SetRemote(BSP_Console_GetTickMs(),
        message->temperature_milli_c,message->sound_speed_mm_s,
        message->version,message->source==TEMPERATURE_SOURCE_MANUAL);
}
#endif

/** @brief 以1Hz调用输出权威状态；字段来自同一副本，不以接收/打印时间续租。 */
static void LogRangeResult(uint32_t now_ms)
{
    (void)now_ms;
    MeasurementPresentationSnapshot p;MeasurementResult_GetPresentation(&p);
    const char *state=p.state==MEASUREMENT_PRESENTATION_TRACKING?"TRACKING":
        p.state==MEASUREMENT_PRESENTATION_HOLD?"HOLD":
        p.state==MEASUREMENT_PRESENTATION_REACQUIRE?"REACQUIRE":
        p.state==MEASUREMENT_PRESENTATION_STALE?"STALE":
        p.state==MEASUREMENT_PRESENTATION_FAULT?"FAULT":"ACQUIRING";
    char source[24];FormatUnsigned64(p.source_common_ns,source);
    char line[APP_LOG_LINE_CAPACITY];
    const int length=snprintf(line,sizeof line,
        "$RANGE_RESULT,SCHEMA=2,DOMAIN=%" PRIu32 ",EPOCH=%" PRIu32 ",REV=%" PRIu32
        ",ID=%" PRIu32 ",STATE=%s,MM=%" PRId32 ",DIR=%s,SRC_NS=%s,AGE_MS=%" PRIu32
        ",HAS_VALUE=%u,FRESH=%u,FORMAL=%u,PROV=%u\r\n",
        p.time_domain,p.authority_clock_epoch,p.state_revision,p.measurement_id,state,
        p.has_value?p.distance_mm:0,p.direction==1U?"A":p.direction==2U?"B":"UNKNOWN",
        source,p.source_age_ms,p.has_value?1U:0U,p.fresh?1U:0U,p.formal?1U:0U,p.provisional?1U:0U);
    SendLine(line,length);
}

/** @brief 只输出权威观察，不发送旧MRES；now_ms用于1Hz日志节流。 */
static void ServiceAuthoritativeResult(uint32_t now_ms)
{
    static uint32_t last_range_log_ms;
    if((uint32_t)(now_ms-last_range_log_ms)>=1000U)
    { last_range_log_ms=now_ms;LogRangeResult(now_ms); }
}

#endif

/**
 * @brief 完成应用资源初始化并在已识别的双板角色上注册模式观测。
 * @return 无返回值。
 * @note 仅启动调用。模式观测等待本次启动代次，不在此处推断代次。
 */
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
#ifdef SBCG_USE_MODE_OBSERVATION
    ModeService_Init(profile->role);
    mode_reported = false;
#ifdef SBCG_USE_BOOT_COUNTER
    LoadBootGeneration();
#endif
#endif
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
#ifdef SBCG_USE_ADVANCED_UI
    ClapRanging_Init(&clap_ranging);
#ifdef SBCG_USE_SDRAM
    if(sdram_initialized)WaveformService_Init();
#else
    WaveformService_Init();
#endif
#endif
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
    {
        uint32_t token=0U;
        if(profile->role==BOARD_ROLE_A){
            uint32_t hash=2166136261U;
            for(unsigned i=0U;i<3U;++i){hash=(hash^profile->uid_words[i])*16777619U;}
            hash=(hash^BSP_Console_GetTickMs())*16777619U;
            token=hash!=0U?hash:1U;
        }
        TimeDomain_InitWithId(token);
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
    MeasurementGate_Init(BSP_Console_GetTickMs());
    Calibration_Init();
#ifdef SBCG_USE_CALIBRATION_CONTROL
    CalibrationControl_Init();
    calibration_reported=0U;
#endif
    {
        const StandardSequenceConfig sequence_config={
            STANDARD_SEQUENCE_PERIOD_NS, 20000000U, 5000000000U, 3U, 50U};
        (void)StandardSequence_Init(&standard_sequence,&sequence_config);
        standard_sequence_detected=false;
        memset(&ranging_app,0,sizeof ranging_app);
        RangingSession_Init(&ranging_app.session,profile->role);
        RangingExchange_Init(&ranging_app.exchange);
    }
    if (!UDPSoundEvent_Init(BSP_Console_GetTickMs(), profile,
                            BSP_Console_GetTickMs, SendLine))
    {
        static const char error[] = "$SOUND_EVENT_INIT,RESULT=INIT_ERROR\r\n";
        SendLine(error, (int)(sizeof(error) - 1U));
    }
    else {
        UDPSoundEvent_SetResultSink(PublishMeasurement);
        UDPSoundEvent_SetMeasurementSink(NULL); /* 不以旧MRES回退新状态。 */
        UDPSoundEvent_SetEventSink(HandlePeerSound);
        UDPSoundEvent_SetDomainSink(HandleDomainMessage);
        UDPSoundEvent_SetAckSink(NULL);
        UDPSoundEvent_SetTemperatureSink(ApplySharedTemperature);
        UDPSoundEvent_SetRangingStatusSink(PublishRemoteRangingStatus);
        UDPSoundEvent_SetRangingAckSink(HandleRangingAck);
        UDPSoundEvent_SetRangingSessionSink(HandleRangingSession);
#ifdef SBCG_USE_MODE_OBSERVATION
        UDPSoundEvent_SetModeControlSink(ModeService_OnControl);
#endif
#ifdef SBCG_USE_ADVANCED_UI
        UDPSoundEvent_SetAuxiliarySink(HandleAuxiliary);
#endif
    }
#endif
#endif
#endif
#ifdef SBCG_ITCM_PROBE
    {
        const bool itcm_ok=BSP_ITCM_ProbeRun();
        BSP_ITCMProbeSnapshot probe;
        BSP_ITCM_ProbeGetSnapshot(&probe);
        char itcm_line[APP_LOG_LINE_CAPACITY];
        const int itcm_length=snprintf(itcm_line,sizeof itcm_line,
            "$ITCM_PROBE,RESULT=%s,ADDR=%08" PRIX32 ",BYTES=%" PRIu32
            ",ERRORS=%" PRIu32 ",CRC=%08" PRIX32 "\r\n",
            itcm_ok?"PASS":"FAIL",probe.address,probe.bytes,probe.errors,probe.crc);
        if(itcm_length>0 && (size_t)itcm_length<sizeof itcm_line)
        { SendLine(itcm_line,itcm_length); }
    }
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

#if defined(SBCG_USE_SOUND_TIME) && defined(SBCG_USE_BOARD_IDENTITY)
/**
 * @brief   发布一条已解析声音事件：更新序列/门控并送出事件。
 * @param   notice 非NULL只读事件副本，包含序号、会话、模型帧与PTP时间。
 * @return  无返回值；未分类、无身份或域无效时丢弃。
 * @note    仅CHIRP推进标准序列；CLAP独立发布，不改变标准音门控。事件携带原模型帧。
 */
static void PublishSoundNotice(const SoundEventTimeEvent *notice)
{
    const BoardIdentityProfile *event_profile = BSP_BoardIdentity_Get();
    if (event_profile == NULL) { return; }
    const bool is_chirp=notice->event_class==EVENT_CLASS_CHIRP;
#ifdef SBCG_USE_ADVANCED_UI
    if(!is_chirp && notice->event_class!=EVENT_CLASS_CLAP)return;
#else
    if(!is_chirp)return;
#endif
    if(is_chirp)
    {
        if(event_profile->role==BOARD_ROLE_A)
            MeasurementGate_OnChirp(BSP_Console_GetTickMs(),notice->session);
        const bool sequence_detected=StandardSequence_Push(
            &standard_sequence,notice->time_ns,notice->quality);
        if(sequence_detected && event_profile->role==BOARD_ROLE_A)
            MeasurementGate_OnSequence(BSP_Console_GetTickMs());
        if(sequence_detected!=standard_sequence_detected){
            StandardSequenceSnapshot sequence;StandardSequence_Get(&standard_sequence,&sequence);
            char sequence_line[256];
            (void)snprintf(sequence_line,sizeof sequence_line,
                "$STANDARD_SEQUENCE,STATUS=%s,EVENTS=%u,ERR_NS=%" PRIu32 ",QUALITY=%u,DIAG=1,LOCAL=1,SYNC=0\r\n",
                sequence.detected?"DETECTED":"RESET",(unsigned)sequence.events,
                (uint32_t)sequence.max_period_error_ns,(unsigned)sequence.quality);
            SendLine(sequence_line,(int)strlen(sequence_line));
            standard_sequence_detected=sequence_detected;
        }
    }
    TimeDomainSnapshot domain;
    TimeDomain_Get(&domain);
    if (!domain.valid || domain.id==0U) { return; }
    SoundEventRecord event = {0};
    event.valid = true;
    event.type = is_chirp?SOUND_EVENT_CHIRP:SOUND_EVENT_CLAP;
    event.quality = notice->quality;
    event.side = event_profile->role == BOARD_ROLE_A ? SOUND_SIDE_A : SOUND_SIDE_B;
    event.time_domain = domain.id;
    event.session = notice->session;
    event.sequence = notice->sequence;
    event.time_ns = notice->time_ns;
    event.frame = notice->frame;
    event.fraction_q16 = notice->fraction_q16;
    SoundDetectorServiceToaCandidates candidates;
    if (SoundDetectorService_GetToaCandidates(notice->sequence, &candidates))
    {
        event.toa_candidate_count = candidates.count;
        for (uint8_t i = 0U; i < SOUND_EVENT_MAX_TOA_CANDIDATES; ++i)
            event.toa_candidate_q16[i] = candidates.offset_q16[i];
    }
    (void)UDPSoundEvent_Send(BSP_Console_GetTickMs(), &event);
}
/** @brief 事件发布延迟：等待同一次扫频的波形捕获完成，避免窗口晚于事件。 */
#define SOUND_NOTICE_DELAY_MS 700U
#define SOUND_NOTICE_MAX 4U
static SoundEventTimeEvent notice_pending[SOUND_NOTICE_MAX];
static uint32_t notice_pending_ms[SOUND_NOTICE_MAX];
static uint8_t notice_pending_count;
/** @brief 暂存或立即发布一条事件；队满时立即发布最旧一条以保序。 */
static void SoundNoticeEnqueue(const SoundEventTimeEvent *notice)
{
    if (notice_pending_count >= SOUND_NOTICE_MAX)
    {
        PublishSoundNotice(&notice_pending[0]);
        for (uint8_t i = 1U; i < notice_pending_count; ++i)
        {
            notice_pending[i - 1U] = notice_pending[i];
            notice_pending_ms[i - 1U] = notice_pending_ms[i];
        }
        --notice_pending_count;
    }
    notice_pending[notice_pending_count] = *notice;
    notice_pending_ms[notice_pending_count] = BSP_Console_GetTickMs();
    ++notice_pending_count;
}
/** @brief 到达延迟后按序发布；无返回值。 */
static void SoundNoticeDrain(uint32_t now_ms)
{
    uint8_t keep = 0U;
    for (uint8_t i = 0U; i < notice_pending_count; ++i)
    {
        if ((uint32_t)(now_ms - notice_pending_ms[i]) >= SOUND_NOTICE_DELAY_MS)
        { PublishSoundNotice(&notice_pending[i]); }
        else
        {
            if (keep != i)
            {
                notice_pending[keep] = notice_pending[i];
                notice_pending_ms[keep] = notice_pending_ms[i];
            }
            ++keep;
        }
    }
    notice_pending_count = keep;
}
#endif
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
 *          DHT低态仍逐轮将最新域资格复制给UDP层；模式控制收包只暂存，前台处理延后至允许时。
 *          A/B音频会话变化先撤销UDP旧候选；声音事件取出前刷新域，配对不跨资格边界。
 *          WAVE全滞后搜索在网络及测距处理后每轮只推进一批，避免回调重复计算。
 *          CHIRP构建额外累计最大一轮LOOP_MS及同轮AUDIO/UI/OTHER分解；LOOP与Chirp
 *          GAP_MS采样点不同，只作同一调度周期的近似对照，不用于相减推断未归因时间。
 *          PROFILE2分段计时仅随CHIRP诊断启用；其他构建不读取或更新该诊断状态。
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
    uint32_t prof_begin_ms = loop_begin_ms;
#endif
#ifdef SBCG_USE_LWIP
    /* Every loop, before logs: service RX, protocol timers and link changes. */
#ifdef SBCG_USE_CHIRP
    prof_begin_ms = BSP_Console_GetTickMs();
#endif
    MX_LWIP_Process();
#ifdef SBCG_USE_CHIRP
    { const uint32_t d = BSP_Console_GetTickMs() - prof_begin_ms;
      if (d > prof_lwip_max_ms) { prof_lwip_max_ms = d; } }
#endif
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
#ifdef SBCG_USE_CHIRP
    prof_begin_ms = BSP_Console_GetTickMs();
#endif
    TemperatureService_Process(BSP_Console_GetTickMs());
#ifdef SBCG_USE_CHIRP
    { const uint32_t d = BSP_Console_GetTickMs() - prof_begin_ms;
      if (d > prof_temp_max_ms) { prof_temp_max_ms = d; } }
#endif
    TemperatureConsole_Process(BSP_Console_GetTickMs());
#if defined(SBCG_USE_SOUND_TIME) && defined(SBCG_USE_BOARD_IDENTITY)
    ShareTemperature(BSP_Console_GetTickMs());
#endif
#ifdef SBCG_USE_BOARD_SYNC
    /* 使用本轮温度状态：DATA拉低时延后主动PTP发送和硬件校正。 */
    TimeSync_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
#ifdef SBCG_USE_PPS
    /* DHT拉低只禁止新目标排程；安全截止及已到达目标的消费每轮仍须执行。 */
    PpsService_Process(BSP_Console_GetTickMs(), !TemperatureService_IsStartLowActive());
    {
        TimeSyncSnapshot sync_snapshot;PpsServiceSnapshot pps_snapshot;
        TimeSync_GetSnapshot(BSP_Console_GetTickMs(), &sync_snapshot);
        if(PpsService_GetSnapshot(BSP_Console_GetTickMs(), &pps_snapshot))
            TimeDomain_UpdateWithPps(&sync_snapshot,&pps_snapshot);
        else TimeDomain_Invalidate();
    }
#else
    {
        TimeSyncSnapshot sync_snapshot;
        TimeSync_GetSnapshot(BSP_Console_GetTickMs(), &sync_snapshot);
        TimeDomain_Update(&sync_snapshot);
    }
#endif
#endif
    #if defined(SBCG_USE_SOUND_TIME) && defined(SBCG_USE_BOARD_IDENTITY)
    {
        TimeDomainSnapshot event_domain;
        TimeDomain_Get(&event_domain);
        UDPSoundEvent_SetTimeDomain(event_domain.valid?event_domain.id:0U);
        RefreshRanging(BSP_Console_GetTickMs());
    }
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
#ifdef SBCG_USE_CHIRP
    prof_begin_ms = BSP_Console_GetTickMs();
#endif
    /* 单次消费锚点，同一副本分发给旧自检与候选模型，DHT低态不发布。 */
    if(!TemperatureService_IsStartLowActive())
    {
        AudioTimeAnchorObservation observation;
        if(AudioTimeAnchor_TakeObservation(&observation))
        {
            const uint32_t observation_ms=BSP_Console_GetTickMs();
            AudioTimeMapMonitor_Observe(observation_ms,&observation);
            SoundEventTime_Observe(BSP_Console_GetTickMs(),&observation);
#ifdef SBCG_USE_ADVANCED_UI
            WaveformService_ObserveAnchor(&observation);
#endif
        }
        BSP_AudioSnapshot snapshot;
        BSP_Audio_GetSnapshot(&snapshot);
#ifdef SBCG_USE_BOARD_IDENTITY
        if(ranging_app.local_audio_session!=0U && snapshot.session!=ranging_app.local_audio_session)
            UDPSoundEvent_SetTimeDomain(0U);
#endif
        SoundEventTime_Process(BSP_Console_GetTickMs(),&snapshot);
#ifdef SBCG_USE_BOARD_IDENTITY
        ServiceDomain(BSP_Console_GetTickMs());
#endif
        /* 按本板身份与当前有效共同域发布成功TOA；域资格不代表外部同步精度验收，
           诊断输出仍保持DIAG/LOCAL=1、SYNC=0的边界。 */
#if defined(SBCG_USE_BOARD_IDENTITY)
        SoundEventTimeEvent notice;
        while (SoundEventTime_TakeEvent(&notice)) { SoundNoticeEnqueue(&notice); }
        SoundNoticeDrain(BSP_Console_GetTickMs());
#else
        SoundEventTimeEvent notice;
        while (SoundEventTime_TakeEvent(&notice)) { (void)notice; }
#endif
        UDPSoundEvent_Process(BSP_Console_GetTickMs());
#ifdef SBCG_USE_BOARD_IDENTITY
        RefreshRanging(BSP_Console_GetTickMs());
        RangingSessionMessage outgoing;
        if(RangingSession_TakeOutgoing(&ranging_app.session,BSP_Console_GetTickMs(),&outgoing))
            (void)UDPSoundEvent_SendRangingSession(BSP_Console_GetTickMs(),&outgoing);
        ServiceAuthoritativeResult(BSP_Console_GetTickMs());
        ServiceAuthoritativeStatus(BSP_Console_GetTickMs());
#ifdef SBCG_USE_ADVANCED_UI
        ServiceAuxiliary(BSP_Console_GetTickMs());
        const uint32_t wave_begin_ms=BSP_Console_GetTickMs();
        WaveformService_ProcessPeriod();
        const uint32_t wave_elapsed_ms=BSP_Console_GetTickMs()-wave_begin_ms;
        if(wave_elapsed_ms>wave_update_max_ms)wave_update_max_ms=wave_elapsed_ms;
#endif
#ifdef SBCG_USE_MODE_OBSERVATION
        /* UDP提交可能等待ETH发送，本轮音频块归还和原有业务处理后才推进观测。 */
        ServiceModeObservation(BSP_Console_GetTickMs());
#endif
#endif
    }
#ifdef SBCG_USE_CHIRP
    { const uint32_t d = BSP_Console_GetTickMs() - prof_begin_ms;
      if (d > prof_sound_max_ms) { prof_sound_max_ms = d; } }
#endif
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
            "$PROFILE2,LWIP_MS=%" PRIu32 ",TEMP_MS=%" PRIu32 ",SND_MS=%" PRIu32 "\r\n",
            prof_lwip_max_ms, prof_temp_max_ms, prof_sound_max_ms);
        if (length > 0 && (size_t)length < sizeof(line)) { SendLine(line, length); }
        prof_lwip_max_ms = prof_temp_max_ms = prof_sound_max_ms = 0U;
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
#ifdef SBCG_USE_ADVANCED_UI
        WaveformSnapshot wave_ui;
        WaveformService_GetSnapshot(&wave_ui);
        length = snprintf(line, sizeof(line),
            "$WAVE_UI,VALID=%u,SYNC=%u,PERIOD=%u,SRC=%u,MHZ=%" PRIu32 ",SPAN_NS=%lu"
            ",SEQ=%" PRIu32 ",STATE=%u,CAND_MHZ=%" PRIu32 ",AGE_MS=%" PRIu32
            ",CONF=%u,WHY=%u,WORK_MS=%" PRIu32 "\r\n",
            wave_ui.valid ? 1U : 0U, wave_ui.synced ? 1U : 0U,
            wave_ui.period_valid ? 1U : 0U, (unsigned)wave_ui.source, wave_ui.period_milli_hz,
            (unsigned long)wave_ui.window_span_ns, wave_ui.plan_sequence, (unsigned)wave_ui.render_state,
            wave_ui.candidate_milli_hz, wave_ui.period_age_ms,
            (unsigned)wave_ui.confirmation_count, (unsigned)wave_ui.reason,wave_update_max_ms);
        if (length > 0 && (size_t)length < sizeof(line)) { SendLine(line, length); }
        wave_update_max_ms=0U;
#endif
    }
#endif
#endif
}
