/**
 * @file        test_touch.c
 * @brief       真实 FT5336 BSP 的身份、坐标、限频及故障释放测试。
 * @details     仅在 HAL 读边界提供寄存器响应，检查发出的实际事务和上层可见结果。
 * @note        主机测试不能证明实际触控方向、总线耗时或共享音频总线的运行表现。
 */
#include "BSP/bsp_touch.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief 断言失败时报告表达式和行号并终止当前用例。 */
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

I2C_HandleTypeDef hi2c3;
static unsigned calls, id_calls, point_calls;
static uint32_t tick_ms, read_duration_ms;
static uint8_t chip_id, response[5];
static HAL_StatusTypeDef id_result, point_result;
static int32_t x, y;

/** @brief 返回边界桩状态；参数必须为生成的共享 I2C3 句柄。 */
HAL_I2C_StateTypeDef HAL_I2C_GetState(const I2C_HandleTypeDef *handle)
{
    CHECK(handle == &hi2c3);
    return handle->state;
}

/** @brief 返回边界桩错误码，供真实 BSP 记录最近失败事务。 */
uint32_t HAL_I2C_GetError(const I2C_HandleTypeDef *handle)
{
    CHECK(handle == &hi2c3);
    return handle->error;
}

/** @brief 返回仿真 HAL tick，允许在真实读路径中覆盖耗时与回绕。 */
uint32_t HAL_GetTick(void)
{
    return tick_ms;
}

/**
 * @brief   校验 HAL 事务后按真实寄存器布局返回响应。
 * @param   handle 必须为共享 I2C3；其余参数按 FT5336 官方地址、寄存器和 8 位地址宽度检查。
 * @return  当前身份或坐标响应配置的 HAL 状态；失败时仍填入按下数据，检验生产代码不会误用它。
 */
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *handle, uint16_t address,
    uint16_t reg, uint16_t width, uint8_t *bytes, uint16_t count, uint32_t timeout_ms)
{
    CHECK(handle == &hi2c3 && address == 0x70U);
    CHECK(width == I2C_MEMADD_SIZE_8BIT && timeout_ms == 2U && bytes != NULL);
    ++calls;
    tick_ms += read_duration_ms;
    if (reg == 0xA8U)
    {
        CHECK(count == 1U);
        ++id_calls;
        bytes[0] = chip_id;
        return id_result;
    }
    CHECK(reg == 0x02U && count == sizeof response);
    ++point_calls;
    memcpy(bytes, response, sizeof response);
    return point_result;
}

/** @brief 复位仿真边界和真实 BSP 软件状态，预置合法身份与单点。 */
static void Reset(void)
{
    hi2c3 = (I2C_HandleTypeDef){HAL_I2C_STATE_READY, 0U, 0U};
    calls = id_calls = point_calls = 0U;
    tick_ms = 100U;
    read_duration_ms = 3U;
    chip_id = 0x51U;
    id_result = point_result = HAL_OK;
    response[0] = 1U;
    response[1] = 0U;
    response[2] = 100U;
    response[3] = 0U;
    response[4] = 200U;
    x = y = -1;
    BSP_Touch_Init();
}

/** @brief 获取真实诊断快照，避免测试访问生产模块内部变量。 */
static BSP_TouchSnapshot Snapshot(void)
{
    BSP_TouchSnapshot result;
    BSP_Touch_GetSnapshot(&result);
    return result;
}

/** @brief 为边界桩编码控制器原始 X/Y、事件与触点数量；raw 坐标为 12 位。 */
static void Point(uint8_t count, uint16_t raw_x, uint16_t raw_y, uint8_t event)
{
    response[0] = count;
    response[1] = (uint8_t)((event << 6U) | ((raw_x >> 8U) & 0x0FU));
    response[2] = (uint8_t)raw_x;
    response[3] = (uint8_t)(0xA0U | ((raw_y >> 8U) & 0x0FU));
    response[4] = (uint8_t)raw_y;
}

