/**
 * @file        test_batch.c
 * @brief       对照原逐帧实现验证批量门控的事件、全部逻辑状态及消费合同。
 * @details     用同一PCM逐字段比较；GCC函数进入钩子区分快速路径与逐点回退，不把主机耗时当实机性能。
 * @note        仅原生测试；输入由测试拥有，不访问HAL、DMA、串口或开发板。
 */
#include "Detector/sound_detector.h"
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef SBCG_BATCH_RED_STANDIN
/* RED阶段只声明测试替身，生产头文件尚不应为了测试预先增加接口。 */
SoundDetectorResult SoundDetector_PushBatch(SoundDetector *,uint32_t,uint64_t,
    const int16_t *,uint16_t,uint16_t *,SoundCandidate *);
#endif

static uint64_t checks,batch_calls,fast_calls,fallback_calls,event_count;
static void *reference_address;
static bool calibrating;
static uint64_t reference_entries;
static int16_t pcm[16384];

/** @brief 求值一次并累计断言；失败输出位置后结束测试，不弹出Windows对话框。 */
#define CHECK(expr) do { ++checks; if(!(expr)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr);exit(1);} } while(0)

/**
 * @brief 测试专用GCC函数进入钩子，记录原Push调用次数。
 * @param function GCC提供的被进入函数地址，仅比较，不解引用。
 * @param caller GCC提供的调用点地址，未使用。
 * @return 无返回值。
 * @note 本函数禁止自身插桩；首次校准来自外部直接调用原Push，不转换函数指针类型。
 */
void __attribute__((no_instrument_function)) __cyg_profile_func_enter(void *function,void *caller)
{
    (void)caller;
    if(calibrating){reference_address=function;calibrating=false;}
    if(function==reference_address)++reference_entries;
}
/** @brief GCC退出钩子；两地址均未使用，无返回值，不参与计时。 */
void __attribute__((no_instrument_function)) __cyg_profile_func_exit(void *function,void *caller)
{(void)function;(void)caller;}

/** @brief 逐字段比较候选a/b，不读取结构体填充字节，无返回值。 */
static void SameEvent(const SoundCandidate *a,const SoundCandidate *b)
{
    CHECK(a->session==b->session);CHECK(a->onset_frame==b->onset_frame);
    CHECK(a->confirmed_frame==b->confirmed_frame);CHECK(a->mean_square_ac==b->mean_square_ac);
    CHECK(a->peak_abs==b->peak_abs);CHECK(a->clipped==b->clipped);
}
/** @brief 逐字段比较两个调用方持有的有效门控状态，无返回值。 */
static void SameState(const SoundDetector *a,const SoundDetector *b)
{
#define SAME(field) CHECK(a->field==b->field)
    SAME(config.window_frames);SAME(config.on_rms_pcm);SAME(config.off_rms_pcm);
    SAME(config.on_windows);SAME(config.quiet_windows);SAME(config.refractory_frames);
    SAME(configured);SAME(have_input);SAME(armed);SAME(have_event);SAME(session);
    SAME(last_frame);SAME(window_first);SAME(onset_frame);SAME(last_event_frame);
    SAME(sum);SAME(sum_squares);SAME(used);SAME(on_count);SAME(quiet_count);
    SAME(peak_abs);SAME(run_peak);SAME(run_mean_square);SAME(clipped);SAME(run_clipped);
#undef SAME
}
/** @brief 对两个独占状态使用相同配置初始化，无返回值。 */
static void PairInit(SoundDetector *batch,SoundDetector *reference,const SoundDetectorConfig *config)
{CHECK(SoundDetector_Init(batch,config));CHECK(SoundDetector_Init(reference,config));SameState(batch,reference);}

