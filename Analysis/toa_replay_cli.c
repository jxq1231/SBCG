/**
 * @file        toa_replay_cli.c
 * @brief       主机进程适配器：只读256点PCM并调用冻结的TOA估算器。
 * @details     按命令行给出的窗口文件逐个读取恰好256个小端有符号16位样本，
 *              以默认配置调用 ToaEstimator_EstimateBackward，输出 VALID 与
 *              相对窗首的子帧位置；窗口长度不符、文件不可读或估算失败均输出
 *              无歧义的原因行。本文件不链接进固件，仅用于离线回放复算。
 * @note        与 snapshot 目录中的 toa_estimator.c/.h 一起编译；O0/O2 与重复
 *              运行由调用方编排，用于核对算法输出的可复现性。
 */
#include "toa_estimator.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define TOA_WINDOW_BYTES 512U  /* 256帧 x int16 */
#define TOA_WINDOW_FRAMES 256U

/**
 * @brief   打印单窗口回放结果行。
 * @param   label 窗口文件路径，用作行尾标识（可能含空格）。
 * @param   valid 估算是否成功。
 * @param   fraction 成功时的子帧位置，失败时为0。
 * @param   reason 失败原因或空串。
 * @return  无返回值。
 * @note    行格式固定为 VALID=<0|1> FRACTION=<.6f> REASON=<...> FILE=<label>，
 *          FILE 作为行末唯一 token，避免路径含空格时被拆断；调用方按输入顺序对齐。
 */
static void Emit(const char *label, int valid, double fraction, const char *reason)
{
    if (valid) {
        printf("VALID=1 FRACTION=%.6f REASON=ok FILE=%s\n", fraction, label);
    } else {
        printf("VALID=0 FRACTION=0.000000 REASON=%s FILE=%s\n",
               reason != 0 ? reason : "unknown", label);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: toa_replay_cli <window.pcm> [window.pcm ...]\n");
        return 2;
    }
    for (int index = 1; index < argc; ++index) {
        const char *path = argv[index];
        if (path == 0 || path[0] == '\0') {
            Emit("?", 0, 0.0, "bad_arg");
            return 2;
        }
        FILE *fp = fopen(path, "rb");
        if (fp == NULL) {
            Emit(path, 0, 0.0, "open_failed");
            continue;
        }
        unsigned char bytes[TOA_WINDOW_BYTES + 1U];
        size_t got = fread(bytes, 1U, TOA_WINDOW_BYTES + 1U, fp);
        fclose(fp);
        if (got != TOA_WINDOW_BYTES) {
            Emit(path, 0, 0.0, "window_size");
            continue;
        }
        int16_t samples[TOA_WINDOW_FRAMES];
        for (uint32_t i = 0U; i < TOA_WINDOW_FRAMES; ++i) {
            const int hi = (int)bytes[2U * i + 1U] << 8;
            samples[i] = (int16_t)((int)(int16_t)bytes[2U * i] | hi);
        }
        double fraction = 0.0;
        const int ok = ToaEstimator_EstimateBackward(
            &ToaBackward_DefaultConfig, samples, &fraction);
        if (!ok) {
            Emit(path, 0, 0.0, "estimate_failed");
            continue;
        }
        if (isnan(fraction) || fraction < 0.0 || fraction >= (double)TOA_WINDOW_FRAMES) {
            Emit(path, 0, 0.0, "nonfinite");
            continue;
        }
        Emit(path, 1, fraction, "ok");
    }
    return 0;
}