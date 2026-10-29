/**
 * @file        sound_event_timing.h
 * @brief       定义窗口TOA发布与板间配对共用的前台等待预算。
 * @details     候选等待右锚至多2500ms；配对额外保留500ms传输与前台调度余量。
 * @note        仅约束软件保留时间，不改变40ms声学TDOA窗口或保证任意网络延迟。
 */
#ifndef SBCG_SOUND_EVENT_TIMING_H
#define SBCG_SOUND_EVENT_TIMING_H

#define SOUND_EVENT_MODEL_WAIT_MS 2500U
#define SOUND_EVENT_PUBLISH_MARGIN_MS 500U
#define SOUND_EVENT_PAIR_TIMEOUT_MS \
    (SOUND_EVENT_MODEL_WAIT_MS + SOUND_EVENT_PUBLISH_MARGIN_MS)

#endif