/**
 * @brief 执行一次批量调用，并用已消费样本推进原实现作精确对照。
 * @param batch/reference 调用方独占的两个已同步状态。
 * @param session 当前测试会话，0用于原拒绝语义。
 * @param first 首样本绝对帧。
 * @param samples 至少limit点的有效测试PCM；预计读前拒绝时可使用占位地址。
 * @param limit 本次可用点数1..64。
 * @return 实际消费点数0/1/64。
 * @note 进入时两个状态应等价；快速路径不得调用原Push，回退只调用一次。
 */
static uint16_t Step(SoundDetector *batch,SoundDetector *reference,uint32_t session,
    uint64_t first,const int16_t *samples,uint16_t limit)
{
    SoundCandidate got={9U,9U,9U,9U,9U,true},expected={0};uint16_t used=99U;
    const uint64_t entered=reference_entries;
    const SoundDetectorResult actual=SoundDetector_PushBatch(batch,session,first,samples,limit,&used,&got);
    const uint64_t calls=reference_entries-entered;++batch_calls;
    CHECK(used==0U || used==1U || used==64U);CHECK(used<=limit);
    SoundDetectorResult wanted=SOUND_DETECTOR_INVALID;
    if(used==64U){++fast_calls;CHECK(calls==0U);}
    if(used==1U){++fallback_calls;CHECK(calls==1U);}
    if(used==0U){
        /* 此分支只用于原实现可在读PCM前拒绝的会话/配置/末帧条件。 */
        wanted=SoundDetector_Push(reference,session,first,0,&expected);
        CHECK(wanted==SOUND_DETECTOR_INVALID);
    }else{
        CHECK(first<=UINT64_MAX-(used-1U));
        for(uint16_t i=0U;i<used;++i){
            wanted=SoundDetector_Push(reference,session,first+i,samples[i],&expected);
            if(i+1U<used)CHECK(wanted==SOUND_DETECTOR_NONE);
        }
    }
    CHECK(actual==wanted);SameEvent(&got,&expected);SameState(batch,reference);
    if(actual==SOUND_DETECTOR_EVENT)++event_count;
    return used;
}

