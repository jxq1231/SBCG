/**
 * @file status_view.h
 * @brief RGB565与未来TouchGFX共享的只读状态快照和文本接口。
 * @details 无HAL/外设依赖，未知或超龄字段不伪装为零；模型状态不等同外部同步精度。
 * @note 前台使用，字符串由调用方持有，不读取当前业务全局变量。
 */
#ifndef SBCG_STATUS_VIEW_H
#define SBCG_STATUS_VIEW_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { UI_ROLE_UNKNOWN,UI_ROLE_A,UI_ROLE_B } UI_Role;
typedef enum { UI_NET_UNKNOWN,UI_NET_DOWN,UI_NET_UP } UI_Network;
typedef enum { UI_SYNC_UNKNOWN,UI_SYNC_LOCAL,UI_SYNC_TRACKING,UI_SYNC_INTERNAL,
 UI_SYNC_ACQUIRING,UI_SYNC_HOLDOVER,UI_SYNC_UNSYNCED,UI_SYNC_FAULT } UI_Sync;
/** @brief 数值携带单位与有效标志；captured_ms为HAL毫秒，偏差为两倍纳秒，不以内部锁代替外部验证。 */
typedef struct {
 uint32_t captured_ms;
 UI_Role role; UI_Network network; UI_Sync sync;
 bool offset_valid,temperature_valid,audio_valid,measurement_valid;
 int64_t offset_x2_ns;
 int32_t temperature_milli_c,distance_mm;
 uint32_t sound_speed_mm_s,audio_dropped,audio_errors;
 uint64_t candidates;
 bool audio_running,temperature_manual;
} UI_StatusSnapshot;
/** @brief 由调用方拥有的NUL结尾ASCII文本；后续中文字体由TouchGFX视图负责。 */
typedef struct {
 char role[16],network[16],sync[24],offset[32],temperature[24],speed[24];
 char audio[16],counts[32],distance[24],precision[24];
 char temperature_source[12],direction[16],quality[16];
 char audio_detail[32];
} UI_StatusText;
/**
 * @brief 格式化快照，年龄>=2000ms标记陈旧；无任何外部精度通过标志。
 * @param snapshot 非NULL只读快照，数值仅在对应valid为true时展示。
 * @param now_ms 当前HAL毫秒，无符号差支持回绕。
 * @param out 非NULL输出，由调用方持有；参数错误时非NULL输出清零。
 * @return true为已格式化，false为参数错误；不读硬件或保留指针。
 */
bool UI_StatusFormat(const UI_StatusSnapshot *snapshot,uint32_t now_ms,UI_StatusText *out);
#ifdef __cplusplus
}
#endif
#endif
