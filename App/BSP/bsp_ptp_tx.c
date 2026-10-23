/**
 * @file        bsp_ptp_tx.c
 * @brief       同步单缓冲以太网发送的硬件 TX 时间戳交接。
 * @details     发送前记录当前描述符、帧地址/长度和 PTP 时刻并布防；发送后校验 DMA 完成状态、描述符身份及时间
 *          窗口，仅清理 CPU 已收回的请求位。
 * @note        限 NO_SYS 单前台同步发送，要求增强描述符且环映射稳定；一次一组 Begin/End，不拥有传入帧，不在
 *          DMA OWN 期间改写描述符。
 */

#include "bsp_ptp_tx.h"
#include "main.h"
#include <stdbool.h>

extern ETH_HandleTypeDef heth;
static ETH_DMADescTypeDef *pending_ring;
static uint32_t pending_mask, armed_index, buffer_address, frame_bytes;
static BSP_PtpTime before;
static bool armed;

/**
 * @brief   验证当前 ETH 发送描述符环与 HAL 映射仍符合采样前提。
 * @return  OK 表示 STARTED、增强描述符和环映射一致；其它为 NOT_READY 或 BAD_CONFIG。
 * @note    仅同步前台 TX 路径；有待清理描述符时还要求环基址不变，不修改 DMA 描述符。
 */
static BSP_PtpStatus CheckRing(void)
{
    if (heth.Instance != ETH || heth.gState != HAL_ETH_STATE_STARTED) { return BSP_PTP_NOT_READY; }
    if (heth.Init.TxDesc == NULL || heth.TxDescList.CurTxDesc >= ETH_TX_DESC_CNT ||
        (READ_REG(heth.Instance->DMABMR) & ETH_DMABMR_EDE) == 0U) { return BSP_PTP_BAD_CONFIG; }
    for (uint32_t i=0U; i<ETH_TX_DESC_CNT; ++i)
    {
        if (heth.TxDescList.TxDesc[i] != (uint32_t)(uintptr_t)&heth.Init.TxDesc[i])
        { return BSP_PTP_BAD_CONFIG; }
    }
    if (pending_mask != 0U && pending_ring != heth.Init.TxDesc) { return BSP_PTP_BAD_CONFIG; }
    return BSP_PTP_OK;
}

/**
 * @brief   清理本模块已完成发送描述符上的时间戳请求位。
 * @return  OK 表示无待清理请求；NOT_READY 表示仍已布防或存在 DMA 所有描述符；其它为环配置错误。
 * @note    仅单一前台 NO_SYS 发送流程；只在 OWN 清零后清 TTSE，用屏障交接，不触碰 DMA 仍拥有的描述符。HAL 超
 *          时已清状态字的情况不重复改写。
 */
BSP_PtpStatus BSP_PTP_TxClean(void)
{
    if (armed) { return BSP_PTP_NOT_READY; }
    if (pending_mask == 0U) { return BSP_PTP_OK; }
    BSP_PtpStatus status = CheckRing();
    if (status != BSP_PTP_OK) { return status; }
    for (uint32_t i=0U; i<ETH_TX_DESC_CNT; ++i)
    {
        ETH_DMADescTypeDef *d = &pending_ring[i];
        if ((pending_mask & (1U<<i)) != 0U && (READ_REG(d->DESC0) & ETH_DMATXDESC_OWN) == 0U)
        {
            /* Normal completion returns OWN. HAL timeout instead clears the
               whole status word: do not rewrite that already-disarmed case.
               No other context submits frames in this NO_SYS workflow. */
            if ((READ_REG(d->DESC0) & ETH_DMATXDESC_TTSE) != 0U)
            {
                __DMB();
                CLEAR_BIT(d->DESC0, ETH_DMATXDESC_TTSE);
                __DMB();
            }
            pending_mask &= ~(1U<<i);
        }
    }
    return pending_mask == 0U ? BSP_PTP_OK : BSP_PTP_NOT_READY;
}

/**
 * @brief   按秒和纳秒字典序比较两个本地 PTP 时间。
 * @param   a 被判断是否更早的时间值，按值传入。
 * @param   b 比较基准时间值，按值传入。
 * @return  true 表示 a 严格早于 b；相等返回 false。
 * @note    使用普通无符号秒比较，未对 32 位秒计数回绕作特殊处理；输入时间合法性由调用路径另行检查。
 */
static bool Earlier(BSP_PtpTime a, BSP_PtpTime b)
{ return a.seconds < b.seconds || (a.seconds == b.seconds && a.nanoseconds < b.nanoseconds); }

