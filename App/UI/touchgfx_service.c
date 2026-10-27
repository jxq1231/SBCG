/**
 * @file        touchgfx_service.c
 * @brief       在前台协调TouchGFX初始化、真实NoOS调度和一秒业务文本。
 * @details     生成主循环入口保持空操作；同一缓冲同时具备实际绘制完成与可见证据才允许前台开启背光。
 * @note        IRQ通知只作无锁原子交接，不进行绘图、GPIO或日志；不读PCM/不消费候选队列。
 */
#include "touchgfx_service.h"
#include "status_snapshot.h"
#include "BSP/bsp_touchgfx_display.h"
#include "BSP/bsp_console.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>

#define UI_REFRESH_MS 1000U
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Display notifications require lock-free word atomics");
_Static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "Display notifications require lock-free flag atomics");
extern void MX_TouchGFX_Init(void);
extern void __real_MX_TouchGFX_Process(void);
static UI_TouchGFXSnapshot state;
static UI_StatusText prepared_text;
static bool attempted;
static uint32_t last_update_ms, last_report_ms;
/** UI_TouchGFX_Process入口到各返回路径的最大HAL毫秒；不含Report()日志格式化/发送。 */
static uint32_t max_update_ms;
static void (*emit_line)(const char *, int);
static _Atomic(uint32_t) completed_buffer_mask;
static _Atomic(bool) invalid_visible_address;
static _Atomic(uint32_t) pending_visible_address;

/** @brief 记录UI前台入口begin_ms到当前的饱和最大HAL毫秒耗时；无返回值，仅前台。 */
static void RecordUpdateTime(uint32_t begin_ms)
{
    const uint32_t elapsed = BSP_Console_GetTickMs() - begin_ms;
    if (elapsed > max_update_ms) { max_update_ms = elapsed; }
}

/**
 * @brief 将固定缓冲字节地址映射为独立完成标志位。
 * @param address 字节地址，A/B以外包括0均不产生有效位。
 * @return A为位0，B为位1，其余为0；不访问外部内存。
 */
static uint32_t BufferBit(uint32_t address)
{
    return address == BSP_TOUCHGFX_FRAME_A ? UINT32_C(1) :
        address == BSP_TOUCHGFX_FRAME_B ? UINT32_C(2) : UINT32_C(0);
}

/**
 * @brief 屏蔽CubeMX自动主循环调用，让DHT保护与音频调度保持服务所有权。
 * @return 无返回值，无副作用。
 * @note 必须通过--wrap链接；真正框架入口仅在UI_TouchGFX_Process中调用。
 */
void __wrap_MX_TouchGFX_Process(void) {}

/**
 * @brief 输出一条有界UI诊断，FRAMES与约1Hz的UPDATES分别计数。
 * @param now_ms 当前HAL毫秒，最多每1000ms输出一次。
 * @param lcd 非NULL前台BSP副本，不保留地址。
 * @return 无返回值；数据来自软件副本，不代表实物验收。
 */
