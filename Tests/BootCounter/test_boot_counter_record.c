/**
 * @file        test_boot_counter_record.c
 * @brief       在主机上验证启动代次记录格式、扇区扫描和双扇区轮转计划。
 * @details     使用两片合成的128KiB扇区缓冲注入有效记录、残写、空洞、满扇区和掉电中间态，
 *              断言只有完整提交记录参与最大值、空槽选择与擦除安全约束。
 * @note        只验证纯数据逻辑；真实Flash擦写、重复烧录和整板断电由板上验收覆盖。
 */
#include "BSP/boot_counter_record.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t sectors[2][BOOT_COUNTER_SECTOR_BYTES];

/** @brief 把两个扇区都置为全擦除态。 @return 无返回值。 */
static void EraseBoth(void)
{
    memset(sectors, 0xFF, sizeof sectors);
}

/** @brief 在指定槽写入一条有效记录。 @param sector 扇区下标。 @param slot 槽下标。 @param value 代次。 @return 无返回值。 */
static void WriteRecord(unsigned sector, unsigned slot, uint64_t value)
{
    BootCounterRecord_Encode(value, &sectors[sector][slot * BOOT_COUNTER_RECORD_BYTES]);
}

/** @brief 扫描指定扇区。 @param sector 扇区下标。 @return 扫描结果。 */
static BootCounterSector ScanOne(unsigned sector)
{
    BootCounterSector scan;
    assert(BootCounterSector_Scan(sectors[sector], BOOT_COUNTER_SECTOR_BYTES, &scan));
    return scan;
}

/** @brief 用两个合成扇区生成计划。 @param plan 输出。 @return Plan结果。 */
static bool MakePlan(BootCounterPlan *plan)
{
    const BootCounterSector scans[2] = {ScanOne(0U), ScanOne(1U)};
    return BootCounter_Plan(scans, plan);
}

/** @brief 验证编码/解码/擦除识别。 @return 无返回值。 */
static void TestRecord(void)
{
    uint8_t record[BOOT_COUNTER_RECORD_BYTES];
    BootCounterRecord_Encode(UINT64_C(0x0123456789ABCDEF), record);
    uint64_t value = 0U;
    assert(BootCounterRecord_Decode(record, &value) && value == UINT64_C(0x0123456789ABCDEF));
    assert(!BootCounterRecord_IsErased(record));

    uint8_t erased[BOOT_COUNTER_RECORD_BYTES];
    memset(erased, 0xFF, sizeof erased);
    assert(BootCounterRecord_IsErased(erased));
    assert(!BootCounterRecord_Decode(erased, &value));

    for (unsigned offset = 0U; offset < BOOT_COUNTER_RECORD_BYTES; ++offset)
    {
        uint8_t corrupt[BOOT_COUNTER_RECORD_BYTES];
        memcpy(corrupt, record, sizeof corrupt);
        corrupt[offset] ^= 0x01U;
        assert(!BootCounterRecord_Decode(corrupt, NULL));
    }
    BootCounterRecord_Encode(0U, record);
    assert(BootCounterRecord_IsErased(record));
    puts("PASS record: round-trip, zero rejected, erased recognized, every byte corruption rejected");
}

/** @brief 验证扇区扫描的最大值、空槽和残写识别。 @return 无返回值。 */
static void TestScan(void)
{
    EraseBoth();
    BootCounterSector scan = ScanOne(0U);
    assert(!scan.has_valid && scan.highest == 0U && scan.first_free == 0U && !scan.has_partial);

    WriteRecord(0U, 0U, 7U);
    WriteRecord(0U, 1U, 9U);
    WriteRecord(0U, 2U, 8U);
    scan = ScanOne(0U);
    assert(scan.has_valid && scan.highest == 9U && scan.first_free == 3U && !scan.has_partial);

    sectors[0][BOOT_COUNTER_RECORD_BYTES] ^= 0x80U;
    scan = ScanOne(0U);
    assert(scan.has_valid && scan.highest == 8U && scan.first_free == 3U && scan.has_partial);
    puts("PASS scan: max, first free slot and partial-record detection");
}