/** @brief 输入count点有效PCM，以limit或随机1..64分块；first与点数不得使坐标溢出。 */
static void Feed(SoundDetector *batch,SoundDetector *reference,uint32_t session,
    uint64_t first,const int16_t *samples,uint32_t count,uint16_t limit,uint32_t seed)
{
    uint32_t offset=0U;
    while(offset<count){
        uint16_t available=limit;
        /* 取较高位，避免LCG低6位的64周期与窗口长度锁相，导致随机用例从不进入快速路径。 */
        if(seed!=0U){seed=seed*1664525U+1013904223U;available=(uint16_t)(1U+(seed>>16)%64U);}
        if(count-offset<available)available=(uint16_t)(count-offset);
        const uint16_t used=Step(batch,reference,session,first+offset,samples+offset,available);
        CHECK(used!=0U);offset+=used;
    }
}
/** @brief 生成安静、阈值邻界、直流、持续声和截幅窗，config为当前门控配置。 */
static void FillPattern(const SoundDetectorConfig *config)
{
    const uint32_t width=config->window_frames;
    for(uint32_t i=0U;i<16384U;++i){
        const uint32_t window=i/width,phase=window%96U;
        int32_t amplitude=0,dc=0;
        if(phase>=12U && phase<16U)amplitude=(int32_t)config->on_rms_pcm-1;
        else if(phase>=16U && phase<25U)amplitude=config->on_rms_pcm;
        else if(phase>=25U && phase<48U)amplitude=10000;
        else if(phase>=48U && phase<51U)amplitude=config->off_rms_pcm;
        else if(phase>=52U && phase<54U)amplitude=(config->on_rms_pcm+config->off_rms_pcm)/2;
        else if(phase>=64U && phase<72U){dc=24000;amplitude=1000;}
        else if(phase>=80U && phase<84U){pcm[i]=i%2U ? INT16_MAX:INT16_MIN;continue;}
        pcm[i]=(int16_t)(dc+(i%2U ? amplitude:-amplitude));
    }
}
/** @brief 验证64点路径确实每8192帧调用128次，并在原有阈值两组下逐字段对照。 */
static void TestFastAndPartitions(void)
{
    for(unsigned profile=0U;profile<2U;++profile){
        const SoundDetectorConfig config={64U,profile==0U ? 256U:128U,profile==0U ? 128U:64U,2U,8U,24000U};
        FillPattern(&config);
        SoundDetector batch,reference;PairInit(&batch,&reference,&config);
        const uint64_t before_fast=fast_calls,before_calls=batch_calls;
        CHECK(Step(&batch,&reference,7U,8189U,pcm,64U)==64U);
        Feed(&batch,&reference,7U,8189U+64U,pcm+64U,8192U-64U,64U,0U);
        CHECK(fast_calls-before_fast==128U && batch_calls-before_calls==128U);
        Feed(&batch,&reference,7U,8189U+8192U,pcm+8192U,8192U,64U,0U);
        for(uint16_t limit=1U;limit<=64U;++limit){
            PairInit(&batch,&reference,&config);
            Feed(&batch,&reference,7U,UINT64_C(5000000003),pcm,16384U,limit,0U);
        }
        for(uint32_t seed=1U;seed<=8U;++seed){
            PairInit(&batch,&reference,&config);Feed(&batch,&reference,7U,31U,pcm,16384U,64U,seed);
        }
        PairInit(&batch,&reference,&config);Feed(&batch,&reference,7U,0U,pcm,63U,63U,0U);
        CHECK(batch.used==63U);CHECK(Step(&batch,&reference,7U,63U,pcm+63U,64U)==1U);
        CHECK(batch.used==0U);CHECK(Step(&batch,&reference,7U,64U,pcm+64U,64U)==64U);
    }
    puts("PASS fast path: 8192 frames/128 calls/no reference Push; both thresholds and all partitions");
}
/** @brief 验证2..256配置均与原实现一致，64之外的配置每次只消费一个点。 */
static void TestOtherWindows(void)
{
    for(uint16_t width=2U;width<=256U;++width){
        const SoundDetectorConfig config={width,8U,2U,2U,2U,8U};
        FillPattern(&config);SoundDetector batch,reference;PairInit(&batch,&reference,&config);
        const uint64_t before_fast=fast_calls;
        Feed(&batch,&reference,3U,8179U,pcm,2048U,64U,0U);
        if(width!=64U)CHECK(fast_calls==before_fast);
    }
    puts("PASS window configuration: every width2..256 matches original logical state");
}
/** @brief 验证缺口/重放/倒序/换会话只消费第一个异常点，以及uint64上下界。 */
static void TestBoundaries(void)
{
    const SoundDetectorConfig config={64U,128U,64U,2U,8U,24000U};
    FillPattern(&config);SoundDetector batch,reference;
    for(unsigned change=0U;change<4U;++change){
        PairInit(&batch,&reference,&config);CHECK(Step(&batch,&reference,1U,100U,pcm,64U)==64U);
        const uint32_t session=change==0U ? 2U:1U;
        const uint64_t first=change==1U ? 200U:change==2U ? 163U:change==3U ? 20U:164U;
        CHECK(Step(&batch,&reference,session,first,pcm+64U,64U)==1U);
        CHECK(batch.used==1U);Feed(&batch,&reference,session,first+1U,pcm+65U,127U,64U,0U);
    }
    PairInit(&batch,&reference,&config);
    CHECK(Step(&batch,&reference,1U,UINT64_MAX-63U,pcm,64U)==64U);
    CHECK(batch.last_frame==UINT64_MAX);
    const int16_t *invalid=(const int16_t *)(uintptr_t)1U;
    CHECK(Step(&batch,&reference,1U,0U,invalid,64U)==0U);
    CHECK(Step(&batch,&reference,2U,0U,pcm,64U)==1U);
    PairInit(&batch,&reference,&config);
    CHECK(Step(&batch,&reference,1U,UINT64_MAX-62U,pcm,64U)==1U);
    Feed(&batch,&reference,1U,UINT64_MAX-61U,pcm+1U,62U,64U,0U);
    CHECK(batch.last_frame==UINT64_MAX);CHECK(Step(&batch,&reference,1U,UINT64_MAX,invalid,1U)==0U);
    PairInit(&batch,&reference,&config);Feed(&batch,&reference,7U,0U,pcm,700U,64U,0U);
    CHECK(Step(&batch,&reference,0U,700U,invalid,64U)==0U);CHECK(!batch.have_input);
    batch=(SoundDetector){0};reference=(SoundDetector){0};CHECK(Step(&batch,&reference,1U,0U,invalid,64U)==0U);
    puts("PASS stream boundaries: gap/replay/reverse/session, partial windows, read-before-reject and uint64 end");
}
/** @brief 检查新指针/长度合同；失败清可用输出且不改状态，不读取占位PCM地址。 */
static void TestInvalidParameters(void)
{
    const SoundDetectorConfig config={64U,128U,64U,2U,8U,24000U};SoundDetector batch,reference;
    PairInit(&batch,&reference,&config);
    const SoundCandidate empty={0};const int16_t *invalid=(const int16_t *)(uintptr_t)1U;
    for(unsigned variant=0U;variant<7U;++variant){
        SoundCandidate out={1,2,3,4,5,true};uint16_t consumed=99U;
        SoundDetector *detector=variant==0U ? NULL:&batch;
        const int16_t *input=variant==1U ? NULL:invalid;
        uint16_t amount=variant==2U ? 0U:variant==3U ? 65U:variant==4U ? UINT16_MAX:64U;
        uint16_t *used=variant==5U ? NULL:&consumed;SoundCandidate *result=variant==6U ? NULL:&out;
        CHECK(SoundDetector_PushBatch(detector,1U,0U,input,amount,used,result)==SOUND_DETECTOR_INVALID);
        if(used!=NULL)CHECK(consumed==0U);
        if(result!=NULL)SameEvent(&out,&empty);
        SameState(&batch,&reference);
    }
    puts("PASS invalid inputs: all output/pointer/length branches reject before PCM and preserve state");
}
/**
 * @brief 用固定种子噪声及正负直流验证两套门限，固定/随机分块均须实际经过快速路径。
 * @return 无返回值。
 * @note 样本完整参与对照；不以两个路径都无事件替代逻辑状态比较。
 */