static void Report(uint32_t now_ms, const BSP_TouchGFXDisplaySnapshot *lcd)
{
    if (emit_line == NULL || (uint32_t)(now_ms - last_report_ms) < UI_REFRESH_MS) { return; }
    last_report_ms = now_ms;
    const char *name = lcd->status == BSP_TOUCHGFX_RUNNING ? "RUNNING" :
        lcd->status == BSP_TOUCHGFX_READY ? "READY" : lcd->status == BSP_TOUCHGFX_ERROR ? "ERROR" : "OFF";
    const bool valid = state.data_generation != 0U && state.page.temperature_valid;
    const uint32_t errors = lcd->underrun_errors | lcd->transfer_errors | lcd->hal_errors | lcd->config_errors;
    char line[256];
    const int length = snprintf(line, sizeof line,
        "$UI,STATE=%s,FRAMES=%" PRIu32 ",ERRORS=%u,DISCARDS=0,CAPTURE_MS=%" PRIu32
        ",ROLE=%s,NET=%s,TEMP_SOURCE=%s,TEMP_VALID=%u,TEMP_MC=%" PRId32 ",C_MM_S=%" PRIu32
        ",BACKEND=TOUCHGFX,UPDATES=%" PRIu32 ",UPDATE_MS=%" PRIu32 "\r\n",
        name, state.frames, errors != 0U ? 1U : 0U, state.page.captured_ms,
        state.page.role == UI_ROLE_A ? "A" : state.page.role == UI_ROLE_B ? "B" : "UNKNOWN",
        state.page.network == UI_NET_UP ? "UP" : state.page.network == UI_NET_DOWN ? "DOWN" : "UNKNOWN",
        state.page.temperature_manual ? "MANUAL" : "AUTO", valid ? 1U : 0U,
        valid ? state.page.temperature_milli_c : 0, valid ? state.page.sound_speed_mm_s : 0U,
        state.data_generation, max_update_ms);
    if (length > 0 && (size_t)length < sizeof line) { emit_line(line, length); }
    const bool has_measurement = state.page.measurement_status == 1U;
    const bool fresh_measurement = state.page.measurement_valid;
    const char *measurement_state = fresh_measurement ? "VALID" :
        has_measurement ? "STALE" :
        state.page.measurement_status == 2U ? "REJECTED" : "NONE";
    const char *direction = state.page.measurement_direction == 1U ? "A" :
        state.page.measurement_direction == 2U ? "B" : "UNKNOWN";
    const uint32_t measurement_age_ms = has_measurement ?
        (uint32_t)(state.page.captured_ms - state.page.measurement_captured_ms) : 0U;
    char range_line[256];
    const int range_length = snprintf(range_line, sizeof range_line,
        "$UI_RANGE,SCHEMA=1,REV=NA,ID=%" PRIu32 ",STATE=%s,MM=%" PRId32
        ",DIR=%s,AGE_MS=%" PRIu32 ",HAS_VALUE=%u,FRESH=%u\r\n",
        state.page.measurement_id, measurement_state,
        has_measurement ? state.page.distance_mm : 0, direction, measurement_age_ms,
        has_measurement ? 1U : 0U, fresh_measurement ? 1U : 0U);
    if (range_length > 0 && (size_t)range_length < sizeof range_line) {
        emit_line(range_line, range_length);
    }
}

/**
 * @brief 前台读取BSP状态并锁存错误，保持服务停止状态。
 * @param lcd 非NULL输出，调用方持有副本。
 * @return 服务及BSP仍可调度为true；错误/未初始化为false。
 */
static bool DisplayReady(BSP_TouchGFXDisplaySnapshot *lcd)
{
    BSP_TouchGFXDisplay_GetSnapshot(lcd);
    if (lcd->status == BSP_TOUCHGFX_ERROR) { state.failed = true; state.ready = false; state.visible = false; }
    return state.ready && !state.failed;
}

/** @brief 按头文件约定在前台单次启动显示及框架。@param now_ms HAL毫秒。@param log 可空同步日志回调。@return 无返回值。 */
void UI_TouchGFX_Init(uint32_t now_ms, void (*log)(const char *, int))
{
    if (attempted) { return; }
    attempted = true;
    emit_line = log;
    max_update_ms = 0U;
    last_update_ms = now_ms - UI_REFRESH_MS;
    last_report_ms = now_ms - UI_REFRESH_MS;
    state.initialized = BSP_TouchGFXDisplay_Init();
    state.ready = state.initialized;
    if (!state.ready) {
        BSP_TouchGFXDisplaySnapshot lcd;
        (void)DisplayReady(&lcd);
        return;
    }
    MX_TouchGFX_Init();
}