/** @brief 验证无历史记录的首次写入计划。 @return 无返回值。 */
static void TestPlanFirstUse(void)
{
    BootCounterPlan plan;
    EraseBoth();
    assert(MakePlan(&plan));
    assert(plan.action == BOOT_COUNTER_ACTION_APPEND && plan.sector == 0U &&
           plan.slot == 0U && plan.next_value == 1U);

    EraseBoth();
    /* 扇区0前5槽为非擦除非有效垃圾：无历史时应优先使用全擦除的扇区1槽0。 */
    for (unsigned slot = 0U; slot < 5U; ++slot)
    {
        memset(&sectors[0][slot * BOOT_COUNTER_RECORD_BYTES], 0x00,
               BOOT_COUNTER_RECORD_BYTES);
    }
    assert(MakePlan(&plan));
    assert(plan.action == BOOT_COUNTER_ACTION_APPEND && plan.sector == 1U &&
           plan.slot == 0U && plan.next_value == 1U);

    EraseBoth();
    memset(sectors[0], 0x00, 16U);
    memset(sectors[1], 0x00, 16U);
    assert(MakePlan(&plan));
    assert(plan.action == BOOT_COUNTER_ACTION_ERASE_AND_APPEND && plan.sector == 0U &&
           plan.slot == 0U && plan.next_value == 1U);
    puts("PASS plan: first use appends or erases only non-history sectors");
}

/** @brief 验证有历史时的追加选择与掉电残写跳过。 @return 无返回值。 */
static void TestPlanAppend(void)
{
    BootCounterPlan plan;
    EraseBoth();
    for (unsigned slot = 0U; slot < 3U; ++slot)
    {
        WriteRecord(0U, slot, (uint64_t)(slot + 1U));
    }
    WriteRecord(1U, 0U, 2U);
    assert(MakePlan(&plan));
    assert(plan.action == BOOT_COUNTER_ACTION_APPEND && plan.sector == 0U &&
           plan.slot == 3U && plan.next_value == 4U);

    /* 掉电只写了记录前半：残写不参与最大值，也不阻止下一槽追加。 */
    BootCounterRecord_Encode(5U, &sectors[0][3U * BOOT_COUNTER_RECORD_BYTES]);
    memset(&sectors[0][3U * BOOT_COUNTER_RECORD_BYTES + 8U], 0xFF,
           BOOT_COUNTER_RECORD_BYTES - 8U);
    assert(MakePlan(&plan));
    assert(plan.action == BOOT_COUNTER_ACTION_APPEND && plan.sector == 0U &&
           plan.slot == 4U && plan.next_value == 4U);
    puts("PASS plan: append after max and skip partial power-loss record");
}

/** @brief 验证满扇区轮转与擦除安全约束。 @return 无返回值。 */
static void TestPlanRotation(void)
{
    BootCounterPlan plan;
    EraseBoth();
    for (unsigned slot = 0U; slot < BOOT_COUNTER_SLOTS; ++slot)
    {
        WriteRecord(0U, slot, (uint64_t)(slot + 1U));
    }
    WriteRecord(1U, 0U, 100U);
    assert(MakePlan(&plan));
    assert(plan.action == BOOT_COUNTER_ACTION_ERASE_AND_APPEND && plan.sector == 1U &&
           plan.slot == 0U && plan.next_value == (uint64_t)BOOT_COUNTER_SLOTS + 1U);

    /* 两扇区都满且最大值相同：擦除保存同值的另一扇区仍保留全局最大值。 */
    for (unsigned slot = 0U; slot < BOOT_COUNTER_SLOTS; ++slot)
    {
        WriteRecord(1U, slot, (uint64_t)(slot + 1U));
    }
    assert(MakePlan(&plan));
    assert(plan.action == BOOT_COUNTER_ACTION_ERASE_AND_APPEND && plan.sector == 1U &&
           plan.next_value == (uint64_t)BOOT_COUNTER_SLOTS + 1U);

    /* 最大代次在扇区1且扇区1有空间：不得选择擦除扇区1。 */
    EraseBoth();
    WriteRecord(0U, 0U, 3U);
    WriteRecord(1U, 0U, 4U);
    assert(MakePlan(&plan));
    assert(plan.action == BOOT_COUNTER_ACTION_APPEND && plan.sector == 1U &&
           plan.slot == 1U && plan.next_value == 5U);
    puts("PASS plan: rotation never erases the sector holding the global maximum");
}

/** @brief 验证代次耗尽与擦除后重写。 @return 无返回值。 */
static void TestPlanExhausted(void)
{
    BootCounterPlan plan;
    EraseBoth();
    WriteRecord(0U, 0U, UINT64_MAX);
    assert(!MakePlan(&plan));
    assert(plan.action == BOOT_COUNTER_ACTION_FAIL);
    puts("PASS plan: UINT64_MAX generation refused instead of wrapping");
}

/** @brief 运行全部记录与轮转用例。 @return 全部断言通过时返回0。 */
int main(void)
{
    TestRecord();
    TestScan();
    TestPlanFirstUse();
    TestPlanAppend();
    TestPlanRotation();
    TestPlanExhausted();
    puts("BootCounterRecord: 6 scenarios passed");
    return 0;
}