/** @brief 初始化须纯软件；首次采样只验证 ID，下一次才读取坐标。 */
static void TestIdentity(void)
{
    Reset();
    CHECK(calls == 0U && !Snapshot().ready && !Snapshot().sample_valid);
    CHECK(!BSP_Touch_Sample(0U, &x, &y));
    CHECK(id_calls == 1U && point_calls == 0U && x == 0 && y == 0);
    CHECK(Snapshot().ready && !Snapshot().pressed && !Snapshot().sample_valid);
    CHECK(!BSP_Touch_Sample(49U, &x, &y) && calls == 1U);
    CHECK(BSP_Touch_Sample(50U, &x, &y) && calls == 2U);
    CHECK(x == 200 && y == 100 && Snapshot().sample_valid);
    CHECK(Snapshot().reads == 2U && Snapshot().max_read_ms == 3U);
    CHECK(Snapshot().io_errors == 0U && Snapshot().id_mismatches == 0U);
}

/** @brief 四角按官方 XY 交换映射；多点、越界和无事件均不得产生有效按下。 */
static void TestCoordinates(void)
{
    Reset();
    (void)BSP_Touch_Sample(0U, &x, &y);
    const uint16_t corners[4][2] = {{0U, 0U}, {271U, 0U}, {0U, 479U}, {271U, 479U}};
    for (unsigned i = 0U; i < 4U; ++i)
    {
        Point(1U, corners[i][0], corners[i][1], 0U);
        CHECK(BSP_Touch_Sample((i + 1U) * 50U, &x, &y));
        CHECK(x == corners[i][1] && y == corners[i][0] && Snapshot().sample_valid);
    }
    Point(0x81U, 42U, 300U, 2U);
    CHECK(BSP_Touch_Sample(250U, &x, &y) && x == 300 && y == 42);
    Point(2U, 42U, 300U, 0U);
    CHECK(!BSP_Touch_Sample(300U, &x, &y) && !Snapshot().sample_valid && x == 0 && y == 0);
    Point(15U, 42U, 300U, 0U);
    CHECK(!BSP_Touch_Sample(350U, &x, &y) && !Snapshot().sample_valid);
    Point(1U, 272U, 300U, 0U);
    CHECK(!BSP_Touch_Sample(400U, &x, &y) && !Snapshot().sample_valid);
    Point(1U, 42U, 480U, 0U);
    CHECK(!BSP_Touch_Sample(450U, &x, &y) && !Snapshot().sample_valid);
    Point(1U, 4095U, 4095U, 0U);
    CHECK(!BSP_Touch_Sample(500U, &x, &y) && !Snapshot().sample_valid);
    Point(1U, 42U, 300U, 3U);
    CHECK(!BSP_Touch_Sample(550U, &x, &y) && !Snapshot().sample_valid);
    Point(1U, 42U, 300U, 1U);
    CHECK(!BSP_Touch_Sample(600U, &x, &y) && Snapshot().sample_valid);
    Point(0U, 4095U, 4095U, 0U);
    CHECK(!BSP_Touch_Sample(650U, &x, &y) && Snapshot().sample_valid);
    CHECK(Snapshot().ready && !Snapshot().pressed && Snapshot().x == 0U && Snapshot().y == 0U);
}

/** @brief 同一采样间隔内复用合法按下/释放缓存，不增加总线流量。 */
static void TestCache(void)
{
    Reset();
    (void)BSP_Touch_Sample(100U, &x, &y);
    CHECK(BSP_Touch_Sample(150U, &x, &y));
    Point(0U, 0U, 0U, 0U);
    for (uint32_t now_ms = 151U; now_ms < 200U; ++now_ms)
    {
        CHECK(BSP_Touch_Sample(now_ms, &x, &y) && x == 200 && y == 100);
        CHECK(Snapshot().sample_valid && calls == 2U);
    }
    CHECK(!BSP_Touch_Sample(200U, &x, &y) && calls == 3U && Snapshot().sample_valid);
    CHECK(!BSP_Touch_Sample(249U, &x, &y) && calls == 3U && Snapshot().sample_valid);
}

