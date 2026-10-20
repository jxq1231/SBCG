#include "bsp_ptp_tx.h"
#include "main.h"
#include <stdbool.h>

extern ETH_HandleTypeDef heth;
static ETH_DMADescTypeDef *pending_ring;
static uint32_t pending_mask, armed_index, buffer_address, frame_bytes;
static BSP_PtpTime before;
static bool armed;

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

static bool Earlier(BSP_PtpTime a, BSP_PtpTime b)
{ return a.seconds < b.seconds || (a.seconds == b.seconds && a.nanoseconds < b.nanoseconds); }

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
