/**
 * @file        test_stress.c
 * @brief       主机合成候选频率与调度压力测试：报告可持续搜索处理能力。
 * @details     用真实ChirpService、冻结模板和可控时钟，按音频块到达/前台循环/日志排空/
 *              偶发172ms间隔/周期日志突发合成调度；候选频率受原门控0.5秒冷却约束。
 * @note        纯主机模拟，不是板端时序轨迹或声学验收；不访问HAL、DMA、串口或开发板。
 */
#include "Detector/chirp_service.h"
#include "Detector/chirp_template.h"
#include "BSP/bsp_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRESS_LOG_CAPACITY 32U
#define STRESS_LOG_DRAIN_MS 9U
#define STRESS_DURATION_MS 20000U
#define STRESS_MAX_JOBS 512U
#define STRESS_STIMULUS_FIRST 8192U

static int16_t pcm[BSP_AUDIO_HALF_ELEMENTS];
static uint32_t now_ms;
static unsigned log_lines,log_pending,log_full;
static uint64_t job_times[STRESS_MAX_JOBS];
static unsigned job_head,job_tail,latency_count;
static uint32_t latencies[STRESS_MAX_JOBS];
/** @brief 条件求值一次，失败打印行号并结束；无返回值，测试专用。 */
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)

/** @brief 受控HAL毫秒，由模拟循环推进。 */
uint32_t BSP_Console_GetTickMs(void){return now_ms;}
/** @brief 模拟日志出口：只统计行数并在队列容量内排队，不访问串口。 */
static void Log(const char *text,int length)
{
    (void)text;CHECK(length>0 && length<256);++log_lines;
    if(log_pending<STRESS_LOG_CAPACITY)++log_pending;else ++log_full;
}
/** @brief 合成配置：候选间隔、每循环基础耗时、1Hz突发行数、偶发间隔周期与期望结果。 */
typedef struct {
    const char *name;
    uint32_t spacing_frames,base_loop_ms,burst_lines,gap_period_ms;
    bool expect_search_pass,expect_log_overflow;
} Profile;
/**
 * @brief 向一个8192帧块写入所有相交的冻结模板，其余样本为0。
 * @param block 块序号；spacing_frames为刺激间隔帧数。
 * @return 无返回值；块内声道1保持0，模拟单声道有效输入。
 */
static void FillBlock(uint32_t block,uint32_t spacing_frames)
{
    const uint64_t first=(uint64_t)block*BSP_AUDIO_HALF_FRAMES;
    const uint64_t end=first+BSP_AUDIO_HALF_FRAMES;
    memset(pcm,0,sizeof pcm);
    for(uint64_t start=STRESS_STIMULUS_FIRST;start<end;start+=spacing_frames)
    {
        for(uint32_t i=0U;i<CHIRP_TEMPLATE_FRAMES;++i)
        {
            const uint64_t frame=start+i;
            if(frame<first || frame>=end)continue;
            pcm[2U*(uint32_t)(frame-first)]=(int16_t)ChirpTemplate_PCM[i];
        }
    }
}
/** @brief 返回快照计数的无符号增量，无副作用。 */
static uint32_t Delta(uint32_t after,uint32_t before){return after-before;}
/** @brief 记录一个已接纳任务的终结时延到完整分布，超出容量时忽略，无返回值。 */
static void AddLatency(uint32_t age){if(latency_count<STRESS_MAX_JOBS)latencies[latency_count++]=age;}
/**
 * @brief 运行一个合成配置并打印候选/完成/丢弃/时延分布/模拟日志统计。
 * @param profile 非NULL只读配置，进程期有效。
 * @return true表示实测行为符合该配置预期；false不符合。
 * @note 任务时间队列按FIFO对应服务队列；FULL候选不计任务；其他Queue级拒绝应在本组配置中避免。
 */
