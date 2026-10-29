/**
 * @file        chirp_template.h
 * @brief       声明由冻结WAV逐点导出的50ms扫频模板和源文件SHA256。
 * @details     2400点PCM16、名义48kHz、2至6kHz线性扫频；不含前后静音。
 * @note        常量只读存储，不计算采样时钟或真实到达时间；数据由导出脚本维护。
 */
#ifndef SBCG_CHIRP_TEMPLATE_H
#define SBCG_CHIRP_TEMPLATE_H
#include <stdint.h>
/** @brief 模板单声道点数，名义48kHz时为50ms。 */
#define CHIRP_TEMPLATE_FRAMES 2400U
/** @brief 源WAV原样PCM16数据，生命周期为整个程序运行期。 */
extern const int16_t ChirpTemplate_PCM[CHIRP_TEMPLATE_FRAMES];
/** @brief 包含WAV容器的完整源文件SHA256，64个十六进制字符加NUL。 */
extern const char ChirpTemplate_SourceSha256[65];
#endif
