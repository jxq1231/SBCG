/**
 * @file        test_window_diagnostics.c
 * @brief       验证单窗口诊断分类、原始相关度与原匹配接口的兼容性。
 * @details     使用冻结模板、正交短窗和PCM16极值，覆盖失败清零及帧号边界。
 * @note        全部PCM由测试独占；只验证纯C运算，不访问硬件或证明实物识别率。
 */
#include "Detector/chirp_window.h"
#include "Detector/chirp_template.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief 条件仅求值一次；失败打印位置并终止测试，无返回值。 */
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)

/**
 * @brief 逐字段确认失败输出已清零，不依赖结构体填充字节。
 * @param out 非NULL只读测试输出，仅调用期间借用。
 * @return 无返回值。
 */
static void CheckEmpty(const ChirpMatch *out)
{
    CHECK(out->session==0U && out->first_frame==0U && out->last_frame==0U);
    CHECK(out->confirmed_frame==0U && out->correlation==0.0f && out->rms_pcm==0U);
    CHECK(!out->inverted && !out->clipped);
}

/**
 * @brief 确认非法输入将独立诊断输出清零并标记INVALID。
 * @param diag 非NULL只读输出，仅调用期间借用。
 * @return 无返回值。
 */
static void CheckInvalid(const ChirpWindowDiagnostic *diag)
{
    CHECK(diag->status==CHIRP_WINDOW_INVALID && diag->correlation==0.0);
    CHECK(!diag->correlation_valid && !diag->inverted && !diag->clipped);
    CHECK(diag->rms_pcm==0U);
}

/**
 * @brief 对相同输入调用新旧接口，逐字段核对匹配输出。
 * @param ref 非NULL已初始化模板，样本借用至本函数返回。
 * @param pcm 非NULL窗口，至少ref.frames点，不与输出重叠。
 * @param first 窗口绝对帧起点，允许0及末帧恰好UINT64_MAX。
 * @param config 非NULL合法门限，仅调用期间借用。
 * @return 新接口诊断值的副本；输入与局部输出均不逃逸。
 */
static ChirpWindowDiagnostic Compare(const ChirpWindowTemplate *ref,const int16_t *pcm,
                                   uint64_t first,const ChirpMatcherConfig *config)
{
    ChirpMatch old_out,new_out,null_diag_out;
    ChirpWindowDiagnostic diag;
    memset(&old_out,0xa5,sizeof old_out);memset(&new_out,0x5a,sizeof new_out);
    memset(&null_diag_out,0xff,sizeof null_diag_out);memset(&diag,0xa5,sizeof diag);
    const bool old_match=ChirpWindow_Score(ref,pcm,UINT32_MAX,first,config,&old_out);
    const bool new_match=ChirpWindow_ScoreWithDiagnostic(ref,pcm,UINT32_MAX,first,config,&new_out,&diag);
    CHECK(old_match==new_match);
    CHECK(ChirpWindow_ScoreWithDiagnostic(ref,pcm,UINT32_MAX,first,config,&null_diag_out,NULL)==new_match);
    if(new_match){
        CHECK(diag.status==CHIRP_WINDOW_MATCH && diag.correlation_valid);
        CHECK(old_out.session==new_out.session && old_out.session==null_diag_out.session);
        CHECK(old_out.first_frame==new_out.first_frame && old_out.first_frame==null_diag_out.first_frame);
        CHECK(old_out.last_frame==new_out.last_frame && old_out.last_frame==null_diag_out.last_frame);
        CHECK(old_out.confirmed_frame==0U && new_out.confirmed_frame==0U && null_diag_out.confirmed_frame==0U);
        CHECK(old_out.correlation==new_out.correlation && old_out.correlation==null_diag_out.correlation);
        CHECK(old_out.rms_pcm==new_out.rms_pcm && old_out.rms_pcm==null_diag_out.rms_pcm);
        CHECK(old_out.inverted==new_out.inverted && old_out.inverted==null_diag_out.inverted);
        CHECK(old_out.clipped==new_out.clipped && old_out.clipped==null_diag_out.clipped);
        CHECK(new_out.first_frame==first && new_out.last_frame==first+ref->frames-1U);
        CHECK(new_out.correlation==(float)diag.correlation && new_out.rms_pcm==diag.rms_pcm);
        CHECK(new_out.inverted==diag.inverted && new_out.clipped==diag.clipped);
    }else{
        CheckEmpty(&old_out);CheckEmpty(&new_out);CheckEmpty(&null_diag_out);
    }
    return diag;
}

