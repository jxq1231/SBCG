/**
 * @file        udp_chirp_capture.h
 * @brief       声明候选附近 End 验证 PCM 的 UDP 二进制分片下传接口。
 * @details     将单个候选的有界 PCM 快照按 56 字节大端头 + int16 样本分片为多包发送到 PC；
 *              PC 侧按 (session,capture_id) 重组。发送成功仅表示协议栈接受，不证明对端收到。
 * @note        前台串行调用；懒创建并绑定 5008 板端端口，调用后于 MX_LWIP_Init；
 *              不修改生成网卡或 ETH 配置，不保存 slice 指针。
 */
#ifndef SBCG_UDP_CHIRP_CAPTURE_H
#define SBCG_UDP_CHIRP_CAPTURE_H
#include <stdbool.h>
#include <stdint.h>
#include "Detector/chirp_service.h"

/** @brief 单个 UDP 数据报内最多携带的 int16 样本数；56+512*2=1080 字节，低于链路口径，无需 IP 分片。 */
#define UDP_CHIRP_CAPTURE_CHUNK_FRAMES 512U
/**
 * @brief 分片包头字节数，网络大端序：
 *        magic4 | ver1 | kind1 | flags1 | rsv1 | seq4 | total4 | present4 | pad4 |
 *        id4 | session4 | first_frame8 | hint_frame8 | raw_first_frame8，后随 int16 载荷。
 */
#define UDP_CHIRP_CAPTURE_HEADER_BYTES 56U
/** @brief 封包头标志位：第0位左被裁剪、第1位右被裁剪、第2位完成时补过右尾部。 */
#define UDP_CHIRP_CAPTURE_FLAG_LEFT_CLAMP 0x01U
#define UDP_CHIRP_CAPTURE_FLAG_RIGHT_CLAMP 0x02U
#define UDP_CHIRP_CAPTURE_FLAG_TAIL_TOPUP 0x04U

/** @brief 复制诊断计数；容量为下发尝试次数，非网络保序信息。 */
typedef struct {
    uint32_t captures_ok, captures_skipped;
    uint32_t chunks_total, chunks_sent, chunk_errors;
    uint32_t packets_ok, packets_error, pcb_errors, link_skips;
} UDPChirpCaptureSnapshot;

/**
 * @brief   复位计数并删除旧 UDP 控制块；不创建或立即发送。
 * @param   now_ms 当前 HAL 毫秒，仅保留接口一致性，本模块不使用。
 * @param   log 可为 NULL 的日志回调，返回前须消费文本；NULL 时跳过发送。
 * @return  无返回值。
 * @note    MX_LWIP_Init 之后前台调用一次；失败路径在下一次 Send 中懒创建。
 */
void UDPChirpCapture_Init(uint32_t now_ms, void (*log)(const char *, int));
/**
 * @brief   将一个有界 PCM 切片分片发送到 PC 6008 端口。
 * @param   slice 非 NULL 只读切片，仅本次调用借用；samples 至少 frames 个 int16。
 * @return  true 表示全部分片都提交给 LwIP；false 为链路未就绪/参数无效/中途失败。
 * @note    前台串行；任一数据报失败即停止并计错误，不保留部分状态；
 *          log 非 NULL 时前缀输出一行 $CHIRP_CAPTURE 元数据。
 */
bool UDPChirpCapture_Send(const ChirpCaptureSlice *slice);
/**
 * @brief   复制发送诊断计数。
 * @param   out 非 NULL 独占输出。
 * @return  无返回值。
 */
void UDPChirpCapture_GetSnapshot(UDPChirpCaptureSnapshot *out);
#endif