/** @brief 按头文件约定门控前台调度。@param now_ms HAL毫秒。@param allow DHT允许标志。@return 无返回值，不自行等待VSYNC。 */
void UI_TouchGFX_Process(uint32_t now_ms, bool allow)
{
    if (!attempted || !allow) { return; }
    const uint32_t begin_ms = BSP_Console_GetTickMs();
    if (state.ready) { BSP_TouchGFXDisplay_Process(); }
    BSP_TouchGFXDisplaySnapshot lcd;
    if (!DisplayReady(&lcd)) { RecordUpdateTime(begin_ms); Report(now_ms, &lcd); return; }
    if (atomic_exchange_explicit(&invalid_visible_address, false, memory_order_acq_rel)) {
        (void)BSP_TouchGFXDisplay_Reveal(0U);
        (void)DisplayReady(&lcd);
        RecordUpdateTime(begin_ms);
        Report(now_ms, &lcd);
        return;
    }
    if ((uint32_t)(now_ms - last_update_ms) >= UI_REFRESH_MS) {
        UI_StatusSnapshot_Read(now_ms, &state.page);
        if (UI_StatusFormat(&state.page, now_ms, &prepared_text)) {
            ++state.data_generation;
            if (state.data_generation == 0U) { ++state.data_generation; }
        }
        last_update_ms = now_ms;
    }
    __real_MX_TouchGFX_Process();
    /* 框架处理期间IRQ可能锁存硬件错误，因此展示与日志之前再次检查。 */
    BSP_TouchGFXDisplay_Process();
    if (!DisplayReady(&lcd)) { RecordUpdateTime(begin_ms); Report(now_ms, &lcd); return; }
    /* 可见IRQ允许先于完成回调；保留最近可见地址，等待同一缓冲的实际绘制完成位。 */
    const uint32_t visible = atomic_load_explicit(&pending_visible_address, memory_order_acquire);
    const uint32_t completed = atomic_load_explicit(&completed_buffer_mask, memory_order_acquire);
    if ((completed & BufferBit(visible)) != 0U &&
        (!state.visible || state.visible_address != visible) && BSP_TouchGFXDisplay_Reveal(visible)) {
        state.visible = true;
        state.visible_address = visible;
    }
    (void)DisplayReady(&lcd);
    RecordUpdateTime(begin_ms);
    Report(now_ms, &lcd);
}

/** @brief 复制已准备的文本和版本。@param out 非NULL文本输出。@param generation 非NULL版本输出。@return 尚无数据或参数错误为false。 */
bool UI_TouchGFX_GetText(UI_StatusText *out, uint32_t *generation)
{
    if (out == NULL || generation == NULL || state.data_generation == 0U) { return false; }
    *out = prepared_text;
    *generation = state.data_generation;
    return true;
}

/**
 * @brief 前台累计endFrame，并原子记录实际完成绘制的缓冲。
 * @param rendered_address 父类调用前捕获的绘制地址；0仅计帧，A/B发布对应完成位。
 * @return 无返回值；计数饱和，不打开背光，非法非零地址交前台错误处理。
 * @note 完成位允许与更早到达的可见通知关联，不要求后续页面再次变脏。
 */
void UI_TouchGFX_FrameCompleted(uint32_t rendered_address)
{
    if (!state.ready || state.failed) { return; }
    if (state.frames != UINT32_MAX) { ++state.frames; }
    const uint32_t bit = BufferBit(rendered_address);
    if (bit != 0U) {
        (void)atomic_fetch_or_explicit(&completed_buffer_mask, bit, memory_order_release);
    } else if (rendered_address != 0U) {
        atomic_store_explicit(&invalid_visible_address, true, memory_order_release);
    }
}

/** @brief IRQ/前台仅原子保存可见通知。@param address 字节地址A/B，非法地址交前台报错。@return 无返回值，不访问GPIO/日志。 */
void UI_TouchGFX_BufferVisible(uint32_t address)
{
    if (address != BSP_TOUCHGFX_FRAME_A && address != BSP_TOUCHGFX_FRAME_B) {
        atomic_store_explicit(&invalid_visible_address, true, memory_order_release);
        return;
    }
    atomic_store_explicit(&pending_visible_address, address, memory_order_release);
}

/** @brief 前台复制软件诊断。@param out 可空输出副本。@return 无返回值，不触碰硬件。 */
void UI_TouchGFX_GetSnapshot(UI_TouchGFXSnapshot *out)
{ if (out != NULL) { *out = state; } }
