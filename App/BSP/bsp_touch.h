/**
 * @file        bsp_touch.h
 * @brief       FT5336 前台限频读取接口及触控有效性诊断。
 * @details     提供软件状态初始化、480×272 横屏单点采样和最近采样快照；不承担 I2C3 外设配置。
 * @note        由 TouchGFX 前台调用，调用者须与 WM8994 顺序访问已生成的共享 I2C3，禁止在 ISR 调用。
 */

#ifndef BSP_TOUCH_H
#define BSP_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 最近一次触控状态及累计读诊断；全部字段由前台采样更新。 */
typedef struct
{
    bool ready;              /**< 已通过 FT5336 身份验证；不代表最近采样成功。 */
    bool sample_valid;       /**< 最近坐标读为合法零点/单点；故障、多点和越界均为 false。 */
    bool pressed;            /**< sample_valid 且存在合法 DOWN/CONTACT 单点。 */
    uint16_t x;              /**< 横屏像素 X，范围 0..479；释放/无效时为零。 */
    uint16_t y;              /**< 横屏像素 Y，范围 0..271；释放/无效时为零。 */
    uint32_t reads;          /**< 实际发起的 HAL 同步读次数，包含身份和坐标读。 */
    uint32_t io_errors;      /**< HAL_ERROR/HAL_TIMEOUT 次数，不包含 BUSY。 */
    uint32_t busy_skips;     /**< 外设状态/硬件 BUSY 或 HAL_BUSY 引起的跳过次数。 */
    uint32_t id_mismatches;  /**< 成功读取但身份不为 0x51 的次数。 */
    uint32_t last_hal_error; /**< 最近已发起失败事务的 HAL 错误；BUSY/成功时为零。 */
    uint32_t max_read_ms;    /**< 单次 HAL 读实际最长 tick 差，单位 ms，允许计数回绕。 */
} BSP_TouchSnapshot;

/**
 * @brief   清空本模块软件状态，不访问或重新配置任何外设。
 * @return  无返回值。
 * @note    应在 TouchGFX 初始化阶段调用；首次 Sample 仅验证芯片身份，随后才读取坐标。
 */
void BSP_Touch_Init(void);

/**
 * @brief   返回限频缓存或执行一次 FT5336 同步读，输出横屏单点状态。
 * @param   now_ms 调用者提供的 HAL 毫秒 tick；无符号差处理 UINT32_MAX 回绕。
 * @param   x 调用者拥有的非空输出，按下时为 0..479 像素；释放或无效时清零。
 * @param   y 调用者拥有的非空输出，按下时为 0..271 像素；释放或无效时清零。
 * @return  true 表示当前缓存为合法单点按下；false 表示释放、无效或参数为空。
 * @note    正常读间隔至少 50 ms，身份未就绪/总线失败后至少 250 ms；每次最多一笔读，先记时再尝试。
 *          短间隔调用保留缓存及 sample_valid；错误立即清除按下，调用者须查询 sample_valid 区分取消和正常释放。
 *          空指针不发起事务并使缓存无效。仅前台串行调用；HAL 超时参数为 2 ms，内部 BUSY 等待可超出此值。
 */
bool BSP_Touch_Sample(uint32_t now_ms, int32_t *x, int32_t *y);

/**
 * @brief   复制最近状态和累计诊断，不读取硬件。
 * @param   snapshot 调用者拥有的输出；允许 NULL，NULL 时无操作。
 * @return  无返回值。
 * @note    sample_valid 在限频缓存期间保持不变；合法零点与 LIFT_UP 为有效释放，NO_EVENT 为无效输入。
 *          收到无效释放时，界面应取消本次手势，避免将总线故障、多点或越界误判为点击完成。
 */
void BSP_Touch_GetSnapshot(BSP_TouchSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
