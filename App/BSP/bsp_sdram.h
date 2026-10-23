/**
 * @file        bsp_sdram.h
 * @brief       使用CubeMX生成的FMC资源初始化SDRAM并推进独占内存自检。
 * @details     不设置GPIO/RCC/MPU，不使用DMA/IRQ，不提供未通过测试的帧缓冲。
 * @note        仅启动/前台串行调用；8MB内存测试是破坏性的，完成后不自动重启。
 */
#ifndef SBCG_BSP_SDRAM_H
#define SBCG_BSP_SDRAM_H
#include "sdram_test.h"
#include <stdbool.h>
#include <stdint.h>
#define BSP_SDRAM_BASE UINT32_C(0xC0000000)
#define BSP_SDRAM_BYTES UINT32_C(0x00800000)
typedef enum {
    BSP_SDRAM_OFF, BSP_SDRAM_CONFIG_ERROR, BSP_SDRAM_COMMAND_ERROR,
    BSP_SDRAM_TIMEOUT, BSP_SDRAM_REFRESH_ERROR, BSP_SDRAM_TESTING,
    BSP_SDRAM_PASS, BSP_SDRAM_MEMORY_ERROR
} BSP_SdramStatus;
/** @brief 只读诊断副本：时钟Hz、累计校验字节、耗时ms、错误偏移字节与32位数据。 */
typedef struct {
    BSP_SdramStatus status;
    SDRAM_TestPhase phase;
    uint32_t command_index, sdclk_hz, refresh_count, pass, verified_bytes;
    uint32_t elapsed_ms, error_offset_bytes, expected, actual;
} BSP_SdramSnapshot;
/**
 * @brief 在生成FMC/MPU初始化后发送芯片上电命令，成功后仅建立测试状态。
 * @return true表示芯片初始化成功、测试尚未完成；false表示错误并停止后续访问。
 * @note 前台音频启动前调用；唯一1ms等待用于SDRAM上电，命令BUSY双重限时。
 *       重复调用不重发命令/不破坏内存；失败后需要复位再试。
 */
bool BSP_SDRAM_Init(void);
/**
 * @brief 推进一次有界内存测试，不重新初始化资源。
 * @param now_ms 当前HAL毫秒计数，可回绕。
 * @param allow_work false时不访问SDRAM，供DHT起始低电平保护使用。
 * @return 无返回值；终态不再访问内存，无自动恢复/重试。
 */
void BSP_SDRAM_Process(uint32_t now_ms, bool allow_work);
/**
 * @brief 复制测试和初始化结果，无目标内存读取。
 * @param snapshot 调用方输出，NULL安全无操作；不保存指针。
 * @return 无返回值；仅前台串行使用。
 */
void BSP_SDRAM_GetSnapshot(BSP_SdramSnapshot *snapshot);
#endif