static void TestNoiseAndDC(void)
{
    static const int32_t offsets[]={0,24000,-24000};
    for(unsigned profile=0U;profile<2U;++profile){
        const SoundDetectorConfig config={64U,profile==0U ? 256U:128U,profile==0U ? 128U:64U,2U,8U,24000U};
        for(unsigned dc=0U;dc<sizeof offsets/sizeof offsets[0];++dc){
            uint32_t rng=UINT32_C(0x20261026)+profile;
            for(uint32_t i=0U;i<16384U;++i){
                const uint32_t phase=(i/64U)%96U;
                const int32_t amplitude=phase<12U || (phase>=36U && phase<64U) ?
                    (int32_t)config.off_rms_pcm/2:(int32_t)config.on_rms_pcm*3;
                rng=rng*1664525U+1013904223U;
                const int32_t value=offsets[dc]+(int32_t)(rng%(uint32_t)(2*amplitude+1))-amplitude;
                CHECK(value>=INT16_MIN && value<=INT16_MAX);pcm[i]=(int16_t)value;
            }
            for(unsigned random=0U;random<2U;++random){
                SoundDetector batch,reference;PairInit(&batch,&reference,&config);
                const uint64_t before=fast_calls;
                for(unsigned block=0U;block<2U;++block)
                    Feed(&batch,&reference,29U,UINT64_C(9000000001)+block*UINT64_C(16384),
                         pcm,16384U,64U,random ? UINT32_C(0x31415926):0U);
                CHECK(fast_calls>before);
            }
        }
    }
    puts("PASS seeded noise: both thresholds, zero/positive/negative DC, fixed/random partitions and fast path");
}
/**
 * @brief 回放指定小端PCM16文件，并对照可选整数衰减后的低增益输入。
 * @param path 测试PCM文件路径，由调用方提供并在本调用期间有效。
 * @param config 当前门控配置，只读借用。
 * @param random true使用固定种子的随机分块，false使用64点分块。
 * @param divisor 非零整数衰减系数，PCM按C有符号除法向零截断。
 * @return 与逐点参考一致的候选事件数，低于门限允许为0。
 * @note 不更改源文件；每次回放必须实际经过完整窗快速路径。
 */
