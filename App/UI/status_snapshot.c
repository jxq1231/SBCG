/**
 * @file        status_snapshot.c
 * @brief       为两种显示后端复制业务只读状态。
 * @details     汇集身份、网络、同步、温度、音频和检测计数；不消费任何业务队列。
 * @note        前台使用；音频复制后重新取时避免IRQ新回调造成无符号年龄误判。
 */
#include "status_snapshot.h"
#include "BSP/bsp_board_identity.h"
#include "BSP/bsp_audio.h"
#include "BSP/bsp_console.h"
#include "Network/ethernet_monitor.h"
#include "Sync/time_sync.h"
#include "Temperature/temperature_service.h"
#include "Measure/measurement_result.h"
#include "Detector/sound_detector_service.h"
#include <stddef.h>

/**
 * @brief 一次性复制业务副本，不读PCM或消费任一队列。
 * @param now_ms 本次快照HAL毫秒，所有字段共用此时效参考。
 * @param out 可空输出；无效字段保持占位资格，不保留业务指针。
 * @return 无返回值；A参考不产生伪造的零偏差，B只显示新鲜估计。
 */
void UI_StatusSnapshot_Read(uint32_t now_ms, UI_StatusSnapshot *out)
{
    if (out == NULL) { return; }
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
    MeasurementPresentationSnapshot presentation={0};
    MeasurementResult_GetPresentation(&presentation);
    out->measurement_state=(uint8_t)presentation.state;
    out->measurement_fresh=presentation.fresh;
    out->measurement_formal=presentation.formal;
    out->measurement_revision=presentation.state_revision;
    out->measurement_captured_ms=presentation.captured_ms;
    out->measurement_status=presentation.has_value?1U:
        presentation.state==MEASUREMENT_PRESENTATION_STALE?3U:0U;
    if(presentation.has_value){
        out->measurement_valid=true;
        out->distance_mm=presentation.distance_mm;
        out->measurement_direction=presentation.direction;
        out->measurement_quality=presentation.quality;
        out->measurement_id=presentation.measurement_id;
    }
}

