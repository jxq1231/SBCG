/**
 * @file        test_search_diagnostics.c
 * @brief       使用真实服务验证完整搜索的原始峰、分类计数和有界日志交接。
 * @details     门控用例经Stage/Complete取得CPU副本；边界夹具仅控制私有标量任务，不绕过评分。
 * @note        PCM及日志由测试独占，无硬件访问；主机调用次数不代表板端耗时。
 */
#define main unused_service_main
#include "test_service.c"
#undef main
#include "../../App/Detector/chirp_service.c"
static unsigned reports;
#ifndef SEARCH_DIAGNOSTICS_RED
static unsigned process_calls;
#endif
static char search_line[256];
/** @brief 借用本次日志并保留搜索行；回调结束后不保存text指针，无返回值。 */
static void SearchLog(const char *text,int length)
{
 Log(text,length);
 if(strncmp(text,"$CHIRP_SEARCH,",14)==0){++reports;memcpy(search_line,text,(size_t)length);search_line[length]=0;}
}
#ifndef SEARCH_DIAGNOSTICS_RED
/** @brief 读取line中给定非NULL键的无符号值；缺键或NA立即失败，返回64位数。 */
static uint64_t Field(const char *line,const char *key)
{const char *p=strstr(line,key);CHECK(p!=NULL);p+=strlen(key);CHECK(*p>='0' && *p<='9');return strtoull(p,NULL,10);}
/** @brief 初始化单声道测试并清回调计数，无参数，无返回值。 */
static void Reset(void)
{
 events=lines=reports=process_calls=0U;search_line[0]=0;signal_kind=0U;input_channel=0U;
 CHECK(ChirpService_Init(tick_ms,0U,SearchLog));CHECK(strstr(last_any,",DIAG=3,")!=NULL);
}
/** @brief 处理一次真实服务，校验每轮最多一行和8窗评分，无参数，无返回值。 */
static void Step(void)
{
 const unsigned before=lines;ChirpServiceSnapshot old,s;ChirpService_GetSnapshot(&old);
 BSP_AudioSnapshot snap={.running=true,.session=1U,.callback_count=1U,.last_callback_ms=tick_ms};
 ChirpService_Process(tick_ms,&snap);++tick_ms;++process_calls;ChirpService_GetSnapshot(&s);
 CHECK(lines-before<=1U && s.last_scores<=8U && s.evaluations-old.evaluations==s.last_scores);
 CHECK(s.last_scanned<=8192U);
}
/** @brief 完成当前任务，最多100次前台调用；无参数，无返回值。 */
static void Finish(void)
{ChirpServiceSnapshot s;unsigned n=0U;do{Step();ChirpService_GetSnapshot(&s);CHECK(++n<=100U);}while(s.completed_jobs==0U);}
/** @brief 核对搜索行的统计恒等式，line仅本次借用，无返回值。 */
static void Counts(const char *line)
{CHECK(Field(line,",N=")==Field(line,",V=")+Field(line,",Z=")+Field(line,",I="));CHECK(Field(line,",LR=")<=Field(line,",V="));}
/** @brief 用独立双精度遍历复算音调/反向模板的所有窗口；kind为1音调或2反向，无返回值。 */
static void IndependentPeak(unsigned kind)
{
 static const int16_t tone[16]={0,3827,7071,9239,10000,9239,7071,3827,0,-3827,-7071,-9239,-10000,-9239,-7071,-3827};
 const uint64_t lo=Field(search_line,",LO="),hi=Field(search_line,",HI=");
 double best=-1.0;uint64_t best_frame=0U;unsigned valid=0U;uint16_t best_rms=0U;bool inverted=false;
 for(uint64_t first=lo;first<=hi;++first){
  double sx=0.0,st=0.0,xx=0.0,tt=0.0,xt=0.0;
  for(unsigned i=0U;i<2400U;++i){
   const uint64_t frame=first+i;double x=0.0;
   if(frame>=2000U && frame<4400U){const unsigned at=(unsigned)(frame-2000U);x=kind==1U ? tone[at%16U]:ChirpTemplate_PCM[2399U-at];}
   const double t=ChirpTemplate_PCM[i];sx+=x;st+=t;xx+=x*x;tt+=t*t;xt+=x*t;
  }
  const double vx=2400.0*xx-sx*sx,vt=2400.0*tt-st*st;
  if(vx>0.0){++valid;const double numerator=2400.0*xt-sx*st;double corr=fabs(numerator)/sqrt(vx*vt);if(corr>1.0)corr=1.0;
   if(corr>best){best=corr;best_frame=first;best_rms=(uint16_t)sqrt(vx/(2400.0*2400.0));inverted=numerator<0.0;}}
 }
 CHECK(Field(search_line,",BEST=")==best_frame);
 CHECK(llabs((long long)Field(search_line,",CORR_PPM=")-(long long)(best*1000000.0+0.5))<=1);
 CHECK(Field(search_line,",RMS=")==best_rms && Field(search_line,",INV=")==(inverted ? 1U:0U));
 CHECK(Field(search_line,",V=")==valid);Counts(search_line);
}
/** @brief 为边界评分构造已验证副本上的一个任务；首尾为窗口起点，id来自候选计数，无返回值。 */
static void Seed(uint64_t first,uint64_t last,uint64_t hint)
{
 scan_offset=BSP_AUDIO_HALF_FRAMES;head=0U;count=1U;job_cached=false;
 jobs[0]=(SearchJob){.first=first,.next=first,.last=last,.hint=hint,.id=state.candidates,
  .completed_ms=tick_ms};
}
/** @brief 将测试独占mono的8192点交错提交；不保留PCM地址，无返回值。 */
static void StageMono(const int16_t *mono)
{
 memset(pcm,0,sizeof pcm);for(unsigned i=0U;i<8192U;++i)pcm[2U*i]=mono[i];
 const BSP_AudioBlock block={.samples=pcm,.session=1U,.frame_count=8192U,.first_frame=0U,
  .completed_ms=tick_ms,.index_valid=true,.contiguous=true};
 borrowed=true;CHECK(ChirpService_Stage(tick_ms,&block));borrowed=false;CHECK(ChirpService_Complete(true));memset(pcm,0,sizeof pcm);
}
#endif
/** @brief 执行新增搜索诊断合同；成功返回0，失败由CHECK终止。 */
int main(void)
{
 CHECK(ChirpService_Init(0U,0U,SearchLog));CHECK(strstr(last_any,",DIAG=3,")!=NULL);
#ifndef SEARCH_DIAGNOSTICS_RED
 ChirpServiceSnapshot s;
 Reset();Block(0U,2000U,true);Finish();CHECK(events==1U && reports==0U);
 CHECK(strstr(last_event,",FIRST=2000,")!=NULL && report_pending);
 ChirpService_GetSnapshot(&s);CHECK(s.evaluations==257U && s.last_scores==1U && s.report_emitted==0U);
 /* 已有待发报告时仍评分下一任务，日志延迟不能占用8窗预算。 */
 Seed(1800U,2056U,1992U);Step();CHECK(reports==1U && state.last_scores==8U);
 CHECK(Field(search_line,",ID=")==1U && Field(search_line,",SESSION=")==1U);
 CHECK(Field(search_line,",HINT=")==1984U && Field(search_line,",LO=")==1792U && Field(search_line,",HI=")==2048U);
 CHECK(Field(search_line,",BEST=")==2000U && Field(search_line,",MATCH=")==1U && strstr(search_line,",WHY=MATCH,")!=NULL);
 CHECK(Field(search_line,",EDGE=")==0U && Field(search_line,",N=")==257U);Counts(search_line);
 ChirpService_GetSnapshot(&s);CHECK(s.report_emitted==1U && s.report_dropped==0U);
 for(unsigned kind=1U;kind<=2U;++kind){Reset();signal_kind=kind;Block(0U,2000U,true);Finish();
  CHECK(events==0U && reports==1U && strstr(search_line,",MATCH=0,WHY=LOW_CORR,")!=NULL);IndependentPeak(kind);}
 /* 两个短强脉冲满足64点能量门控，但2400点窗口去直流RMS低于64。 */
 static int16_t mono[8192];Reset();memset(mono,0,sizeof mono);mono[2000]=mono[2064]=1200;
 StageMono(mono);Finish();CHECK(events==0U && reports==1U && strstr(search_line,",WHY=LOW_RMS,")!=NULL);
 CHECK(Field(search_line,",LR=")==Field(search_line,",V=") && Field(search_line,",V=")==257U);Counts(search_line);
 /* 原始峰和有效匹配独立：低幅精确模板先到，与后续高幅窗同分时保留首峰。 */
 int16_t short_template[16];Reset();memset(mono,0,sizeof mono);
 for(unsigned i=0U;i<16U;++i){short_template[i]=(i%2U)==0U ? 1000:-1000;
  mono[i]=(i%2U)==0U ? 1:-1;mono[32U+i]=short_template[i];}
 StageMono(mono);CHECK(ChirpWindow_Init(&reference,short_template,16U));Seed(0U,64U,0U);Finish();
 CHECK(events==1U && reports==0U);Step();
 CHECK(Field(search_line,",BEST=")==0U && Field(search_line,",CORR_PPM=")==1000000U);
 CHECK(Field(search_line,",RMS=")==1U && strstr(search_line,",MATCH=1,WHY=LOW_RMS,")!=NULL);Counts(search_line);
 /* 恰好零相关仍是有效首峰；不能用相关度0或帧0作为无峰哨兵。 */
 Reset();for(unsigned i=0U;i<8192U;++i)mono[i]=(i%4U)<2U ? 1000:-1000;
 StageMono(mono);CHECK(ChirpWindow_Init(&reference,short_template,16U));Seed(0U,64U,0U);Finish();
 CHECK(reports==1U && Field(search_line,",BEST=")==0U && Field(search_line,",CORR_PPM=")==0U);
 CHECK(Field(search_line,",V=")==65U && strstr(search_line,",WHY=LOW_CORR,")!=NULL);Counts(search_line);
 /* 门控不能从全零输入产生任务；私有标量夹具覆盖合法零方差与非法参考路径。 */
 for(unsigned invalid=0U;invalid<2U;++invalid){Reset();Block(0U,UINT64_MAX,true);Seed(0U,64U,0U);
  if(invalid!=0U)reference.frames=0U;
  Finish();CHECK(reports==1U && strstr(search_line,",BEST=NA,CORR_PPM=NA,RMS=NA,CLIP=NA,INV=NA,EDGE=NA,MATCH=0,WHY=NO_VALID,")!=NULL);
  CHECK(Field(search_line,",N=")==65U && Field(search_line,",V=")==0U);
  CHECK(Field(search_line,invalid!=0U ? ",I=":",Z=")==65U);Counts(search_line);}
 /* 首峰、尾峰、帧0和大于32位的帧号由同一真实窗口评分路径产生。 */
 for(unsigned edge=0U;edge<4U;++edge){Reset();const uint64_t origin=edge==3U ? UINT64_C(5000000000):0U;
  const uint64_t onset=origin+(edge==2U ? 0U:2000U);Block(origin,onset,true);
  const uint64_t lo=edge==1U ? onset-64U:onset;Seed(lo,lo+64U,onset);Finish();
  CHECK(events==1U && reports==0U);Step();CHECK(reports==1U && Field(search_line,",BEST=")==onset);
  CHECK(Field(search_line,",EDGE=")==(edge==1U ? 2U:1U));Counts(search_line);}
 /* 失败End、换会话或流超时先使待发报告失效，不将旧搜索诊断交给新流。 */
 for(unsigned reason=0U;reason<3U;++reason){Reset();Block(0U,2000U,true);Finish();CHECK(report_pending);
  const unsigned before=reports;
  if(reason==0U)Block(8192U,UINT64_MAX,false);
  else{BSP_AudioSnapshot snap={.running=true,.session=reason==1U ? 2U:1U,.callback_count=1U,
   .last_callback_ms=reason==2U ? tick_ms-1000U:tick_ms};ChirpService_Process(tick_ms,&snap);}
  ChirpService_GetSnapshot(&s);CHECK(!report_pending && s.report_dropped==1U && s.report_emitted==0U);
  Step();CHECK(reports==before && state.report_dropped==1U);}
 /* 仅防御覆盖丢失诊断；任务的丢弃计数不受影响。 */
 Reset();Block(0U,2000U,true);Finish();const uint32_t drops=state.dropped;
 StoreReport(&(SearchJob){.first=0U,.last=64U,.hint=0U,.id=2U});
 CHECK(state.report_dropped==1U && state.dropped==drops);Step();CHECK(reports==1U);
 /* 饱和候选ID不是永久唯一号；格式按整数上界证明仍可交给256字节队列。 */
 Reset();state.candidates=UINT32_MAX;Block(0U,2000U,true);Finish();Step();CHECK(Field(search_line,",ID=")==UINT32_MAX);
 pending_report=(SearchReport){.id=UINT32_MAX,.session=UINT32_MAX,.hint=UINT64_MAX,.first=UINT64_MAX,
  .last=UINT64_MAX,.raw_first=UINT64_MAX,.raw_best={.status=CHIRP_WINDOW_LOW_CORR,.correlation=1.0,
  .correlation_valid=true,.rms_pcm=UINT16_MAX,.clipped=true,.inverted=true},.have_raw=true,.matched=true,
  .total=257U,.valid=257U,.low_rms=257U,.zero=257U,.invalid=257U};
 report_pending=true;EmitReport();CHECK(strlen(search_line)==247U && strlen(search_line)<256U);
 state.max_scan_ms=state.max_score_ms=state.max_process_gap_ms=state.drop_expired=state.drop_history=
  state.drop_full=state.drop_range=state.drop_reset=state.completed_jobs=state.max_complete_ms=
  state.max_drop_ms=state.report_emitted=state.report_dropped=UINT32_MAX;
 have_copy=false;diagnostic_next=true;last_state_ms=tick_ms-1000U;Step();
 CHECK(strncmp(last_any,"$CHIRP_DIAG,",12)==0 && strlen(last_any)==252U);
 CHECK(strstr(last_any,",RPT=4294967295,RLOST=4294967295\r\n")!=NULL);
 printf("PASS search diagnostics: raw peaks, low RMS, NA classifications, boundaries, report priority/loss, budget; SEARCH_MAX=%zu DIAG_MAX=%zu\n",strlen(search_line),strlen(last_any));
#endif
 return 0;
}

