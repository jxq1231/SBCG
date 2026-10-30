/**
 * @file        bsp_touch.c
 * @brief       FT5336 身份核对和 480×272 横屏单点轮询实现。
 * @details     复用已生成的 I2C3，每次到期仅执行一笔身份或五字节触控读，缓存短间隔请求；失败立即使手势无效。
 * @note        仅供与 WM8994 顺序访问总线的前台调用，不初始化/复位外设，不写控制器，不操作 GPIO、IRQ 或 DMA。
 */

#include "bsp_touch.h"
#include "main.h"
#include <stddef.h>

extern I2C_HandleTypeDef hi2c3;

#define TOUCH_I2C_HANDLE hi2c3
#define TOUCH_I2C_ADDRESS 0x70U
#define TOUCH_ID_REGISTER 0xA8U
#define TOUCH_EXPECTED_ID 0x51U
#define TOUCH_STATUS_REGISTER 0x02U
#define TOUCH_POLL_INTERVAL_MS 50U
#define TOUCH_RETRY_INTERVAL_MS 250U
#define TOUCH_READ_TIMEOUT_MS 2U
#define TOUCH_WIDTH_PX 480U
#define TOUCH_HEIGHT_PX 272U
#define TOUCH_EVENT_LIFT_UP 1U
#define TOUCH_EVENT_NO_EVENT 3U

/* 寄存器与方向依据本地官方 STM32Cube_FW_F7_V1.17.4：
 * Drivers/BSP/Components/ft5336/ft5336.h：0x70 地址，0xA8 身份，0x02 状态及 0x03..0x06 首点坐标。
 * Drivers/BSP/STM32746G-Discovery/stm32746g_discovery_ts.c：BSP_TS_Init 采用 TS_SWAP_XY，
 * BSP_TS_GetState 对 FT5336 直接使用 x=raw_y、y=raw_x 的像素值，不作 4096 缩放或轴翻转。
 * 仅采用这些协议事实，不调用官方 BSP 的初始化、写寄存器或总线恢复流程。
 */
static BSP_TouchSnapshot touch;
static uint32_t last_attempt_ms;
static uint32_t retry_interval_ms;
static bool attempt_recorded;

/**
 * @brief   清除缓存按下及坐标，并说明此次释放能否作为正常手势结束。
 * @param   valid true 表示合法零点或抬起；false 表示身份未通过、故障或非法输入，需要取消手势。
 * @return  无返回值。
 * @note    不改变身份验证和累计诊断；只由前台软件路径调用。
 */
static void TouchRelease(bool valid)
{
    touch.sample_valid = valid;
    touch.pressed = false;
    touch.x = 0U;
    touch.y = 0U;
}

/**
 * @brief   检查共享总线后执行一次有限长度同步读，记录实际耗时和错误。
 * @param   reg FT5336 的 8 位寄存器地址，当前仅为身份或状态起始地址。
 * @param   bytes 本次调用期间有效的非空调用者缓冲区；成功时由 HAL 填入，失败时内容不可使用。
 * @param   count 读取字节数，当前调用仅为 1 或 5 字节。
 * @return  true 表示 HAL_OK；false 表示忙或读错误，缓存已变为无效释放并启用 250 ms 退避。
 * @note    调用前必须登记本次采样时刻，避免失败路径被主循环重复调用。共享总线不能在本函数中重初始化或恢复。
 *          HAL 参数为 2 ms，但本工程 HAL 内部另有 25 ms BUSY 等待，不能据此承诺调用总时长不超过 2 ms。
 */
static bool TouchRead(uint16_t reg, uint8_t *bytes, uint16_t count)
{
    touch.last_hal_error = 0U;
    if (HAL_I2C_GetState(&TOUCH_I2C_HANDLE) != HAL_I2C_STATE_READY ||
        __HAL_I2C_GET_FLAG(&TOUCH_I2C_HANDLE, I2C_FLAG_BUSY) == SET)
    {
        ++touch.busy_skips;
        retry_interval_ms = TOUCH_RETRY_INTERVAL_MS;
        return false;
    }

    ++touch.reads;
    const uint32_t start_ms = HAL_GetTick();
    const HAL_StatusTypeDef status = HAL_I2C_Mem_Read(&TOUCH_I2C_HANDLE,
        TOUCH_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT, bytes, count, TOUCH_READ_TIMEOUT_MS);
    const uint32_t read_ms = (uint32_t)(HAL_GetTick() - start_ms);
    if (read_ms > touch.max_read_ms) { touch.max_read_ms = read_ms; }
    if (status == HAL_OK) { return true; }

    retry_interval_ms = TOUCH_RETRY_INTERVAL_MS;
    if (status == HAL_BUSY)
    {
        ++touch.busy_skips;
    }
    else
    {
        ++touch.io_errors;
        touch.last_hal_error = HAL_I2C_GetError(&TOUCH_I2C_HANDLE);
        touch.ready = false;
    }
    return false;
}