/**
 * @brief   为即将同步发送的单缓冲以太网帧布防一次硬件 TX 时间戳。
 * @param   buffer 非空只读帧首地址；调用方保持发送期间有效，不转移所有权，模块仅保存数值地址以便 End 校验。
 * @param   length 帧长度，单位字节；必须大于零且不超过单描述符 TBS1 容量。
 * @return  OK 表示当前空闲描述符已插入时间戳请求；其它表示参数、描述符、PTP 或 HAL 状态不符。
 * @note    仅主循环，须在一次同步单缓冲发送前调用；一次只允许一组 Begin/End。清旧 TTSS 并保存发送前 PTP 时刻
 *          ；Begin 本身不发送帧，不支持并发提交。
 */
BSP_PtpStatus BSP_PTP_TxBegin(const uint8_t *buffer, uint16_t length)
{
    if (buffer == NULL || length == 0U || length > ETH_DMATXDESC_TBS1) { return BSP_PTP_BAD_SNAPSHOT; }
    BSP_PtpStatus status = BSP_PTP_TxClean();
    if (status != BSP_PTP_OK) { return status; }
    status = CheckRing();
    if (status != BSP_PTP_OK) { return status; }
    const uint32_t index = heth.TxDescList.CurTxDesc;
    ETH_DMADescTypeDef *d = &heth.Init.TxDesc[index];
    if ((READ_REG(d->DESC0) & ETH_DMATXDESC_OWN) != 0U || heth.TxDescList.PacketAddress[index] != NULL)
    { return BSP_PTP_NOT_READY; }
    BSP_PtpTime sampled_before;
    status = BSP_PTP_Read(&sampled_before);
    if (status != BSP_PTP_OK) { return status; }
    __DMB();
    /* A valid-looking old timestamp must never satisfy this request. */
    CLEAR_BIT(d->DESC0, ETH_DMATXDESC_TTSS);
    pending_ring = heth.Init.TxDesc;
    pending_mask |= 1U<<index;
    if (HAL_ETH_PTP_InsertTxTimestamp(&heth) != HAL_OK)
    {
        (void)BSP_PTP_TxClean();
        return BSP_PTP_HAL_ERROR;
    }
    armed_index = index;
    buffer_address = (uint32_t)(uintptr_t)buffer;
    frame_bytes = length;
    before = sampled_before;
    armed = true;
    return BSP_PTP_OK;
}

/**
 * @brief   在同步发送返回后验证所布防帧的 DMA TX 时间戳。
 * @param   time 调用者拥有的输出；NULL 会使已布防采样失败，成功才写入，失败保留原内容。
 * @return  OK 表示描述符、缓冲地址、长度及发送前后时间窗全部匹配；其它表示未布防或快照/配置/HAL 错误。
 * @note    仅与成功 Begin 配对的前台路径；即使失败也解除 armed 并尝试清理。要求 OWN 已归 CPU、单帧 FS/LS/TTSS
 *          且无 ES，再读 DESC6/7；屏障不等于 D-Cache 维护，当前启动代码未启用缓存。
 */
BSP_PtpStatus BSP_PTP_TxEnd(BSP_PtpTime *time)
{
    if (!armed) { return BSP_PTP_NOT_READY; }
    armed = false;
    BSP_PtpStatus status = CheckRing();
    BSP_PtpTime sample = {0U,0U};
    if (status == BSP_PTP_OK)
    {
        ETH_DMADescTypeDef *d = &pending_ring[armed_index];
        const uint32_t required = ETH_DMATXDESC_FS | ETH_DMATXDESC_LS | ETH_DMATXDESC_TTSS;
        const uint32_t mask = required | ETH_DMATXDESC_OWN | ETH_DMATXDESC_ES;
        if (time == NULL || heth.TxDescList.CurTxDesc != (armed_index+1U)%ETH_TX_DESC_CNT ||
            (READ_REG(d->DESC0) & mask) != required)
        { status = BSP_PTP_BAD_SNAPSHOT; }
        else
        {
            /* OWN=0 acquires completed DMA writeback before reading DESC6/7.
               Cache remains disabled; a barrier is not cache maintenance. */
            __DMB();
            sample = (BSP_PtpTime){READ_REG(d->DESC7),READ_REG(d->DESC6)};
            if (READ_REG(d->DESC2) != buffer_address ||
                (READ_REG(d->DESC1) & (ETH_DMATXDESC_TBS1|ETH_DMATXDESC_TBS2)) != frame_bytes ||
                sample.nanoseconds >= 1000000000U || Earlier(sample,before))
            { status = BSP_PTP_BAD_SNAPSHOT; }
            else
            {
                BSP_PtpTime after;
                status = BSP_PTP_Read(&after);
                if (status == BSP_PTP_OK && Earlier(after,sample)) { status = BSP_PTP_BAD_SNAPSHOT; }
            }
        }
    }
    (void)BSP_PTP_TxClean(); /* Leaves a DMA-owned descriptor completely untouched. */
    if (status == BSP_PTP_OK) { *time = sample; }
    return status;
}
