/**
 * @file        sdram_test.c
 * @brief       不阻塞前台的little-endian SDRAM破坏性读写检查。
 * @details     通过volatile访问真实内存；4轮全范围模式在填充后跨过100ms刷新窗口再校验。
 * @note        使用时目标区域必须独占；最多128字一片，失败或完成立即停止访问。
 */
#include "sdram_test.h"
#include <stddef.h>
#include <string.h>

/** @brief 生成常量/互补和逐地址唯一/互补模式。@param pass 0..3轮号。@param index 32位字索引。@return 期望数据。 */
static uint32_t Pattern(uint32_t pass, uint32_t index)
{
    if (pass == 0U) { return UINT32_C(0x5555AAAA); }
    if (pass == 1U) { return UINT32_C(0xAAAA5555); }
    const uint32_t value = index ^ UINT32_C(0xA5A55A5A);
    return pass == 2U ? value : ~value;
}

/** @brief 检查读回并保存首次错误。@param t 非NULL状态。@param offset 字节偏移。@param want 期望。@param got 实际。@return 相等为true。 */
static bool Check(SDRAM_Test *t, uint32_t offset, uint32_t want, uint32_t got)
{
    if (want == got) { return true; }
    t->phase = SDRAM_TEST_FAIL;
    t->error_offset_bytes = offset; t->expected = want; t->actual = got;
    return false;
}

/**
 * @brief 检查32位组合数据线、16位访问及四个字节写入掩码。
 * @param t 非NULL有效测试状态；仅前8字节会被改写。
 * @return 读回全部正确返回true；失败不再执行后续访问。
 * @note little-endian接口，volatile uint8_t别名合法用于逐字节访问。
 */
static bool DataBus(SDRAM_Test *t)
{
    for (uint32_t bit = 0U; bit < 32U; ++bit) {
        const uint32_t pattern = UINT32_C(1) << bit;
        t->memory[0] = pattern;
        if (!Check(t, 0U, pattern, t->memory[0])) { return false; }
        t->memory[0] = ~pattern;
        if (!Check(t, 0U, ~pattern, t->memory[0])) { return false; }
    }
    volatile uint16_t *half = (volatile uint16_t *)t->memory;
    half[0] = UINT16_C(0x1357); half[1] = UINT16_C(0x2468);
    if (!Check(t, 0U, UINT32_C(0x24681357), t->memory[0])) { return false; }
    volatile uint8_t *byte = (volatile uint8_t *)t->memory;
    for (uint32_t lane = 0U; lane < 4U; ++lane) {
        t->memory[0] = UINT32_C(0xA5A5A5A5);
        byte[lane] = UINT8_C(0x3C);
        const uint32_t expected = (UINT32_C(0xA5A5A5A5) & ~(UINT32_C(0xFF) << (lane * 8U))) |
                                  (UINT32_C(0x3C) << (lane * 8U));
        if (!Check(t, 0U, expected, t->memory[0])) { return false; }
    }
    return true;
}

/** @brief 建立不访问目标RAM的测试状态。@param t 输出/持有状态，可为NULL。@param memory 独占对齐内存。@param bytes 区域字节数。@return 参数有效返回true。 */
bool SDRAM_TestInit(SDRAM_Test *t, volatile void *memory, uint32_t bytes)
{
    if (t == NULL) { return false; }
    memset(t, 0, sizeof *t);
    if (memory == NULL || ((uintptr_t)memory & 3U) != 0U || bytes < 8U ||
        bytes > UINT32_C(0x800000) || (bytes & 3U) != 0U) { return false; }
    t->memory = memory; t->words = bytes / 4U; t->phase = SDRAM_TEST_DATA;
    return true;
}

/** @brief 推进一个有界分片或保持等待。@param t 状态，NULL无操作。@param now_ms 回绕安全毫秒值。@return 无返回值；失败/通过状态不访问内存。 */
void SDRAM_TestStep(SDRAM_Test *t, uint32_t now_ms)
{
    if (t == NULL) { return; }
    if (t->phase == SDRAM_TEST_DATA) {
        if (DataBus(t)) { t->phase = SDRAM_TEST_FILL; }
        return;
    }
    if (t->phase == SDRAM_TEST_HOLD) {
        if ((uint32_t)(now_ms - t->hold_ms) >= SDRAM_TEST_HOLD_MS) {
            t->phase = SDRAM_TEST_VERIFY; t->cursor = 0U;
        }
        return;
    }
    if (t->phase != SDRAM_TEST_FILL && t->phase != SDRAM_TEST_VERIFY) { return; }
    const uint32_t count = t->words - t->cursor < SDRAM_TEST_SLICE_WORDS ?
                          t->words - t->cursor : SDRAM_TEST_SLICE_WORDS;
    for (uint32_t i = 0U; i < count; ++i) {
        const uint32_t index = t->cursor;
        const uint32_t expected = Pattern(t->pass, index);
        if (t->phase == SDRAM_TEST_FILL) { t->memory[index] = expected; }
        else {
            if (!Check(t, index * 4U, expected, t->memory[index])) { return; }
            ++t->verified_words;
        }
        ++t->cursor;
    }
    if (t->cursor != t->words) { return; }
    if (t->phase == SDRAM_TEST_FILL) { t->phase = SDRAM_TEST_HOLD; t->hold_ms = now_ms; }
    else if (++t->pass == SDRAM_TEST_PASSES) { t->phase = SDRAM_TEST_PASS; }
    else { t->phase = SDRAM_TEST_FILL; t->cursor = 0U; }
}
