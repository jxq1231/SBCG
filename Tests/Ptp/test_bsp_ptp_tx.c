#include "BSP/bsp_ptp_tx.h"
#include "main.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

ETH_TypeDef test_eth;
RCC_TypeDef test_rcc;
ETH_HandleTypeDef heth;
static ETH_DMADescTypeDef descriptors[ETH_TX_DESC_CNT];
static uint8_t frame[70];
static BSP_PtpTime current;
static BSP_PtpStatus clock_status;
static unsigned int inserts, barriers, register_writes;
static bool insert_failure;

void PtpTx_TestBarrier(void) { ++barriers; }
void PtpTx_TestClear(volatile uint32_t *reg, uint32_t bits)
{ *reg &= ~bits; ++register_writes; }
BSP_PtpStatus BSP_PTP_Read(BSP_PtpTime *time)
{ *time=current; return clock_status; }
HAL_StatusTypeDef HAL_ETH_PTP_InsertTxTimestamp(ETH_HandleTypeDef *handle)
{
    assert(handle==&heth && heth.TxDescList.CurTxDesc<ETH_TX_DESC_CNT);
    ETH_DMADescTypeDef *d=&descriptors[heth.TxDescList.CurTxDesc];
    assert((d->DESC0 & (ETH_DMATXDESC_OWN|ETH_DMATXDESC_TTSS))==0);
    d->DESC0|=ETH_DMATXDESC_TTSE; ++inserts;
    return insert_failure ? HAL_ERROR : HAL_OK;
}
static void Map(void)
{
    heth.Instance=ETH; heth.gState=HAL_ETH_STATE_STARTED; heth.Init.TxDesc=descriptors;
    for (unsigned int i=0;i<ETH_TX_DESC_CNT;++i)
    { heth.TxDescList.TxDesc[i]=(uint32_t)(uintptr_t)&descriptors[i]; }
}
static void Reset(void)
{
    Map(); heth.TxDescList.CurTxDesc=0;
    for (unsigned int i=0;i<ETH_TX_DESC_CNT;++i) { descriptors[i].DESC0 &= ~ETH_DMATXDESC_OWN; }
    BSP_PtpTime ignored;
    (void)BSP_PTP_TxEnd(&ignored); (void)BSP_PTP_TxClean();
    memset(&heth,0,sizeof(heth)); memset(descriptors,0,sizeof(descriptors));
    memset(&test_eth,0,sizeof(test_eth)); memset(&test_rcc,0,sizeof(test_rcc)); Map();
    test_eth.DMABMR=ETH_DMABMR_EDE;
    current=(BSP_PtpTime){2,100}; clock_status=BSP_PTP_OK;
    inserts=barriers=register_writes=0; insert_failure=false;
}
static void Complete(unsigned int index)
{
    ETH_DMADescTypeDef *d=&descriptors[index];
    d->DESC0=ETH_DMATXDESC_FS|ETH_DMATXDESC_LS|ETH_DMATXDESC_TTSS|ETH_DMATXDESC_TTSE;
    d->DESC1=sizeof(frame); d->DESC2=(uint32_t)(uintptr_t)frame;
    d->DESC6=500; d->DESC7=2;
    heth.TxDescList.CurTxDesc=(index+1)%ETH_TX_DESC_CNT;
    current.nanoseconds=900;
}
int main(void)
{
    Reset(); assert(BSP_PTP_TxBegin(frame,sizeof(frame))==BSP_PTP_OK);
    assert(inserts==1 && (descriptors[0].DESC0 & ETH_DMATXDESC_TTSE));
    Complete(0); BSP_PtpTime time={99,99};
    assert(BSP_PTP_TxEnd(&time)==BSP_PTP_OK && time.seconds==2 && time.nanoseconds==500);
    assert((descriptors[0].DESC0 & ETH_DMATXDESC_TTSE)==0 && barriers>0);
    puts("PASS: TX captures DESC7/6 after a matching completed single-buffer frame and disarms it");

    Reset(); unsigned int last=ETH_TX_DESC_CNT-1; heth.TxDescList.CurTxDesc=last;
    descriptors[last].DESC0=ETH_DMATXDESC_TTSS;
    assert(BSP_PTP_TxBegin(frame,sizeof(frame))==BSP_PTP_OK);
    Complete(last); assert(BSP_PTP_TxEnd(&time)==BSP_PTP_OK);
    puts("PASS: descriptor wrap and stale TTSS cannot reuse the previous frame timestamp");

    Reset(); assert(BSP_PTP_TxBegin(NULL,70)!=BSP_PTP_OK);
    assert(BSP_PTP_TxBegin(frame,0)!=BSP_PTP_OK);
    assert(BSP_PTP_TxBegin(frame,8192)!=BSP_PTP_OK);
    descriptors[0].DESC0=ETH_DMATXDESC_OWN|ETH_DMATXDESC_TTSS;
    uint32_t untouched=descriptors[0].DESC0;
    assert(BSP_PTP_TxBegin(frame,70)!=BSP_PTP_OK && descriptors[0].DESC0==untouched);
    descriptors[0].DESC0=0; heth.TxDescList.PacketAddress[0]=(uint32_t *)(void *)frame;
    assert(BSP_PTP_TxBegin(frame,70)!=BSP_PTP_OK);
    heth.TxDescList.PacketAddress[0]=NULL; heth.TxDescList.CurTxDesc=ETH_TX_DESC_CNT;
    assert(BSP_PTP_TxBegin(frame,70)!=BSP_PTP_OK);
    heth.TxDescList.CurTxDesc=0; heth.TxDescList.TxDesc[0]^=4;
    assert(BSP_PTP_TxBegin(frame,70)!=BSP_PTP_OK);
    Map(); heth.gState=HAL_ETH_STATE_READY;
    assert(BSP_PTP_TxBegin(frame,70)!=BSP_PTP_OK);
    Map(); test_eth.DMABMR=0;
    assert(BSP_PTP_TxBegin(frame,70)!=BSP_PTP_OK);
    test_eth.DMABMR=ETH_DMABMR_EDE; clock_status=BSP_PTP_BAD_CONFIG;
    assert(BSP_PTP_TxBegin(frame,70)!=BSP_PTP_OK && inserts==0);
    puts("PASS: invalid buffers, busy descriptors, state, mapping and clock failures are rejected before writes");

    Reset(); insert_failure=true;
    assert(BSP_PTP_TxBegin(frame,70)==BSP_PTP_HAL_ERROR);
    assert((descriptors[0].DESC0 & ETH_DMATXDESC_TTSE)==0);
    insert_failure=false; assert(BSP_PTP_TxBegin(frame,70)==BSP_PTP_OK);
    Complete(0); assert(BSP_PTP_TxEnd(&time)==BSP_PTP_OK);
    puts("PASS: failed HAL timestamp requests are disarmed and the next request can recover");

    for (unsigned int fault=0;fault<13;++fault)
    {
        Reset(); assert(BSP_PTP_TxBegin(frame,70)==BSP_PTP_OK); Complete(0);
        ETH_DMADescTypeDef *d=&descriptors[0];
        switch(fault)
        {
            case 0:d->DESC0|=ETH_DMATXDESC_OWN;break;
            case 1:d->DESC0|=ETH_DMATXDESC_ES;break;
            case 2:d->DESC0&=~ETH_DMATXDESC_TTSS;break;
            case 3:d->DESC0&=~ETH_DMATXDESC_FS;break;
            case 4:d->DESC0&=~ETH_DMATXDESC_LS;break;
            case 5:heth.TxDescList.CurTxDesc=2;break;
            case 6:d->DESC2^=4;break;
            case 7:d->DESC1=69;break;
            case 8:d->DESC1|=1U<<16;break;
            case 9:d->DESC6=1000000000;break;
            case 10:d->DESC6=901;break;
            case 11:d->DESC6=99;break;
            default:heth.TxDescList.TxDesc[0]^=4;break;
        }
        uint32_t status=d->DESC0; time=(BSP_PtpTime){99,99};
        assert(BSP_PTP_TxEnd(&time)!=BSP_PTP_OK && time.seconds==99 && time.nanoseconds==99);
        if(fault==0) { assert(d->DESC0==status); }
    }
    puts("PASS: ownership, status, identity, length and time-bound faults never expose a valid snapshot");

    Reset(); assert(BSP_PTP_TxEnd(&time)==BSP_PTP_NOT_READY);
    assert(BSP_PTP_TxBegin(frame,70)==BSP_PTP_OK);
    assert(BSP_PTP_TxBegin(frame,70)==BSP_PTP_NOT_READY);
    Complete(0); assert(BSP_PTP_TxEnd(&time)==BSP_PTP_OK);
    assert(BSP_PTP_TxEnd(&time)==BSP_PTP_NOT_READY);
    puts("PASS: only one active TX token exists and each completion is consumed once");

    Reset(); assert(BSP_PTP_TxBegin(frame,70)==BSP_PTP_OK); Complete(0);
    descriptors[0].DESC0|=ETH_DMATXDESC_OWN; untouched=descriptors[0].DESC0;
    assert(BSP_PTP_TxEnd(&time)!=BSP_PTP_OK);
    assert(BSP_PTP_TxClean()!=BSP_PTP_OK && descriptors[0].DESC0==untouched);
    descriptors[0].DESC0&=~ETH_DMATXDESC_OWN;
    assert(BSP_PTP_TxClean()==BSP_PTP_OK && !(descriptors[0].DESC0 & ETH_DMATXDESC_TTSE));
    puts("PASS: failed in-flight captures defer cleanup until DMA has returned ownership");

    Reset(); assert(BSP_PTP_TxBegin(frame,70)==BSP_PTP_OK); Complete(0);
    /* Real HAL_ETH_Transmit timeout overwrites DESC0, clearing OWN and TTSE.
       This is not evidence that DMA completed, even with old DESC6/7 present. */
    descriptors[0].DESC0=ETH_DMATXDESC_FS|ETH_DMATXDESC_LS;
    unsigned int writes_before=register_writes;
    time=(BSP_PtpTime){99,99};
    assert(BSP_PTP_TxEnd(&time)==BSP_PTP_BAD_SNAPSHOT && time.seconds==99 && time.nanoseconds==99);
    assert(register_writes==writes_before);
    assert(BSP_PTP_TxClean()==BSP_PTP_OK && register_writes==writes_before);
    assert(BSP_PTP_TxEnd(&time)==BSP_PTP_NOT_READY);
    current.nanoseconds=100;
    assert(BSP_PTP_TxBegin(frame,70)==BSP_PTP_OK);
    Complete(1); assert(BSP_PTP_TxEnd(&time)==BSP_PTP_OK);
    puts("PASS: HAL timeout status rejects old timestamps without rewriting the cleared descriptor");

    Reset(); assert(BSP_PTP_TxBegin(frame,70)==BSP_PTP_OK); Complete(0);
    clock_status=BSP_PTP_BAD_CONFIG; time=(BSP_PtpTime){99,99};
    assert(BSP_PTP_TxEnd(&time)==BSP_PTP_BAD_CONFIG && time.seconds==99);
    Reset(); assert(BSP_PTP_TxBegin(frame,70)==BSP_PTP_OK); Complete(0);
    assert(BSP_PTP_TxEnd(NULL)==BSP_PTP_BAD_SNAPSHOT);
    assert(!(descriptors[0].DESC0 & ETH_DMATXDESC_TTSE));
    puts("PASS: lost clock configuration and null output cancel captures without stale application data");
    return 0;
}