/**
 * @brief 验证一组非法参数下两个输出的独立失败语义。
 * @param ref 可为NULL或无效模板，只读借用。
 * @param pcm 可为NULL；只有其余参数有效时才允许读取。
 * @param session 会话，0用于验证拒绝。
 * @param first 绝对窗口起点，允许构造末帧溢出。
 * @param config 可为NULL或无效配置，只读借用。
 * @return 无返回值。
 */
static void Invalid(const ChirpWindowTemplate *ref,const int16_t *pcm,uint32_t session,
                    uint64_t first,const ChirpMatcherConfig *config)
{
    ChirpMatch out;ChirpWindowDiagnostic diag;
    memset(&out,0xa5,sizeof out);memset(&diag,0xa5,sizeof diag);
    CHECK(!ChirpWindow_ScoreWithDiagnostic(ref,pcm,session,first,config,&out,&diag));
    CheckEmpty(&out);CheckInvalid(&diag);
    memset(&out,0xa5,sizeof out);
    CHECK(!ChirpWindow_Score(ref,pcm,session,first,config,&out));CheckEmpty(&out);
}

/**
 * @brief 执行诊断和兼容性用例，成功返回0。
 * @return 0表示所有断言通过；失败由CHECK终止为非零。
 * @note 测试栈和静态样本独占，不触发DMA、IRQ或应用日志。
 */