static uint64_t FileEvents(const char *path,const SoundDetectorConfig *config,bool random,uint16_t divisor)
{
    CHECK(divisor!=0U);FILE *file=fopen(path,"rb");CHECK(file!=NULL);SoundDetector batch,reference;
    PairInit(&batch,&reference,config);uint64_t first=0U;const uint64_t before=event_count;
    const uint64_t before_fast=fast_calls;
    uint8_t raw[128];int16_t samples[64];size_t length;
    while((length=fread(raw,1U,sizeof raw,file))!=0U){
        CHECK(length%2U==0U);
        for(size_t i=0U;i<length/2U;++i){
            const uint16_t bits=(uint16_t)(raw[2U*i]|((uint16_t)raw[2U*i+1U]<<8));
            const int32_t decoded=bits<=INT16_MAX ? (int32_t)bits:(int32_t)bits-65536;
            samples[i]=(int16_t)(decoded/divisor);
        }
        Feed(&batch,&reference,11U,first,samples,(uint32_t)(length/2U),64U,random ? (uint32_t)(first+1U):0U);
        first+=length/2U;
    }
    CHECK(!ferror(file));CHECK(fclose(file)==0);CHECK(first==960000U);CHECK(fast_calls>before_fast);
    return event_count-before;
}
/** @brief 校准钩子后执行所有合同及冻结WAV对照；argv[1]为小端PCM16，成功返回0。 */
int main(int argc,char **argv)
{
    CHECK(argc==2);SoundDetector detector={0};SoundCandidate out;
    calibrating=true;CHECK(SoundDetector_Push(&detector,0U,0U,0,&out)==SOUND_DETECTOR_INVALID);
    CHECK(!calibrating && reference_address!=NULL && reference_entries!=0U);
    TestFastAndPartitions();TestOtherWindows();TestBoundaries();TestInvalidParameters();TestNoiseAndDC();
    for(unsigned profile=0U;profile<2U;++profile){
        const SoundDetectorConfig config={64U,profile==0U ? 256U:128U,profile==0U ? 128U:64U,2U,8U,24000U};
        CHECK(FileEvents(argv[1],&config,false,1U)==10U);CHECK(FileEvents(argv[1],&config,true,1U)==10U);
        static const uint16_t divisors[]={16U,64U,128U};
        for(unsigned gain=0U;gain<sizeof divisors/sizeof divisors[0];++gain){
            const uint64_t fixed=FileEvents(argv[1],&config,false,divisors[gain]);
            const uint64_t random=FileEvents(argv[1],&config,true,divisors[gain]);
            CHECK(fixed==random);
            printf("PASS low-gain WAV: profile=%u,divisor=%u,events=%" PRIu64 ",fixed/random agree\n",
                profile,(unsigned)divisors[gain],fixed);
        }
    }
    printf("PASS batch core: checks=%" PRIu64 ",calls=%" PRIu64 ",fast=%" PRIu64 ",fallback=%" PRIu64 ",events=%" PRIu64 "\n",
        checks,batch_calls,fast_calls,fallback_calls,event_count);return 0;
}