/** @brief 身份不符与身份读错误按 250ms 退避，恢复后仍须先验证身份。 */
static void TestRetry(void)
{
    Reset();
    chip_id = 0x11U;
    CHECK(!BSP_Touch_Sample(0U, &x, &y) && id_calls == 1U && !Snapshot().ready);
    CHECK(Snapshot().id_mismatches == 1U && !Snapshot().sample_valid);
    for (uint32_t now_ms = 1U; now_ms < 250U; ++now_ms)
        CHECK(!BSP_Touch_Sample(now_ms, &x, &y) && calls == 1U);
    id_result = HAL_TIMEOUT;
    hi2c3.error = HAL_I2C_ERROR_TIMEOUT;
    CHECK(!BSP_Touch_Sample(250U, &x, &y) && calls == 2U && Snapshot().io_errors == 1U);
    CHECK(Snapshot().last_hal_error == HAL_I2C_ERROR_TIMEOUT);
    chip_id = 0x51U;
    id_result = HAL_OK;
    CHECK(!BSP_Touch_Sample(499U, &x, &y) && calls == 2U);
    CHECK(!BSP_Touch_Sample(500U, &x, &y) && Snapshot().ready && calls == 3U);
    CHECK(Snapshot().last_hal_error == 0U && !Snapshot().sample_valid);
    CHECK(BSP_Touch_Sample(550U, &x, &y) && point_calls == 1U);
}

/** @brief HAL 状态/硬件 BUSY 均无读事务并释放，且忙尝试也受退避限制。 */
static void TestBusy(void)
{
    Reset();
    hi2c3.state = HAL_I2C_STATE_RESET;
    CHECK(!BSP_Touch_Sample(0U, &x, &y) && calls == 0U && Snapshot().busy_skips == 1U);
    hi2c3.state = HAL_I2C_STATE_READY;
    CHECK(!BSP_Touch_Sample(249U, &x, &y) && calls == 0U);
    CHECK(!BSP_Touch_Sample(250U, &x, &y) && calls == 1U && Snapshot().ready);
    CHECK(BSP_Touch_Sample(300U, &x, &y));
    hi2c3.flags = I2C_FLAG_BUSY;
    hi2c3.error = 123U;
    CHECK(!BSP_Touch_Sample(350U, &x, &y) && calls == 2U && !Snapshot().sample_valid);
    CHECK(x == 0 && y == 0 && !Snapshot().pressed && Snapshot().last_hal_error == 0U);
    hi2c3.flags = 0U;
    CHECK(!BSP_Touch_Sample(599U, &x, &y) && calls == 2U);
    CHECK(BSP_Touch_Sample(600U, &x, &y) && calls == 3U);
    hi2c3.state = HAL_I2C_STATE_BUSY_RX;
    CHECK(!BSP_Touch_Sample(650U, &x, &y) && calls == 3U && Snapshot().busy_skips == 3U);
}

