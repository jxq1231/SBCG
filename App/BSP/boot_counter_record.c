/**
 * @file        boot_counter_record.c
 * @brief       实现启动代次Flash记录的编码、CRC校验、扇区扫描与双扇区轮转计划。
 * @details     记录布局：0..3 magic、4..5 版本、6..7 头标记、8..15 代次、
 *              16..19 CRC32、20..23 提交标记、24..31 代次补码。所有多字节字段小端。
 *              只有整条记录同时满足提交标记、CRC和补码才视为已登记代次；
 *              残写记录按扇区空洞处理，后续扫描继续寻找有效历史。
 * @note        纯函数，无全局状态；调用者负责保证扇区地址可读且长度对齐。
 */
#include "boot_counter_record.h"
#include <string.h>

#define BOOT_COUNTER_MAGIC UINT32_C(0x544F4F42)     /* ASCII "BOOT" 小端。 */
#define BOOT_COUNTER_VERSION 1U
#define BOOT_COUNTER_HEADER_MARK UINT16_C(0x5AA5)
#define BOOT_COUNTER_COMMIT_MARK UINT32_C(0x5A3CC3A5)
#define CRC32_INITIAL UINT32_C(0xFFFFFFFF)
#define CRC32_POLYNOMIAL UINT32_C(0xEDB88320)

/** @brief 按小端写入32位整数。 @param out 非NULL可写字段。 @param value 待写值。 @return 无返回值。 */
static void Put32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8U);
    out[2] = (uint8_t)(value >> 16U);
    out[3] = (uint8_t)(value >> 24U);
}

/** @brief 按小端读取32位整数。 @param in 非NULL只读字段。 @return 无符号整数值。 */
static uint32_t Get32(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8U) |
           ((uint32_t)in[2] << 16U) | ((uint32_t)in[3] << 24U);
}

/** @brief 按小端写入16位整数。 @param out 非NULL可写字段。 @param value 待写值。 @return 无返回值。 */
static void Put16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8U);
}

/** @brief 按小端读取16位整数。 @param in 非NULL只读字段。 @return 无符号整数值。 */
static uint16_t Get16(const uint8_t *in)
{
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8U));
}

/** @brief 按小端写入64位整数。 @param out 非NULL可写字段。 @param value 待写值。 @return 无返回值。 */
static void Put64(uint8_t *out, uint64_t value)
{
    for (unsigned i = 0U; i < 8U; ++i)
    {
        out[i] = (uint8_t)(value >> (8U * i));
    }
}

/** @brief 按小端读取64位整数。 @param in 非NULL只读字段。 @return 无符号整数值。 */
static uint64_t Get64(const uint8_t *in)
{
    uint64_t value = 0U;
    for (unsigned i = 0U; i < 8U; ++i)
    {
        value |= (uint64_t)in[i] << (8U * i);
    }
    return value;
}

/** @brief 计算CRC32（多项式0xEDB88320，反射算法）。 @param data 非NULL只读字节。 @param length 字节数。 @return CRC32值。 */
static uint32_t Crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = CRC32_INITIAL;
    for (uint32_t i = 0U; i < length; ++i)
    {
        crc ^= data[i];
        for (unsigned bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc >> 1U) ^ (CRC32_POLYNOMIAL & (0U - (crc & 1U)));
        }
    }
    return crc ^ CRC32_INITIAL;
}

/**
 * @brief   编码一条启动代次记录。
 * @param   value 待记录的非零代次。
 * @param   out 非NULL的32字节输出。
 * @return  无返回值；value为0时写全0xFF，随后解码会拒绝。
 */
void BootCounterRecord_Encode(uint64_t value, uint8_t out[BOOT_COUNTER_RECORD_BYTES])
{
    if (out == NULL)
    {
        return;
    }
    if (value == 0U)
    {
        memset(out, 0xFF, BOOT_COUNTER_RECORD_BYTES);
        return;
    }
    Put32(&out[0], BOOT_COUNTER_MAGIC);
    Put16(&out[4], (uint16_t)BOOT_COUNTER_VERSION);
    Put16(&out[6], BOOT_COUNTER_HEADER_MARK);
    Put64(&out[8], value);
    Put32(&out[16], Crc32(out, 16U));
    Put32(&out[20], BOOT_COUNTER_COMMIT_MARK);
    Put64(&out[24], ~value);
}

/**
 * @brief   校验并解码一条记录。
 * @param   in 非NULL只读32字节。
 * @param   value 可为NULL的输出；成功时写入。
 * @return  全部校验通过且代次非零为true。
 */
