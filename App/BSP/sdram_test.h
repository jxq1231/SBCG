/**
 * @file        sdram_test.h
 * @brief       外部内存的分片破坏性自检接口，不依赖HAL。
 * @details     调用方独占缓冲区；先检查数据位/字节写，再以四种模式覆盖全区域。
 * @note        仅前台串行使用；测试期间该内存不能用于帧缓冲或其它数据。
 */
#ifndef SBCG_SDRAM_TEST_H
#define SBCG_SDRAM_TEST_H
#include <stdbool.h>
#include <stdint.h>

#define SDRAM_TEST_SLICE_WORDS 128U
#define SDRAM_TEST_HOLD_MS 100U
#define SDRAM_TEST_PASSES 4U
typedef enum {
    SDRAM_TEST_INVALID, SDRAM_TEST_DATA, SDRAM_TEST_FILL, SDRAM_TEST_HOLD,
    SDRAM_TEST_VERIFY, SDRAM_TEST_PASS, SDRAM_TEST_FAIL
} SDRAM_TestPhase;

/** @brief 测试状态由调用方持有；字为32位，错误偏移为字节，hold_ms为回绕安全HAL毫秒。 */
typedef struct {
    volatile uint32_t *memory;
    uint32_t words, cursor, pass, hold_ms, verified_words;
    uint32_t error_offset_bytes, expected, actual;
    SDRAM_TestPhase phase;
} SDRAM_Test;

/**
 * @brief 初始化状态但不读写目标内存。
 * @param test 调用方状态，NULL返回false。
 * @param memory 调用方独占、4字节对齐、可读写的little-endian内存。
 * @param bytes 区域字节数；8～8388608且为4的倍数，整个测试期间保持有效。
 * @return 参数有效返回true；否则状态为INVALID（非NULL时）。
 */
bool SDRAM_TestInit(SDRAM_Test *test, volatile void *memory, uint32_t bytes);
/**
 * @brief 推进数据线测试或最多128字的填充/校验；保持阶段等待至少100ms。
 * @param test 非NULL有效状态；NULL/终态无操作，首次错误终止后续访问。
 * @param now_ms 调用方单调毫秒计数，允许32位回绕。
 * @return 无返回值。仅前台调用，任何测试阶段均不延时或分配内存。
 */
void SDRAM_TestStep(SDRAM_Test *test, uint32_t now_ms);
#endif
