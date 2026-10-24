/**
 * @file        status_service.c
 * @brief       将真实业务只读状态绘制到LCD独占后台缓冲。
 * @details     首帧及每秒一轮快照固定后分34次绘制，每次最多八行；垂直消隐换页由BSP管理。
 * @note        只在音频归还后的前台调用；DHT低态跳过，超2000ms放弃未发布帧，无IRQ/DMA/PCM消费。
 */
#include "status_service.h"
#include "status_screen.h"
#include "BSP/bsp_lcd.h"
#include "BSP/bsp_board_identity.h"
#include "BSP/bsp_audio.h"
#include "BSP/bsp_console.h"
#include "Network/ethernet_monitor.h"
#include "Sync/time_sync.h"
#include "Temperature/temperature_service.h"
#include "Detector/sound_detector_service.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define UI_REFRESH_MS 1000U
#define UI_FRAME_MAX_AGE_MS 2000U
#define UI_ROWS_PER_SLICE 8U
static UI_StatusServiceSnapshot state;
static UI_StatusSnapshot published;
static UI_Canvas canvas;
static bool attempted, have_published;
static uint32_t last_started_ms, last_report_ms, last_presented;
static void (*emit_line)(const char *,int);

/**
 * @brief 一次性复制业务副本，不读PCM或消费任一队列。
 * @param now_ms 本次快照HAL毫秒，所有字段共用此时效参考。
 * @param out 非NULL输出；无效字段保持占位资格，不保留业务指针。
 * @return 无返回值；A参考不产生伪造的零偏差，B只显示新鲜估计。
 */
static void Capture(uint32_t now_ms, UI_StatusSnapshot *out)
{
    *out = (UI_StatusSnapshot){.captured_ms=now_ms};
    const BoardIdentityProfile *identity=BSP_BoardIdentity_Get();
    if (identity != NULL) {
        out->role=identity->role==BOARD_ROLE_A?UI_ROLE_A:identity->role==BOARD_ROLE_B?UI_ROLE_B:UI_ROLE_UNKNOWN;
    }
    EthernetMonitorSnapshot network; EthernetMonitor_GetSnapshot(now_ms,&network);
    out->network=!network.valid?UI_NET_UNKNOWN:network.link_up?UI_NET_UP:UI_NET_DOWN;
    TimeSyncSnapshot sync; TimeSync_GetSnapshot(now_ms,&sync);
    if (sync.configured) {
        if (sync.faulted) { out->sync=UI_SYNC_FAULT; }
        else if (out->role==UI_ROLE_A && sync.role==BOARD_ROLE_A) { out->sync=UI_SYNC_LOCAL; }
        else if (out->role==UI_ROLE_B && sync.role==BOARD_ROLE_B) {
            out->sync=sync.state==CLOCK_SERVO_FAULT?UI_SYNC_FAULT:
                sync.state==CLOCK_SERVO_HOLDOVER?UI_SYNC_HOLDOVER:
                sync.state==CLOCK_SERVO_UNSYNCED?UI_SYNC_UNSYNCED:
                sync.internal_locked?UI_SYNC_INTERNAL:
                sync.offset_valid?UI_SYNC_TRACKING:UI_SYNC_ACQUIRING;
            out->offset_valid=sync.offset_valid; out->offset_x2_ns=sync.offset_x2_ns;
        }
    }
    TemperatureSnapshot temperature; TemperatureService_GetSnapshot(now_ms,&temperature);
    out->temperature_manual=temperature.manual; out->temperature_valid=temperature.valid;
    if (temperature.valid) {
        out->temperature_milli_c=temperature.temperature_milli_c;
        out->sound_speed_mm_s=temperature.sound_speed_mm_s;
    }
    BSP_AudioSnapshot audio; BSP_Audio_GetSnapshot(&audio);
    /* IRQ可在入口now_ms之后发布新回调；必须在复制快照后取时，避免把1000-1001当超龄。 */
    const uint32_t audio_now_ms=BSP_Console_GetTickMs();
    out->audio_running=audio.running; out->audio_dropped=audio.dropped_blocks;
    out->audio_errors=audio.error_count;
    if (out->audio_errors==0U && (audio.hal_error|audio.dma_error|audio.start_error)!=0U) { out->audio_errors=1U; }
    out->audio_valid=audio.running && audio.session!=0U && audio.callback_count!=0U &&
        (uint32_t)(audio_now_ms-audio.last_callback_ms)<1000U && audio.error_count==0U &&
        audio.hal_error==0U && audio.dma_error==0U && audio.dropped_blocks==0U;
    SoundDetectorServiceSnapshot detector; SoundDetectorService_GetSnapshot(&detector);
    if (detector.configured) { out->candidates=detector.candidates; }
    /* 尚无双板声学测距结果：measurement_valid保持false，方向和质量由文本层占位。 */
}

/**
 * @brief 输出一条已发布页面与LCD状态的有界诊断。
 * @param now_ms 当前HAL毫秒，最多每1000ms输出。
 * @param lcd 非NULL只读BSP副本，不保存其地址。
 * @return 无返回值；页面未发布时只标明无有效温度，不声称实屏已验收。
 */