/**
 * @brief   将一次成功读取的状态和首点解码为横屏单点缓存。
 * @param   bytes 调用者拥有的五字节只读缓冲区，包含 TD_STAT、XH、XL、YH、YL；有效期覆盖本次调用，不得为 NULL。
 * @return  无返回值。
 * @note    控制器 X/Y 高字节的低四位组成 12 位坐标，事件位与触点 ID 不参与坐标。
 *          多点、超出横屏边界或 NO_EVENT 取消手势；合法零点和 LIFT_UP 为有效释放。
 */
static void TouchDecode(const uint8_t bytes[5])
{
    const uint8_t count = bytes[0] & 0x0FU;
    if (count == 0U)
    {
        TouchRelease(true);
        return;
    }
    if (count != 1U) { return; }

    const uint16_t raw_x = (uint16_t)(((uint16_t)(bytes[1] & 0x0FU) << 8U) | bytes[2]);
    const uint16_t raw_y = (uint16_t)(((uint16_t)(bytes[3] & 0x0FU) << 8U) | bytes[4]);
    const uint8_t event = bytes[1] >> 6U;
    if (raw_y >= TOUCH_WIDTH_PX || raw_x >= TOUCH_HEIGHT_PX || event == TOUCH_EVENT_NO_EVENT)
    {
        return;
    }
    if (event == TOUCH_EVENT_LIFT_UP)
    {
        TouchRelease(true);
        return;
    }
    touch.sample_valid = true;
    touch.pressed = true;
    touch.x = raw_y;
    touch.y = raw_x;
}

/**
 * @brief   清空前台触控缓存、累计诊断及调度时刻，不发起硬件访问。
 * @return  无返回值。
 * @note    首次到期由 Sample 读身份；本函数不调用 HAL 初始化、写入、延时或总线恢复。
 */
void BSP_Touch_Init(void)
{
    touch = (BSP_TouchSnapshot){0};
    last_attempt_ms = 0U;
    retry_interval_ms = TOUCH_POLL_INTERVAL_MS;
    attempt_recorded = false;
}

/**
 * @brief   按前台 tick 限频执行一笔只读事务，再向调用者返回当前缓存。
 * @param   now_ms HAL 毫秒 tick；无符号差支持正常 32 位回绕，不承诺处理跨完整周期未调用的情况。
 * @param   x 调用者拥有的横屏 X 像素输出，不得为 NULL；释放/无效时清零。
 * @param   y 调用者拥有的横屏 Y 像素输出，不得为 NULL；释放/无效时清零。
 * @return  true 表示缓存为有效单点按下；false 表示释放、故障、非法输入或参数错误。
 * @note    每次尝试先登记时刻并清除旧按下，身份读成功也须等下一次 50 ms 到期才读坐标。
 *          仅前台串行调用；所有忙/失败路径保留无效释放，不在 ISR 内执行，不修改 WM8994 或外设配置。
 */
bool BSP_Touch_Sample(uint32_t now_ms, int32_t *x, int32_t *y)
{
    if (x == NULL || y == NULL)
    {
        TouchRelease(false);
        if (x != NULL) { *x = 0; }
        if (y != NULL) { *y = 0; }
        return false;
    }

    if (!attempt_recorded || (uint32_t)(now_ms - last_attempt_ms) >= retry_interval_ms)
    {
        attempt_recorded = true;
        last_attempt_ms = now_ms;
        retry_interval_ms = TOUCH_POLL_INTERVAL_MS;
        TouchRelease(false);

        if (!touch.ready)
        {
            uint8_t identity = 0U;
            if (TouchRead(TOUCH_ID_REGISTER, &identity, 1U))
            {
                if (identity == TOUCH_EXPECTED_ID) { touch.ready = true; }
                else
                {
                    ++touch.id_mismatches;
                    retry_interval_ms = TOUCH_RETRY_INTERVAL_MS;
                }
            }
        }
        else
        {
            uint8_t bytes[5] = {0U};
            if (TouchRead(TOUCH_STATUS_REGISTER, bytes, sizeof bytes)) { TouchDecode(bytes); }
        }
    }
    *x = touch.x;
    *y = touch.y;
    return touch.pressed;
}

/**
 * @brief   将前台缓存和累计诊断复制给调用者。
 * @param   snapshot 调用者拥有的输出；NULL 时无操作。
 * @return  无返回值。
 * @note    不读取 I2C，不改变限频状态。sample_valid 用于区分正常抬起和应取消的故障释放。
 */
void BSP_Touch_GetSnapshot(BSP_TouchSnapshot *snapshot)
{
    if (snapshot != NULL) { *snapshot = touch; }
}