int main(void)
{
    ChirpWindowTemplate ref;
    const ChirpMatcherConfig *config=&ChirpMatcher_DefaultConfig;
    CHECK(ChirpWindow_Init(&ref,ChirpTemplate_PCM,CHIRP_TEMPLATE_FRAMES));
    ChirpWindowDiagnostic diag=Compare(&ref,ChirpTemplate_PCM,0U,config);
    CHECK(diag.correlation>0.999999 && !diag.inverted && !diag.clipped && diag.rms_pcm>=64U);
    diag=Compare(&ref,ChirpTemplate_PCM,UINT64_C(5000000000),config);
    CHECK(diag.status==CHIRP_WINDOW_MATCH);
    diag=Compare(&ref,ChirpTemplate_PCM,UINT64_MAX-(CHIRP_TEMPLATE_FRAMES-1U),config);
    CHECK(diag.status==CHIRP_WINDOW_MATCH);

    static int16_t pcm[CHIRP_TEMPLATE_FRAMES];
    for(uint16_t i=0U;i<CHIRP_TEMPLATE_FRAMES;++i)pcm[i]=(int16_t)-ChirpTemplate_PCM[i];
    diag=Compare(&ref,pcm,0U,config);
    CHECK(diag.status==CHIRP_WINDOW_MATCH && diag.inverted && diag.correlation>0.999999);
    for(uint16_t i=0U;i<CHIRP_TEMPLATE_FRAMES;++i)pcm[i]=ChirpTemplate_PCM[CHIRP_TEMPLATE_FRAMES-1U-i];
    diag=Compare(&ref,pcm,0U,config);
    CHECK(diag.status==CHIRP_WINDOW_LOW_CORR && diag.correlation_valid && diag.correlation<0.8);

    int16_t alternating[16],orthogonal[16],tiny[16];
    for(uint16_t i=0U;i<16U;++i){
        alternating[i]=(i&1U)==0U ? 1000:-1000;
        orthogonal[i]=(i&2U)==0U ? 1000:-1000;
        tiny[i]=(i&1U)==0U ? 1:-1;
    }
    CHECK(ChirpWindow_Init(&ref,alternating,16U));
    diag=Compare(&ref,tiny,0U,config);
    CHECK(diag.status==CHIRP_WINDOW_LOW_RMS && diag.correlation_valid);
    CHECK(diag.correlation==1.0 && diag.rms_pcm==1U && !diag.inverted && !diag.clipped);
    diag=Compare(&ref,orthogonal,0U,config);
    CHECK(diag.status==CHIRP_WINDOW_LOW_CORR && diag.correlation_valid && diag.correlation==0.0);
    CHECK(diag.rms_pcm==1000U && !diag.inverted);

    /* 精确等于RMS门限的窗口保留原来的“>=”规则。 */
    for(uint16_t i=0U;i<16U;++i)tiny[i]=(i&1U)==0U ? 64:-64;
    diag=Compare(&ref,tiny,0U,config);CHECK(diag.status==CHIRP_WINDOW_MATCH && diag.rms_pcm==64U);
    ChirpMatcherConfig threshold=*config;threshold.min_rms_pcm=65U;
    diag=Compare(&ref,tiny,0U,&threshold);CHECK(diag.status==CHIRP_WINDOW_LOW_RMS);
    threshold=*config;threshold.min_correlation=1.0f;
    diag=Compare(&ref,alternating,0U,&threshold);CHECK(diag.status==CHIRP_WINDOW_MATCH);

    memset(pcm,0,sizeof pcm);diag=Compare(&ref,pcm,0U,config);
    CHECK(diag.status==CHIRP_WINDOW_ZERO_VARIANCE && !diag.correlation_valid);
    CHECK(diag.correlation==0.0 && diag.rms_pcm==0U && !diag.inverted && !diag.clipped);
    for(uint16_t i=0U;i<16U;++i)pcm[i]=INT16_MIN;
    diag=Compare(&ref,pcm,0U,config);
    CHECK(diag.status==CHIRP_WINDOW_ZERO_VARIANCE && !diag.correlation_valid && diag.clipped);

    for(uint16_t i=0U;i<CHIRP_TEMPLATE_FRAMES;++i)pcm[i]=(i&1U)==0U ? INT16_MIN:INT16_MAX;
    CHECK(ChirpWindow_Init(&ref,pcm,CHIRP_TEMPLATE_FRAMES));
    diag=Compare(&ref,pcm,UINT64_MAX-(CHIRP_TEMPLATE_FRAMES-1U),config);
    CHECK(diag.status==CHIRP_WINDOW_MATCH && diag.correlation==1.0 && diag.clipped && diag.rms_pcm==32767U);
    threshold=*config;threshold.min_rms_pcm=32768U;
    diag=Compare(&ref,pcm,0U,&threshold);
    CHECK(diag.status==CHIRP_WINDOW_LOW_RMS && diag.correlation_valid && diag.clipped);
    CHECK(diag.correlation==1.0 && diag.rms_pcm==32767U);

    Invalid(NULL,pcm,1U,0U,config);Invalid(&ref,NULL,1U,0U,config);
    Invalid(&ref,pcm,0U,0U,config);Invalid(&ref,pcm,1U,0U,NULL);
    Invalid(&ref,pcm,1U,UINT64_MAX-(CHIRP_TEMPLATE_FRAMES-2U),config);
    Invalid(&ref,pcm,1U,UINT64_MAX,config);
    ChirpWindowTemplate bad_ref=ref;bad_ref.samples=NULL;Invalid(&bad_ref,pcm,1U,0U,config);
    bad_ref=ref;bad_ref.frames=15U;Invalid(&bad_ref,pcm,1U,0U,config);
    bad_ref=ref;bad_ref.frames=2401U;Invalid(&bad_ref,pcm,1U,0U,config);
    bad_ref=ref;bad_ref.variance=0U;Invalid(&bad_ref,pcm,1U,0U,config);
    threshold=*config;threshold.min_rms_pcm=32769U;Invalid(&ref,pcm,1U,0U,&threshold);
    const float bad_thresholds[]={0.0f,-0.1f,1.01f,NAN,INFINITY,-INFINITY};
    for(size_t i=0U;i<sizeof bad_thresholds/sizeof bad_thresholds[0];++i){
        threshold=*config;threshold.min_correlation=bad_thresholds[i];Invalid(&ref,pcm,1U,0U,&threshold);
    }
    /* out不可空；即使未获得匹配输出，独立diag仍必须复位。 */
    memset(&diag,0xa5,sizeof diag);
    CHECK(!ChirpWindow_ScoreWithDiagnostic(&ref,pcm,1U,0U,config,NULL,&diag));CheckInvalid(&diag);
    CHECK(!ChirpWindow_ScoreWithDiagnostic(&ref,pcm,1U,0U,config,NULL,NULL));
    CHECK(!ChirpWindow_Score(&ref,pcm,1U,0U,config,NULL));
    puts("PASS window diagnostics: legacy/new fields, raw low RMS, exact zero correlation, variance, inversion, clipping and integer/frame limits");
    return 0;
}
