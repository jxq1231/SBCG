/**
 * @file        test_calibration.c
 * @brief       验证500mm单点标定的保存、应用开关、清除与版本行为。
 * @details     使用真实纯C模块检查状态转换、无效输入和非零版本回绕。
 * @note        由现有主机测试脚本分别以-O0和-O2编译运行，不依赖HAL。
 */
#include "Calibration/calibration.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#undef assert
/** @brief 检查表达式，失败时打印位置并结束主机测试进程。 */
#define assert(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)

/**
 * @brief 检查模块当前快照的全部字段，避免依赖结构体填充字节。
 * @param calibrated 预期是否已保存有效标定。
 * @param offset_mm 预期已保存的偏移，单位mm。
 * @param version 预期标定状态版本。
 * @param enabled 预期是否应用已保存的偏移。
 * @return 无返回值；断言失败时终止测试。
 */
static void assert_snapshot(bool calibrated, int32_t offset_mm,
                            uint16_t version, bool enabled)
{
    CalibrationSnapshot snapshot;
    Calibration_Get(&snapshot);
    assert(snapshot.calibrated == calibrated);
    assert(snapshot.offset_mm == offset_mm);
    assert(snapshot.reference_mm == CALIBRATION_REFERENCE_MM);
    assert(snapshot.version == version);
    assert(snapshot.enabled == enabled);
}

/** @brief 验证未标定状态拒绝开关，空清除不递增版本。 @return 无返回值。 */
static void test_initial_state(void)
{
    Calibration_Init();
    assert_snapshot(false, 0, 0U, false);
    assert(Calibration_Apply(480) == 480);
    assert(!Calibration_SetEnabled(true));
    assert(!Calibration_SetEnabled(false));
    Calibration_Reset();
    Calibration_Get(NULL);
    assert_snapshot(false, 0, 0U, false);
}

/** @brief 验证零偏移也是有效标定，应用开关仅在状态变化时递增版本。 @return 无返回值。 */
static void test_zero_offset_enable_state(void)
{
    Calibration_Init();
    assert(Calibration_SetFromMeasurement(500));
    assert_snapshot(true, 0, 1U, true);
    assert(Calibration_SetEnabled(true));
    assert_snapshot(true, 0, 1U, true);
    assert(Calibration_SetEnabled(false));
    assert_snapshot(true, 0, 2U, false);
    assert(Calibration_Apply(650) == 650);
    assert(Calibration_SetEnabled(false));
    assert_snapshot(true, 0, 2U, false);
    assert(Calibration_SetEnabled(true));
    assert_snapshot(true, 0, 3U, true);
}

/** @brief 验证关闭应用保留非零系数，重新启用后恢复同一偏移。 @return 无返回值。 */
static void test_saved_offset_and_application(void)
{
    Calibration_Init();
    assert(Calibration_SetFromMeasurement(485));
    assert_snapshot(true, 15, 1U, true);
    assert(Calibration_Apply(485) == 500);
    assert(Calibration_Apply(600) == 615);
    assert(Calibration_SetEnabled(false));
    assert_snapshot(true, 15, 2U, false);
    assert(Calibration_Apply(485) == 485);
    assert(Calibration_SetEnabled(true));
    assert_snapshot(true, 15, 3U, true);
    assert(Calibration_Apply(485) == 500);
}

/** @brief 验证再次标定替换旧系数并启用应用，不叠加旧偏移。 @return 无返回值。 */
static void test_recalibration_replaces_offset(void)
{
    Calibration_Init();
    assert(Calibration_SetFromMeasurement(485));
    assert(Calibration_SetEnabled(false));
    assert(Calibration_SetFromMeasurement(470));
    assert_snapshot(true, 30, 3U, true);
    assert(Calibration_Apply(470) == 500);
    assert(Calibration_SetFromMeasurement(470));
    assert_snapshot(true, 30, 4U, true);
}

/** @brief 验证非法原始距离及越界偏移不会破坏启用或停用中的保存状态。 @return 无返回值。 */
static void test_invalid_measurements_preserve_state(void)
{
    const int32_t invalid_raw_mm[] = {-1, 10001, 10000, 1501, 1600};
    Calibration_Init();
    assert(Calibration_SetFromMeasurement(470));
    for (size_t index = 0U; index < sizeof invalid_raw_mm / sizeof invalid_raw_mm[0]; ++index)
    {
        assert(!Calibration_SetFromMeasurement(invalid_raw_mm[index]));
        assert_snapshot(true, 30, 1U, true);
    }
    assert(Calibration_SetEnabled(false));
    for (size_t index = 0U; index < sizeof invalid_raw_mm / sizeof invalid_raw_mm[0]; ++index)
    {
        assert(!Calibration_SetFromMeasurement(invalid_raw_mm[index]));
        assert_snapshot(true, 30, 2U, false);
    }
}

/** @brief 验证既有原始距离与最大偏移边界保持可用。 @return 无返回值。 */
static void test_existing_bounds(void)
{
    Calibration_Init();
    assert(Calibration_SetFromMeasurement(0));
    assert_snapshot(true, 500, 1U, true);
    assert(Calibration_SetFromMeasurement(1500));
    assert_snapshot(true, -1000, 2U, true);
    assert(Calibration_Apply(1500) == 500);
}

/** @brief 验证清除启用或停用的标定都推进版本，重复清除保持版本。 @return 无返回值。 */
static void test_clear_and_idempotency(void)
{
    Calibration_Init();
    assert(Calibration_SetFromMeasurement(485));
    assert(Calibration_SetFromMeasurement(470));
    Calibration_Reset();
    assert_snapshot(false, 0, 3U, false);
    assert(Calibration_Apply(470) == 470);
    assert(!Calibration_SetEnabled(true));
    Calibration_Reset();
    assert_snapshot(false, 0, 3U, false);
    assert(Calibration_SetFromMeasurement(500));
    assert(Calibration_SetEnabled(false));
    Calibration_Reset();
    assert_snapshot(false, 0, 6U, false);
    Calibration_Reset();
    assert_snapshot(false, 0, 6U, false);
    Calibration_Init();
    assert_snapshot(false, 0, 0U, false);
}

/** @brief 验证16位版本回绕跳过零，标定、开关和清除使用同一规则。 @return 无返回值。 */
static void test_nonzero_version_wrap(void)
{
    Calibration_Init();
    for (uint32_t index = 0U; index < UINT16_MAX; ++index)
    {
        assert(Calibration_SetFromMeasurement(500));
    }
    assert_snapshot(true, 0, UINT16_MAX, true);
    assert(Calibration_SetEnabled(false));
    assert_snapshot(true, 0, 1U, false);
    for (uint32_t index = 0U; index < UINT16_MAX - 1U; ++index)
    {
        assert(Calibration_SetFromMeasurement(500));
    }
    Calibration_Reset();
    assert_snapshot(false, 0, 1U, false);
    for (uint32_t index = 0U; index < UINT16_MAX; ++index)
    {
        assert(Calibration_SetFromMeasurement(500));
    }
    assert_snapshot(true, 0, 1U, true);
}

/** @brief 顺序执行全部纯C标定状态测试。 @return 全部通过时返回0。 */
int main(void)
{
    test_initial_state();
    test_zero_offset_enable_state();
    test_saved_offset_and_application();
    test_recalibration_replaces_offset();
    test_invalid_measurements_preserve_state();
    test_existing_bounds();
    test_clear_and_idempotency();
    test_nonzero_version_wrap();
    puts("PASS calibration: 500mm saved offset/enable/disable/clear/version/bounds/wrap");
    return 0;
}
