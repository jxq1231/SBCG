/**
 * @file        ethernet_monitor.c
 * @brief       轮询以太网PHY状态并输出初始化与链路日志。
 * @details     经BSP_Ethernet读取PHY信息、速率与双工状态；初始化失败按2秒重试，链路按250毫秒检查，变化或5秒时输出。
 * @note        前台串行调用；不配置MAC、DMA、NVIC或CubeMX引脚。
 */

#include "ethernet_monitor.h"
#include "BSP/bsp_ethernet.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>

#define ETH_POLL_PERIOD_MS 250U
#define ETH_RETRY_PERIOD_MS 2000U
#define ETH_REFRESH_PERIOD_MS 5000U

static EthernetMonitor_Log emit_line;
static uint32_t last_attempt_ms;
static uint32_t last_poll_ms;
static uint32_t last_log_ms;
static BSP_EthernetLink last_link;
static bool ready;
static bool have_link;

/**
 * @brief 复制已有链路缓存，失效时不泄露旧UP状态。
 * @param now_ms 当前HAL毫秒，允许回绕。
 * @param out 可空输出，仅本次调用写入，不保留地址。
 * @return 无返回值；不访问PHY，不改变轮询或日志节流。
 */
void EthernetMonitor_GetSnapshot(uint32_t now_ms, EthernetMonitorSnapshot *out)
{
    if (out == NULL) { return; }
    *out = (EthernetMonitorSnapshot){0};
    out->age_ms = now_ms - last_poll_ms;
    out->valid = ready && have_link && out->age_ms < 1000U;
    out->link_up = out->valid && last_link.state == BSP_ETH_LINK_UP;
}

/**
 * @brief   将PHY访问状态转换为日志用字符串。
 * @param   status BSP以太网访问结果；未列出的值输出IO_ERROR。
 * @return  指向静态只读字符串的指针，调用方不得释放。
 */
static const char *StatusName(BSP_EthernetStatus status)
{
    switch (status)
    {
        case BSP_ETH_OK: return "OK";
        case BSP_ETH_NOT_READY: return "NOT_READY";
        case BSP_ETH_NOT_FOUND: return "NOT_FOUND";
        case BSP_ETH_ID_MISMATCH: return "ID_MISMATCH";
        default: return "IO_ERROR";
    }
}

/**
 * @brief   保存日志接口并复位PHY轮询与重试状态。
 * @param   now_ms 前台毫秒计数；初始化差值允许无符号回绕。
 * @param   log_line 日志回调，可为NULL以停用处理；后续调用期间须有效。
 * @return  无返回值。
 * @note    不访问PHY；首次Process即可尝试初始化。
 */
void EthernetMonitor_Init(uint32_t now_ms, EthernetMonitor_Log log_line)
{
    emit_line = log_line;
    ready = false;
    have_link = false;
    last_attempt_ms = now_ms - ETH_RETRY_PERIOD_MS;
    last_poll_ms = last_log_ms = now_ms;
}

/**
 * @brief   按实际经过的毫秒数重试初始化、读取并报告PHY链路。
 * @param   now_ms 前台当前毫秒计数，差值以uint32_t处理回绕。
 * @return  无返回值；读取失败会回到初始化重试状态。
 * @note    主循环调用，非精确周期任务；同步调用BSP与日志接口。
 */
void EthernetMonitor_Process(uint32_t now_ms)
{
    char line[160];
    if (emit_line == NULL)
    {
        return;
    }
    if (!ready)
    {
        if ((uint32_t)(now_ms - last_attempt_ms) < ETH_RETRY_PERIOD_MS)
        {
            return;
        }
        BSP_EthernetInfo info;
        const BSP_EthernetStatus status = BSP_Ethernet_Init(&info);
        /* Retry interval is measured from the attempt's start tick. There is
           at most one attempt per call, even after a slow HAL access. */
        last_attempt_ms = now_ms;
        const int length = snprintf(line, sizeof(line),
            "$ETH,INIT=%s,PHY_ADDR=%" PRIu32 ",PHY_ID1=%04" PRIX16
            ",PHY_ID2=%04" PRIX16 ",AUTONEG=%u\r\n",
            StatusName(status), info.phy_addr, info.phy_id1, info.phy_id2,
            info.autoneg_enabled ? 1U : 0U);
        emit_line(line, length);
        if (status != BSP_ETH_OK)
        {
            return;
        }
        ready = true;
        have_link = false;
        last_poll_ms = now_ms - ETH_POLL_PERIOD_MS;
    }

    if ((uint32_t)(now_ms - last_poll_ms) < ETH_POLL_PERIOD_MS)
    {
        return;
    }
    last_poll_ms = now_ms;
    BSP_EthernetLink link;
    if (BSP_Ethernet_ReadLink(&link) != BSP_ETH_OK)
    {
        static const char error_line[] = "$ETH,LINK=READ_ERROR\r\n";
        emit_line(error_line, (int)(sizeof(error_line) - 1U));
        ready = false;
        last_attempt_ms = now_ms;
        return;
    }
    const bool changed = !have_link || (link.state != last_link.state) ||
        (link.speed_mbps != last_link.speed_mbps) ||
        (link.full_duplex != last_link.full_duplex);
    if (changed || ((uint32_t)(now_ms - last_log_ms) >= ETH_REFRESH_PERIOD_MS))
    {
        const char *state = link.state == BSP_ETH_LINK_UP ? "UP" :
            (link.state == BSP_ETH_LINK_NEGOTIATING ? "NEGOTIATING" : "DOWN");
        const char *duplex = link.state != BSP_ETH_LINK_UP ? "UNKNOWN" :
            (link.full_duplex ? "FULL" : "HALF");
        const int length = snprintf(line, sizeof(line),
            "$ETH,LINK=%s,SPEED_MBPS=%" PRIu32 ",DUPLEX=%s\r\n",
            state, link.speed_mbps, duplex);
        emit_line(line, length);
        last_link = link;
        have_link = true;
        last_log_ms = now_ms;
    }
}