static bool RunProfile(const Profile *profile)
{
    now_ms=0U;log_lines=log_pending=log_full=0U;job_head=job_tail=latency_count=0U;
    CHECK(ChirpService_Init(0U,0U,Log));
    BSP_AudioSnapshot snapshot={.running=true,.session=1U,.callback_count=1U,.last_callback_ms=0U};
    uint32_t block=0U;uint32_t last_block_ms=0U;
    uint32_t next_block_ms=(uint32_t)((uint64_t)BSP_AUDIO_HALF_FRAMES*1000U/BSP_AUDIO_NOMINAL_HZ);
    uint32_t next_burst_ms=1000U,next_gap_ms=profile->gap_period_ms;
    ChirpServiceSnapshot before,after;
    ChirpService_GetSnapshot(&before);
    bool queue_reject=false;
    while(now_ms<STRESS_DURATION_MS)
    {
        if(now_ms>=next_block_ms)
        {
            FillBlock(block,profile->spacing_frames);
            const BSP_AudioBlock audio={.samples=pcm,.session=1U,.frame_count=BSP_AUDIO_HALF_FRAMES,
                .first_frame=(uint64_t)block*BSP_AUDIO_HALF_FRAMES,.completed_ms=next_block_ms,
                .index_valid=true,.contiguous=true};
            CHECK(ChirpService_Stage(now_ms,&audio));
            CHECK(ChirpService_Complete(true));
            last_block_ms=next_block_ms;snapshot.last_callback_ms=next_block_ms;
            ++block;next_block_ms=(uint32_t)((uint64_t)block*BSP_AUDIO_HALF_FRAMES*1000U/BSP_AUDIO_NOMINAL_HZ);
            continue;
        }
        ChirpService_Process(now_ms,&snapshot);
        ChirpService_GetSnapshot(&after);
        const uint32_t new_candidates=Delta(after.candidates,before.candidates);
        const uint32_t full=Delta(after.drop_full,before.drop_full);
        for(uint32_t i=0U;i<new_candidates;++i)
        {
            CHECK(job_tail<STRESS_MAX_JOBS);
            job_times[job_tail++]=last_block_ms;
        }
        /* FULL表示候选被队长拒绝，没有形成任务；本测试配置应避免，出现则判失败。 */
        if(full!=0U)
        {
            queue_reject=true;
            for(uint32_t i=0U;i<full && job_tail>job_head;++i)--job_tail;
        }
        const uint32_t completed=Delta(after.completed_jobs,before.completed_jobs);
        for(uint32_t i=0U;i<completed;++i)
        {
            CHECK(job_head<job_tail);AddLatency((uint32_t)(now_ms-job_times[job_head++]));
        }
        const uint32_t expired=Delta(after.drop_expired,before.drop_expired);
        const uint32_t history=Delta(after.drop_history,before.drop_history);
        const uint32_t range=Delta(after.drop_range,before.drop_range);
        const uint32_t reset=Delta(after.drop_reset,before.drop_reset);
        for(uint32_t i=0U;i<expired+history+range+reset;++i)
        {
            CHECK(job_head<job_tail);AddLatency((uint32_t)(now_ms-job_times[job_head++]));
        }
        before=after;
        /* 每轮最多排空一行日志；无日志时按配置基础延迟推进。 */
        if(log_pending!=0U){--log_pending;now_ms+=STRESS_LOG_DRAIN_MS;}
        else{now_ms+=profile->base_loop_ms;}
        if(now_ms>=next_burst_ms)
        {
            for(uint32_t i=0U;i<profile->burst_lines;++i)
                if(log_pending<STRESS_LOG_CAPACITY)++log_pending;else ++log_full;
            next_burst_ms+=1000U;
        }
        if(profile->gap_period_ms!=0U && now_ms>=next_gap_ms){now_ms+=172U;next_gap_ms+=profile->gap_period_ms;}
    }
    ChirpService_GetSnapshot(&after);
    const unsigned pending=after.pending;
    const bool no_drop=after.dropped==0U && !queue_reject;
    uint32_t max_latency=0U;
    for(unsigned i=0U;i<latency_count;++i)if(latencies[i]>max_latency)max_latency=latencies[i];
    CHECK(job_head<=job_tail);
    /* 打印完整时延分布，主机保留每个已接纳任务的样本。 */
    for(unsigned i=0U;i<latency_count;++i)
        printf("LATENCY,PROFILE=%s,INDEX=%u,MS=%u\n",profile->name,i,latencies[i]);
    uint32_t sorted[STRESS_MAX_JOBS];
    memcpy(sorted,latencies,latency_count*sizeof sorted[0]);
    for(unsigned i=1U;i<latency_count;++i)
    {
        const uint32_t value=sorted[i];unsigned j=i;
        while(j>0U && sorted[j-1U]>value){sorted[j]=sorted[j-1U];--j;}
        sorted[j]=value;
    }
    const unsigned p95_index=(latency_count*95U)/100U;
    const uint32_t p50=latency_count!=0U ? sorted[latency_count/2U]:0U;
    const uint32_t p95=latency_count!=0U ? sorted[p95_index>=latency_count ? latency_count-1U:p95_index]:0U;
    const bool search_pass=no_drop && pending==0U && max_latency<=1500U;
    const bool target_met=search_pass && max_latency<=1000U;
    const bool log_overflow=log_full!=0U;
    printf("RESULT,PROFILE=%s,CAND=%u,DONE=%u,DROP=%u,PENDING=%u,MAX_COMPLETE_MS=%u,MAX_DROP_MS=%u,"
        "LAT_MAX_MS=%u,LAT_P50_MS=%u,LAT_P95_MS=%u,SIM_LOG_LINES=%u,SIM_LOG_FULL=%u,Q_REJECT=%u\n",
        profile->name,after.candidates,after.completed_jobs,after.dropped,pending,
        after.max_complete_ms,after.max_drop_ms,max_latency,p50,p95,log_lines,log_full,queue_reject?1U:0U);
    printf("VERDICT,PROFILE=%s,PASS=%u,TARGET1000=%u,LOG_OVERFLOW=%u\n",profile->name,
        search_pass?1U:0U,target_met?1U:0U,log_overflow?1U:0U);
    return search_pass==profile->expect_search_pass && log_overflow==profile->expect_log_overflow;
}
/** @brief 运行正常/慢循环、172ms间隔和日志突发组合，期望全部符合配置；成功返回0。 */
int main(void)
{
    static const Profile profiles[]={
        {"fast_2hz",24128U,1U,8U,0U,true,false},
        {"slow_2hz",24128U,34U,8U,7000U,false,false},
        {"fast_gap_2hz",24128U,1U,8U,7000U,true,false},
        {"heavy_log_2hz",24128U,1U,32U,7000U,true,true},
    };
    bool all=true;
    for(unsigned i=0U;i<sizeof profiles/sizeof profiles[0];++i)
    {
        const bool matches=RunProfile(&profiles[i]);
        printf("PROFILE_MATCH,PROFILE=%s,MATCH=%u\n",profiles[i].name,matches?1U:0U);
        all=all && matches;
    }
    puts(all ? "PASS synthetic candidate-frequency stress: expectations met"
             : "FAIL synthetic candidate-frequency stress: expectation mismatch");
    return all?0:1;
}