bool BootCounterRecord_Decode(const uint8_t in[BOOT_COUNTER_RECORD_BYTES], uint64_t *value)
{
    if (in == NULL)
    {
        return false;
    }
    if (Get32(&in[0]) != BOOT_COUNTER_MAGIC ||
        Get16(&in[4]) != (uint16_t)BOOT_COUNTER_VERSION ||
        Get16(&in[6]) != BOOT_COUNTER_HEADER_MARK ||
        Get32(&in[20]) != BOOT_COUNTER_COMMIT_MARK)
    {
        return false;
    }
    const uint64_t decoded = Get64(&in[8]);
    if (decoded == 0U || Get64(&in[24]) != ~decoded ||
        Get32(&in[16]) != Crc32(in, 16U))
    {
        return false;
    }
    if (value != NULL)
    {
        *value = decoded;
    }
    return true;
}

/**
 * @brief   判断槽是否为全擦除状态。
 * @param   in 非NULL只读32字节。
 * @return  32字节均为0xFF时为true。
 */
bool BootCounterRecord_IsErased(const uint8_t in[BOOT_COUNTER_RECORD_BYTES])
{
    if (in == NULL)
    {
        return false;
    }
    for (uint32_t i = 0U; i < BOOT_COUNTER_RECORD_BYTES; ++i)
    {
        if (in[i] != 0xFFU)
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief   扫描一个扇区的全部记录槽。
 * @param   sector 非NULL只读扇区首地址。
 * @param   sector_bytes 扇区字节数，须为记录字节数的整数倍且非零。
 * @param   out 非NULL输出；失败时清零。
 * @return  参数合法时为true。
 */
bool BootCounterSector_Scan(const uint8_t *sector, uint32_t sector_bytes, BootCounterSector *out)
{
    if (sector == NULL || out == NULL || sector_bytes == 0U ||
        (sector_bytes % BOOT_COUNTER_RECORD_BYTES) != 0U)
    {
        if (out != NULL)
        {
            *out = (BootCounterSector){0};
        }
        return false;
    }
    BootCounterSector result = {0};
    result.first_free = BOOT_COUNTER_NO_SLOT;
    const uint32_t slots = sector_bytes / BOOT_COUNTER_RECORD_BYTES;
    for (uint32_t slot = 0U; slot < slots; ++slot)
    {
        const uint8_t *record = &sector[slot * BOOT_COUNTER_RECORD_BYTES];
        if (BootCounterRecord_IsErased(record))
        {
            if (result.first_free == BOOT_COUNTER_NO_SLOT)
            {
                result.first_free = slot;
            }
            continue;
        }
        uint64_t value = 0U;
        if (BootCounterRecord_Decode(record, &value))
        {
            result.has_valid = true;
            if (value > result.highest)
            {
                result.highest = value;
            }
        }
        else
        {
            result.has_partial = true;
        }
    }
    *out = result;
    return true;
}

/**
 * @brief   根据两个扇区扫描结果选择追加位置并计算下一代次。
 * @param   scans 非NULL只读两扇区扫描结果。
 * @param   plan 非NULL输出。
 * @return  可写时为true，否则清除plan并返回false。
 */
bool BootCounter_Plan(const BootCounterSector scans[2], BootCounterPlan *plan)
{
    if (scans == NULL || plan == NULL)
    {
        return false;
    }
    *plan = (BootCounterPlan){.action = BOOT_COUNTER_ACTION_FAIL};
    const BootCounterSector *first = &scans[0];
    const BootCounterSector *second = &scans[1];

    if (!first->has_valid && !second->has_valid)
    {
        uint32_t target = 0U;
        if (first->first_free != 0U)
        {
            target = (second->first_free == 0U) ? 1U : 0U;
        }
        plan->sector = target;
        plan->slot = 0U;
        plan->next_value = 1U;
        plan->action = (scans[target].first_free == 0U) ?
            BOOT_COUNTER_ACTION_APPEND : BOOT_COUNTER_ACTION_ERASE_AND_APPEND;
        return true;
    }

    const uint64_t highest = (second->highest > first->highest) ? second->highest : first->highest;
    if (highest == UINT64_MAX)
    {
        return false;
    }
    uint32_t holder = (second->highest > first->highest) ? 1U : 0U;
    if (first->highest == second->highest)
    {
        /* 同值优先选择仍有空槽的扇区，避免无谓擦除；两者都满时固定选0。 */
        if (first->first_free != BOOT_COUNTER_NO_SLOT)
        {
            holder = 0U;
        }
        else if (second->first_free != BOOT_COUNTER_NO_SLOT)
        {
            holder = 1U;
        }
        else
        {
            holder = 0U;
        }
    }

    const BootCounterSector *active = &scans[holder];
    const uint32_t other = 1U - holder;
    if (active->first_free != BOOT_COUNTER_NO_SLOT)
    {
        plan->sector = holder;
        plan->slot = active->first_free;
        plan->next_value = highest + 1U;
        plan->action = BOOT_COUNTER_ACTION_APPEND;
        return true;
    }

    /* 目标扇区不能保存全局最大代次，否则擦除会丢失已发出的值。 */
    if (scans[other].highest > highest)
    {
        return false;
    }
    plan->sector = other;
    plan->slot = 0U;
    plan->next_value = highest + 1U;
    plan->action = BOOT_COUNTER_ACTION_ERASE_AND_APPEND;
    return true;
}