/** @brief HAL_BUSY/错误填入的旧按下数据不会发布，故障后限频恢复。 */
static void TestFault(void)
{
    Reset();
    (void)BSP_Touch_Sample(0U, &x, &y);
    CHECK(BSP_Touch_Sample(50U, &x, &y));
    point_result = HAL_BUSY;
    hi2c3.error = 999U;
    CHECK(!BSP_Touch_Sample(100U, &x, &y) && !Snapshot().sample_valid && x == 0 && y == 0);
    CHECK(Snapshot().busy_skips == 1U && Snapshot().last_hal_error == 0U);
    point_result = HAL_ERROR;
    hi2c3.error = HAL_I2C_ERROR_AF;
    CHECK(!BSP_Touch_Sample(349U, &x, &y) && calls == 3U);
    CHECK(!BSP_Touch_Sample(350U, &x, &y) && calls == 4U && !Snapshot().ready);
    CHECK(Snapshot().io_errors == 1U && Snapshot().last_hal_error == HAL_I2C_ERROR_AF);
    CHECK(!BSP_Touch_Sample(599U, &x, &y) && calls == 4U && !Snapshot().sample_valid);
    point_result = HAL_OK;
    CHECK(!BSP_Touch_Sample(600U, &x, &y) && calls == 5U && Snapshot().ready);
    CHECK(BSP_Touch_Sample(650U, &x, &y) && calls == 6U && Snapshot().sample_valid);
    point_result = HAL_TIMEOUT;
    hi2c3.error = HAL_I2C_ERROR_TIMEOUT;
    CHECK(!BSP_Touch_Sample(700U, &x, &y) && Snapshot().io_errors == 2U);
}

/** @brief 正常 50ms 和故障 250ms 调度及实际读耗时可跨 32 位 tick 回绕。 */
static void TestWrap(void)
{
    Reset();
    tick_ms = UINT32_MAX - 1U;
    (void)BSP_Touch_Sample(UINT32_MAX - 75U, &x, &y);
    CHECK(Snapshot().max_read_ms == 3U && calls == 1U);
    CHECK(!BSP_Touch_Sample(UINT32_MAX - 26U, &x, &y) && calls == 1U);
    CHECK(BSP_Touch_Sample(UINT32_MAX - 25U, &x, &y) && calls == 2U);
    CHECK(BSP_Touch_Sample(23U, &x, &y) && calls == 2U);
    CHECK(BSP_Touch_Sample(24U, &x, &y) && calls == 3U);
    Reset();
    id_result = HAL_ERROR;
    (void)BSP_Touch_Sample(UINT32_MAX - 100U, &x, &y);
    id_result = HAL_OK;
    CHECK(!BSP_Touch_Sample(148U, &x, &y) && calls == 1U);
    CHECK(!BSP_Touch_Sample(149U, &x, &y) && calls == 2U && Snapshot().ready);
}

/** @brief 空指针不触发读取，且不能保留旧的有效按下缓存。 */
static void TestArguments(void)
{
    Reset();
    (void)BSP_Touch_Sample(0U, &x, &y);
    CHECK(BSP_Touch_Sample(50U, &x, &y));
    CHECK(!BSP_Touch_Sample(100U, NULL, &y) && y == 0 && calls == 2U);
    CHECK(!Snapshot().sample_valid && !Snapshot().pressed);
    CHECK(!BSP_Touch_Sample(100U, &x, NULL) && x == 0 && calls == 2U);
    CHECK(!BSP_Touch_Sample(100U, NULL, NULL) && calls == 2U);
    BSP_Touch_GetSnapshot(NULL);
    BSP_Touch_Init();
    CHECK(!Snapshot().ready && !Snapshot().sample_valid && Snapshot().reads == 0U && calls == 2U);
}

/** @brief 按命令行选择独立用例，各用例通过后输出名称。 */
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    if (strcmp(argv[1], "identity") == 0) TestIdentity();
    else if (strcmp(argv[1], "coordinates") == 0) TestCoordinates();
    else if (strcmp(argv[1], "cache") == 0) TestCache();
    else if (strcmp(argv[1], "retry") == 0) TestRetry();
    else if (strcmp(argv[1], "busy") == 0) TestBusy();
    else if (strcmp(argv[1], "fault") == 0) TestFault();
    else if (strcmp(argv[1], "wrap") == 0) TestWrap();
    else if (strcmp(argv[1], "arguments") == 0) TestArguments();
    else CHECK(0);
    printf("PASS: FT5336 %s\n", argv[1]);
    return 0;
}
