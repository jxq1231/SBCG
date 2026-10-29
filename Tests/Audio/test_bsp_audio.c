#include "BSP/bsp_audio.h"
#include "BSP/bsp_audio_codec.h"
#include "Audio/audio_frame_index.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static SAI_Block_TypeDef a,b;
static DMA_Stream_TypeDef dma_stream;
static DMA_HandleTypeDef dma={0,0,0,&dma_stream,HAL_DMA_STATE_READY};
SAI_HandleTypeDef hsai_BlockA2={&a,{2},NULL,HAL_SAI_STATE_READY,0};
SAI_HandleTypeDef hsai_BlockB2={&b,{0},&dma,HAL_SAI_STATE_READY,0};
uint32_t test_primask;
static uint32_t tick_ms,clock_hz=49142857;
static unsigned starts,stops,configs;
static int16_t *buffer;
static HAL_StatusTypeDef start_result=HAL_OK,stop_result=HAL_OK;
static BSP_AudioCodecStatus codec_result=BSP_CODEC_OK;
static bool error_during_start;
static bool cursor_script;
static uint32_t counter_reads[2], cursor_flags;
static unsigned counter_index;
uint32_t TestDmaCounter(DMA_HandleTypeDef *handle)
{
    if (!cursor_script) { return handle->remaining; }
    CHECK(counter_index<2);
    const uint32_t result=counter_reads[counter_index++];
    if (counter_index==2) { handle->flags|=cursor_flags; }
    return result;
}
uint32_t HAL_GetTick(void) {return tick_ms;}
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t which) {CHECK(which==RCC_PERIPHCLK_SAI2);return clock_hz;}
HAL_SAI_StateTypeDef HAL_SAI_GetState(const SAI_HandleTypeDef *h) {return h->State;}
uint32_t HAL_SAI_GetError(const SAI_HandleTypeDef *h) {return h->ErrorCode;}
uint32_t HAL_DMA_GetError(const DMA_HandleTypeDef *h) {return h->ErrorCode;}
HAL_DMA_StateTypeDef HAL_DMA_GetState(const DMA_HandleTypeDef *h) {return h->State;}
BSP_AudioCodecStatus BSP_AudioCodec_ConfigureMic2(BSP_AudioCodecSetupResult *r)
{
    CHECK((a.CR1&1U)!=0 && (b.CR1&1U)==0);++configs;
    *r=(BSP_AudioCodecSetupResult){4,0x210,7};return codec_result;
}
HAL_StatusTypeDef HAL_SAI_Receive_DMA(SAI_HandleTypeDef *h,uint8_t *p,uint16_t size)
{
    CHECK(h==&hsai_BlockB2 && size==32768 && configs>0 && (a.CR1&1U)!=0);
    CHECK(((uintptr_t)p%32U)==0);buffer=(int16_t*)p;++starts;
    if(start_result==HAL_OK){h->State=HAL_SAI_STATE_BUSY_RX;b.CR1|=1U;dma.remaining=size;dma.State=HAL_DMA_STATE_BUSY;dma_stream.CR=DMA_SxCR_EN;}
    if(error_during_start){h->ErrorCode=16;HAL_SAI_ErrorCallback(h);}
    return start_result;
}
HAL_StatusTypeDef HAL_SAI_DMAStop(SAI_HandleTypeDef *h)
{
    CHECK(h==&hsai_BlockB2);++stops;
    b.CR1=0;
    if(h->State==HAL_SAI_STATE_BUSY_RX)
    {
        if(stop_result==HAL_OK){dma.State=HAL_DMA_STATE_READY;dma_stream.CR=0;}
        else {dma.State=HAL_DMA_STATE_TIMEOUT;}
    }
    /* Real HAL sets READY even after its DMA abort failed. */
    h->State=HAL_SAI_STATE_READY;
    return stop_result;
}
static BSP_AudioSnapshot Snapshot(void) {BSP_AudioSnapshot s;BSP_Audio_GetSnapshot(&s);return s;}
static void Half(uint32_t ms) {tick_ms=ms;dma.remaining=16384;dma.flags=0;HAL_SAI_RxHalfCpltCallback(&hsai_BlockB2);}
static void Full(uint32_t ms) {tick_ms=ms;dma.remaining=32768;dma.flags=0;HAL_SAI_RxCpltCallback(&hsai_BlockB2);}
static void Cursor(uint32_t first,uint32_t second,uint32_t flags,bool valid,uint64_t frames)
{
    BSP_AudioCursor output={UINT64_MAX,UINT32_MAX,UINT32_MAX};
    BSP_AudioSnapshot before=Snapshot();
    cursor_script=true;counter_index=0;counter_reads[0]=first;counter_reads[1]=second;
    dma.flags=0;cursor_flags=flags;
    CHECK(BSP_Audio_ReadCursor(&output)==valid);cursor_script=false;
    if(valid){CHECK(output.complete_frames==frames && output.session==before.session && output.boundary_generation==before.callback_count);}
    else{CHECK(output.complete_frames==0 && output.session==0 && output.boundary_generation==0);}
    CHECK(Snapshot().processed_blocks==before.processed_blocks && Snapshot().dropped_blocks==before.dropped_blocks);
    CHECK(dma.flags==flags);dma.flags=0;
}
int main(void)
{
    BSP_AudioBlock block;
    CHECK(BSP_Audio_Stop() && stops==0);
    CHECK(BSP_Audio_Start());CHECK(starts==1 && Snapshot().running);
    CHECK(Snapshot().derived_milli_hz==47991071U);
    const uint32_t first_session=Snapshot().session;
    CHECK(first_session!=0U && Snapshot().observed_frames==0U && Snapshot().index_faults==0U);
    CHECK(!BSP_Audio_Start() && configs==1 && starts==1);
    CHECK(!BSP_Audio_BeginBlock(&block));
    Cursor(32768,32768,0,true,0);
    Cursor(32766,32765,0,true,1);
    Cursor(32768,32769,0,false,0);
    Cursor(0,32768,0,false,0);
    Cursor(32765,32766,0,false,0);
    Cursor(16385,16384,0,false,0);
    Cursor(16384,16384,0,false,0);
    for(uint32_t f=1;f<=16;f<<=1){Cursor(32760,32759,f,false,0);}
    test_primask=1;Cursor(32768,32768,0,true,0);CHECK(test_primask==1);test_primask=0;
    tick_ms=251;Cursor(32768,32768,0,false,0);tick_ms=0;
    dma_stream.CR=0;Cursor(32768,32768,0,false,0);dma_stream.CR=DMA_SxCR_EN;
    b.CR1=0;Cursor(32768,32768,0,false,0);b.CR1=SAI_xCR1_SAIEN;
    dma.State=HAL_DMA_STATE_READY;Cursor(32768,32768,0,false,0);dma.State=HAL_DMA_STATE_BUSY;
    Half(171);CHECK(BSP_Audio_BeginBlock(&block));
    Cursor(16384,16383,0,true,8192);
    Cursor(2,1,0,true,16383);
    CHECK(block.samples==buffer && block.half==0 && block.completed_ms==171);
    CHECK(block.session==first_session && block.first_frame==0U && block.frame_count==8192U);
    CHECK(block.index_valid && block.contiguous && block.gap_frames==0U);
    BSP_AudioBlock old_token=block;
    CHECK(!BSP_Audio_BeginBlock(&block));
    CHECK(BSP_Audio_EndBlock(&block,true));CHECK(!BSP_Audio_EndBlock(&block,true));
    CHECK(!BSP_Audio_BeginBlock(&block));
    Full(342);CHECK(BSP_Audio_BeginBlock(&block));
    Cursor(32766,32764,0,true,16386);
    puts("PASS: read-only cursor rejects races/errors/stale/stopped DMA, floors halfword pairs and preserves PRIMASK");
    CHECK(block.samples==buffer+16384 && block.half==1);
    CHECK(block.first_frame==8192U && block.contiguous && block.gap_frames==0U);
    CHECK(BSP_Audio_EndBlock(&block,true));
    CHECK(Snapshot().half_count==1 && Snapshot().full_count==1 && Snapshot().processed_blocks==2 && Snapshot().dropped_blocks==0);
    puts("PASS: startup ordering, DMA size/alignment, alternating ownership and single completion");
    Half(513);Full(684);Half(855);
    CHECK(BSP_Audio_BeginBlock(&block) && block.half==0);
    CHECK(block.first_frame==32768U && block.gap_frames==16384U && !block.contiguous && block.index_valid);
    CHECK(BSP_Audio_EndBlock(&block,true));
    CHECK(Snapshot().dropped_blocks==2);
    Full(1026);CHECK(BSP_Audio_BeginBlock(&block));Half(1197);
    CHECK(!BSP_Audio_EndBlock(&block,true));CHECK(Snapshot().dropped_blocks==3);
    CHECK(BSP_Audio_BeginBlock(&block));
    CHECK(block.first_frame==49152U && block.gap_frames==8192U && block.index_valid);
    CHECK(BSP_Audio_EndBlock(&block,false)==false);
    CHECK(Snapshot().dropped_blocks==4);
    Full(1368);CHECK(BSP_Audio_BeginBlock(&block));
    dma.flags=1;dma.remaining=16384;
    CHECK(!BSP_Audio_EndBlock(&block,true));CHECK(Snapshot().dropped_blocks==5);
    dma.flags=0;Half(1539);dma.flags=2;
    CHECK(!BSP_Audio_BeginBlock(&block));CHECK(Snapshot().dropped_blocks==6);
    dma.flags=0;Full(1710);dma.remaining=0;
    CHECK(!BSP_Audio_BeginBlock(&block));CHECK(Snapshot().dropped_blocks==7);
    dma.remaining=32768;Half(1881);CHECK(BSP_Audio_BeginBlock(&block));
    CHECK(block.first_frame==81920U && block.gap_frames==40960U && !block.contiguous);
    CHECK(BSP_Audio_EndBlock(&block,true));
    puts("PASS: late consumer, overwrite during compute, pending IRQ boundary, reload edge and recovery");
    test_primask=1;(void)Snapshot();CHECK(test_primask==1);test_primask=0;
    unsigned before=Snapshot().callback_count;
    HAL_SAI_RxHalfCpltCallback(&hsai_BlockA2);HAL_SAI_RxCpltCallback(NULL);
    CHECK(Snapshot().callback_count==before);
    Half(2000);CHECK(Snapshot().error_count==1);
    CHECK((Snapshot().index_faults&AUDIO_INDEX_ORDER)!=0U);
    Cursor(16384,16384,0,false,0);
    CHECK(BSP_Audio_BeginBlock(&block) && !block.index_valid && !block.contiguous);
    CHECK(BSP_Audio_EndBlock(&block,true));
    hsai_BlockB2.ErrorCode=16;dma.ErrorCode=8;HAL_SAI_ErrorCallback(&hsai_BlockB2);
    CHECK(!Snapshot().running && Snapshot().hal_error==16 && Snapshot().dma_error==8);
    CHECK((Snapshot().index_faults&AUDIO_INDEX_HAL_ERROR)!=0U);
    CHECK(!BSP_Audio_BeginBlock(&block));
    before=Snapshot().callback_count;Full(2100);CHECK(Snapshot().callback_count==before);
    CHECK(BSP_Audio_Stop() && (a.CR1&1U)==0 && stops==1);
    Cursor(32768,32768,0,false,0);CHECK(!BSP_Audio_ReadCursor(NULL));
    CHECK(BSP_Audio_Stop() && stops==1);
    puts("PASS: IRQ filtering, callback order fault, error snapshot, stop and PRIMASK preservation");
    hsai_BlockB2.ErrorCode=dma.ErrorCode=0;codec_result=BSP_CODEC_IO_ERROR;
    CHECK(!BSP_Audio_Start() && Snapshot().start_error==2 && starts==1 && a.CR1==0);
    CHECK(Snapshot().codec_register==0x210 && Snapshot().codec_writes==7 && Snapshot().codec_hal_error==4);
    codec_result=BSP_CODEC_OK;clock_hz=0;
    CHECK(!BSP_Audio_Start() && Snapshot().start_error==4 && a.CR1==0);
    clock_hz=49142857;start_result=HAL_ERROR;
    CHECK(!BSP_Audio_Start() && Snapshot().start_error==3 && a.CR1==0);
    start_result=HAL_OK;error_during_start=true;
    CHECK(!BSP_Audio_Start() && !Snapshot().running && a.CR1==0);
    error_during_start=false;CHECK(BSP_Audio_Stop());
    CHECK(BSP_Audio_Start());Half(2200);
    CHECK(BSP_Audio_BeginBlock(&block) && block.session>first_session && block.first_frame==0U);
    /* All fields collide except the boot-local session: reject stale identity. */
    old_token=block;old_token.session=first_session;
    CHECK(!BSP_Audio_EndBlock(&old_token,true));
    CHECK(BSP_Audio_EndBlock(&block,true));
    Full(2451);CHECK(BSP_Audio_BeginBlock(&block) && !block.index_valid);
    CHECK((Snapshot().index_faults&AUDIO_INDEX_LATE)!=0U);
    CHECK(BSP_Audio_EndBlock(&block,true));Half(2622);
    CHECK(BSP_Audio_BeginBlock(&block) && !block.index_valid);
    CHECK(BSP_Audio_EndBlock(&block,true));CHECK(BSP_Audio_Stop());
    tick_ms=UINT32_MAX-181U;
    CHECK(BSP_Audio_Start());Half(UINT32_MAX-10U);Full(160);
    CHECK(Snapshot().observed_frames==16384U && Snapshot().index_faults==0U);
    CHECK(Snapshot().first_callback_ms==UINT32_MAX-10U && Snapshot().last_callback_ms==160);
    stop_result=HAL_ERROR;CHECK(!BSP_Audio_Stop());CHECK(Snapshot().start_error==5);
    unsigned old_configs=configs;CHECK(!BSP_Audio_Start() && configs==old_configs);
    stop_result=HAL_OK;CHECK(!BSP_Audio_Stop());
    CHECK(!BSP_Audio_Start() && configs==old_configs && dma_stream.CR==DMA_SxCR_EN);
    dma_stream.CR=0;dma.State=HAL_DMA_STATE_READY;CHECK(BSP_Audio_Stop());
    dma.State=HAL_DMA_STATE_BUSY;
    CHECK(!BSP_Audio_Start() && configs==old_configs);
    dma.State=HAL_DMA_STATE_READY;
    CHECK(!BSP_Audio_BeginBlock(NULL) && !BSP_Audio_EndBlock(NULL,true));BSP_Audio_GetSnapshot(NULL);
    puts("PASS: codec/clock/DMA/start-callback failures, tick wrap, failed-stop ownership and null inputs");
    return 0;
}