static void Report(uint32_t now_ms, const BSP_LcdSnapshot *lcd)
{
    if (emit_line==NULL || (uint32_t)(now_ms-last_report_ms)<UI_REFRESH_MS) { return; }
    last_report_ms=now_ms;
    const char *name=lcd->status==BSP_LCD_RUNNING?"RUNNING":lcd->status==BSP_LCD_READY?"READY":
        lcd->status==BSP_LCD_ERROR?"ERROR":"OFF";
    char line[256];
    const bool valid=have_published && published.temperature_valid;
    const uint32_t errors=lcd->underrun_errors|lcd->transfer_errors|lcd->reload_timeouts|lcd->config_errors|lcd->hal_errors;
    const int length=snprintf(line,sizeof line,
        "$UI,STATE=%s,FRAMES=%" PRIu32 ",ERRORS=%u,DISCARDS=%" PRIu32
        ",CAPTURE_MS=%" PRIu32 ",ROLE=%s,NET=%s,TEMP_SOURCE=%s,TEMP_VALID=%u,TEMP_MC=%" PRId32 ",C_MM_S=%" PRIu32 "\r\n",
        name,lcd->presented_frames,errors!=0U?1U:0U,state.discarded_frames,published.captured_ms,
        published.role==UI_ROLE_A?"A":published.role==UI_ROLE_B?"B":"UNKNOWN",
        published.network==UI_NET_UP?"UP":published.network==UI_NET_DOWN?"DOWN":"UNKNOWN",
        published.temperature_manual?"MANUAL":"AUTO",valid?1U:0U,
        valid?published.temperature_milli_c:0,valid?published.sound_speed_mm_s:0U);
    if (length>0 && (size_t)length<sizeof line) { emit_line(line,length); }
}

/** @brief 初始化一次软件调度并申请LCD服务。@param now_ms HAL毫秒。@param log 可空日志出口，返回前复制文本。@return 无返回值；失败不自动重试。 */
void UI_StatusService_Init(uint32_t now_ms, void (*log)(const char *,int))
{
    if (attempted) { return; }
    attempted=true; emit_line=log;
    last_started_ms=now_ms-UI_REFRESH_MS; last_report_ms=now_ms-UI_REFRESH_MS;
    state.initialized=BSP_LCD_Init();
}

/** @brief 前台轮询和绘制一片。@param now_ms HAL毫秒。@param allow_work false不执行本轮。@return 无返回值；失败不重置BSP或改业务状态。 */
void UI_StatusService_Process(uint32_t now_ms, bool allow_work)
{
    if (!attempted || !allow_work) { return; }
    if (state.initialized) { BSP_LCD_Process(now_ms); }
    BSP_LcdSnapshot lcd; BSP_LCD_GetSnapshot(&lcd);
    if (lcd.presented_frames!=last_presented) {
        published=state.page; have_published=true; last_presented=lcd.presented_frames;
    }
    Report(now_ms,&lcd);
    if (!state.initialized || lcd.status==BSP_LCD_ERROR) { return; }
    if (state.rendering && (uint32_t)(now_ms-state.page.captured_ms)>=UI_FRAME_MAX_AGE_MS) {
        BSP_LCD_CancelFrame(); state.rendering=false; state.rendered_rows=0U;
        if (state.discarded_frames!=UINT32_MAX) { ++state.discarded_frames; }
        return;
    }
    if (!state.rendering) {
        if (lcd.pending_address!=0U || (uint32_t)(now_ms-last_started_ms)<UI_REFRESH_MS) { return; }
        uint16_t *pixels=BSP_LCD_BeginFrame();
        if (pixels==NULL) { return; }
        if (!UI_CanvasInit(&canvas,pixels,BSP_LCD_WIDTH*BSP_LCD_HEIGHT,BSP_LCD_WIDTH,BSP_LCD_HEIGHT,BSP_LCD_WIDTH)) {
            BSP_LCD_CancelFrame(); return;
        }
        Capture(now_ms,&state.page); last_started_ms=now_ms;
        state.rendered_rows=0U; state.rendering=true;
    }
    const uint16_t remaining=(uint16_t)(UI_SCREEN_HEIGHT-state.rendered_rows);
    const uint16_t rows=remaining<UI_ROWS_PER_SLICE?remaining:UI_ROWS_PER_SLICE;
    if (!UI_StatusRenderRows(&canvas,&state.page,state.page.captured_ms,state.rendered_rows,rows)) {
        BSP_LCD_CancelFrame(); state.rendering=false; return;
    }
    state.rendered_rows=(uint16_t)(state.rendered_rows+rows);
    if (state.rendered_rows==UI_SCREEN_HEIGHT) {
        state.rendering=false;
        if (!BSP_LCD_Present(now_ms)) { BSP_LCD_CancelFrame(); }
    }
}

/** @brief 复制软件状态。@param out 可空调用方输出，不保留地址。@return 无返回值，无硬件访问。 */
void UI_StatusService_GetSnapshot(UI_StatusServiceSnapshot *out)
{ if (out!=NULL) { *out=state; } }
