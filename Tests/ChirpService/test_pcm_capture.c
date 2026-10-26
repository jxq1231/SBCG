/**
 * @file        test_pcm_capture.c
 * @brief       用真实服务验证候选附近End验证PCM切片的交接、定界裁剪与右尾补充。
 * @details     门控/Stage夹具经真实Complete取得CPU副本；断言切片内容、裁剪标志、补尾与计数，
 *              并验证注册sink不改变搜索/匹配结果。
 * @note        PCM及日志由测试独占，无硬件访问；本测试不调用UDP发送模块。
 */
#define main unused_service_main
#include "test_service.c"
#undef main
#include "../../App/Detector/chirp_service.c"

static unsigned handoffs,handoff_windows;
static ChirpCaptureSlice last_slice;
static int16_t capture_copy[CHIRP_CAPTURE_FRAMES];
#ifndef PCM_CAPTURE_RED
/** @brief 复制本次切片的元数据与内容，不保存samples指针，无返回值。 */
static void CaptureSink(const ChirpCaptureSlice *slice)
{
 ++handoffs;last_slice=*slice;
 CHECK(slice->samples!=NULL && slice->frames!=0U && slice->frames<=CHIRP_CAPTURE_FRAMES);
 CHECK(slice->session!=0U && slice->id!=0U);
 handoff_windows+=(unsigned)slice->frames;
 memcpy(capture_copy,slice->samples,slice->frames*sizeof capture_copy[0]);
}
/** @brief 初始化真实服务并注册测试sink，清事件/交接计数，无返回值。 */
static void ResetSink(void)
{
 events=lines=handoffs=handoff_windows=0U;last_slice=(ChirpCaptureSlice){0};
 CHECK(ChirpService_Init(tick_ms,0U,Log));
 CHECK(strstr(last_any,",DIAG=3,")!=NULL);
 ChirpService_SetCaptureSink(CaptureSink);
}
/** @brief 只初始化真实服务，不注册sink，清事件计数，无返回值。 */
static void ResetNoSink(void)
{
 events=lines=0U;signal_kind=0U;input_channel=0U;
 CHECK(ChirpService_Init(tick_ms,0U,Log));
 CHECK(strstr(last_any,",DIAG=3,")!=NULL);
}
/** @brief 将测试独占mono的8192点交错提交到指定first，真实End提交；不保留PCM地址，无返回值。 */
static void StageMono(uint64_t first,const int16_t *mono)
{
 memset(pcm,0,sizeof pcm);for(unsigned i=0U;i<8192U;++i)pcm[2U*i]=mono[i];
 const BSP_AudioBlock block={.samples=pcm,.session=1U,.frame_count=8192U,.first_frame=first,
  .completed_ms=tick_ms,.index_valid=true,.contiguous=true};
 borrowed=true;CHECK(ChirpService_Stage(tick_ms,&block));borrowed=false;CHECK(ChirpService_Complete(true));
 memset(pcm,0,sizeof pcm);
}
#endif
/** @brief 执行候选PCM捕获合同；成功返回0，失败由CHECK终止。 */
int main(void)
{
 CHECK(ChirpService_Init(0U,0U,Log));
#ifndef PCM_CAPTURE_RED
 ChirpServiceSnapshot s;
 /* 未注册sink：完整任务仍照常完成，只统计capture_miss，不复制PCM。 */
 {ResetNoSink();Block(0U,2000U,true);Work(80U);ChirpService_GetSnapshot(&s);
  CHECK(events==1U && s.evaluations==257U && s.capture_ok==0U && s.capture_miss==1U);}
 /* 单一块、候选贴近流起点：候选早于正规左扩展则左不裁剪，右端按可用范围裁剪。
    切片内容与模板段逐点一致，匹配结果与手off流程不受sink影响。 */
 {static int16_t mono[8192];ResetSink();
  memset(mono,0,sizeof mono);for(unsigned i=0U;i<2400U;++i)mono[2000U+i]=ChirpTemplate_PCM[i];
  StageMono(0U,mono);Work(80U);ChirpService_GetSnapshot(&s);
  CHECK(events==1U && handoffs==1U && s.capture_ok==1U && s.capture_miss==0U);
  CHECK(s.capture_clamped==1U && s.capture_tail==0U);
  CHECK(last_slice.first_frame==0U && last_slice.raw_first_frame==2000U);
  CHECK(last_slice.hint_frame>=1900U && last_slice.hint_frame<=2000U);
  CHECK(last_slice.id==1U && last_slice.session==1U && last_slice.frames==8192U);
  CHECK(!last_slice.left_clamped && last_slice.right_clamped && !last_slice.tail_topup);
  CHECK(last_slice.hint_frame+CHIRP_CAPTURE_LEFT+CHIRP_TEMPLATE_FRAMES+CHIRP_CAPTURE_RIGHT>8192U);
  for(unsigned k=0U;k<8192U;++k){
   const int16_t expect=(k>=2000U && k<4400U) ? ChirpTemplate_PCM[k-2000U]:0;
   CHECK(capture_copy[k]==expect);
  }}
 /* 两个候选间隔超过去重窗口连续完成，每次独立交接且id递增，sink不改变搜索/匹配预算。 */
 {static int16_t mono[8192];ResetSink();
  for(unsigned i=0U;i<2400U;++i)mono[2000U+i]=ChirpTemplate_PCM[i];
  StageMono(0U,mono);Work(80U);
  memset(mono,0,sizeof mono);StageMono(8192U,mono);StageMono(16384U,mono);
  for(unsigned i=0U;i<2400U;++i)mono[2000U+i]=ChirpTemplate_PCM[i];
  StageMono(24576U,mono);Work(80U);ChirpService_GetSnapshot(&s);
  CHECK(handoffs==2U && s.capture_ok==2U && s.completed_jobs==2U && s.capture_miss==0U);
  CHECK(s.evaluations==257U*2U && last_slice.id==2U);}
 /* 候选贴块尾：缓存时右端未到，完成时补尾到期望右端并清右裁剪标志。 */
 {static int16_t mono[8192],mono2[8192];ResetSink();
  memset(mono,0,sizeof mono);for(unsigned i=0U;i<2400U;++i)mono[2000U+i]=ChirpTemplate_PCM[i];
  StageMono(0U,mono);Work(7U);
  memset(mono2,0,sizeof mono2);mono2[100]=1234;
  StageMono(8192U,mono2);Work(60U);ChirpService_GetSnapshot(&s);
  CHECK(handoffs==1U && s.capture_tail==1U);
  CHECK(last_slice.first_frame==0U && last_slice.frames==
        last_slice.hint_frame+CHIRP_CAPTURE_LEFT+CHIRP_TEMPLATE_FRAMES+CHIRP_CAPTURE_RIGHT);
  CHECK(!last_slice.right_clamped && last_slice.tail_topup && !last_slice.left_clamped);
  CHECK(capture_copy[2000U]==ChirpTemplate_PCM[0] &&
        capture_copy[4399U]==ChirpTemplate_PCM[2399U]);
  CHECK(capture_copy[8292]==1234);}
 /* 流起点后首个可用块起点即截左：候选临近块头时左端被裁剪并置位。 */
 {static int16_t mono[8192];ResetSink();Block(0U,2000U,false);
  memset(mono,0,sizeof mono);for(unsigned i=0U;i<2400U;++i)mono[1008U+i]=ChirpTemplate_PCM[i];
  StageMono(8192U,mono);Work(80U);ChirpService_GetSnapshot(&s);
  CHECK(handoffs==1U && s.capture_clamped==1U);
  CHECK(last_slice.first_frame==8192U && last_slice.left_clamped && !last_slice.right_clamped);
  CHECK(last_slice.frames==last_slice.hint_frame+CHIRP_CAPTURE_RIGHT+
        CHIRP_CAPTURE_LEFT+CHIRP_TEMPLATE_FRAMES-last_slice.first_frame);
  CHECK(capture_copy[1008U]==ChirpTemplate_PCM[0] &&
        capture_copy[3407U]==ChirpTemplate_PCM[2399U]);}
 /* End失败清历史后捕获上下文失效，新任务仍重新缓存并交接。 */
 {ResetSink();Block(0U,2000U,true);Work(80U);Block(0U,2000U,false);
  ChirpService_GetSnapshot(&s);CHECK(handoffs==1U && s.rejected_blocks==1U);
  Block(0U,2000U,true);Work(80U);ChirpService_GetSnapshot(&s);
  CHECK(s.capture_ok==2U && handoffs==2U);}
#endif
 printf("PASS pcm capture: handoffs=%u frames=%u, clamps/topup/miss counters consistent\n",
   handoffs,handoff_windows);
 return 0;
}