/**
 * @file        test_chirp_matcher.c
 * @brief       验证流式相关核心的参数、会话、帧边界和峰发布契约。
 * @details     使用固定非周期小模板进行原生C行为测试，不访问硬件。
 * @note        真实50ms模板和独立相关对照由Python参考测试覆盖。
 */
#include "Detector/chirp_matcher.h"
#include "Detector/chirp_template.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const int16_t pattern[16]={1000,-700,400,900,-1200,300,-600,1100,
    -300,-800,700,-100,500,-1000,200,600};
static const ChirpMatcherConfig config={10U,0.95f,4U,100U};
static unsigned checks;
/** @brief 失败时打印当前断言并结束用例，条件只求值一次；成功时增加检查计数。 */
#define CHECK(condition) do { ++checks; if(!(condition)) { \
    fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition); return 1; } } while(0)

/** @brief 执行接口与流生命周期测试；返回0表示全部断言通过，无硬件输入。 */
int main(void)
{
    ChirpMatcher m={0}; ChirpMatch event;
    CHECK(CHIRP_TEMPLATE_FRAMES==2400U);
    CHECK(strcmp(ChirpTemplate_SourceSha256,"c37114332a698910e12ac855b47b566df229a3c7ec01414371e32969da3e084d")==0);
    CHECK(ChirpMatcher_Init(&m,pattern,16U,&config));
    CHECK(!ChirpMatcher_Init(NULL,pattern,16U,&config));
    CHECK(!ChirpMatcher_Init(&m,NULL,16U,&config));
    CHECK(!m.configured);
    CHECK(!ChirpMatcher_Init(&m,pattern,15U,&config));
    CHECK(!ChirpMatcher_Init(&m,pattern,2401U,&config));
    CHECK(!ChirpMatcher_Init(&m,pattern,16U,NULL));
    int16_t constant[16]={0};
    CHECK(!ChirpMatcher_Init(&m,constant,16U,&config));
    ChirpMatcherConfig bad=config;
    const float invalid[]={0.0f,-1.0f,1.01f,NAN,INFINITY};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        bad=config; bad.min_correlation=invalid[i];
        CHECK(!ChirpMatcher_Init(&m,pattern,16U,&bad));
    }
    bad=config;bad.peak_search_frames=0U;
    CHECK(!ChirpMatcher_Init(&m,pattern,16U,&bad));
    bad=config;bad.peak_search_frames=2401U;
    CHECK(!ChirpMatcher_Init(&m,pattern,16U,&bad));
    bad=config;bad.min_rms_pcm=32769U;
    CHECK(!ChirpMatcher_Init(&m,pattern,16U,&bad));
    CHECK(!ChirpMatcher_Reset(NULL));CHECK(!ChirpMatcher_Reset(&m));
    CHECK(ChirpMatcher_Init(&m,pattern,16U,&config));
    CHECK(ChirpMatcher_Init(&m,pattern,16U,&m.config));
    CHECK(ChirpMatcher_Push(NULL,7U,0U,0,&event)==CHIRP_MATCH_INVALID);
    CHECK(ChirpMatcher_Push(&m,7U,0U,pattern[0],NULL)==CHIRP_MATCH_INVALID);
    CHECK(!m.have_input);
    unsigned events=0U;
    for(uint64_t f=0;f<200U;++f) {
        int16_t x=f>=30U && f<46U ? pattern[f-30U]:0;
        ChirpMatcherResult r=ChirpMatcher_Push(&m,7U,f,x,&event);
        CHECK(r==CHIRP_MATCH_NONE || r==CHIRP_MATCH_EVENT);
        if(r==CHIRP_MATCH_EVENT) {
            ++events;CHECK(event.session==7U);CHECK(event.first_frame==30U);
            CHECK(event.last_frame==45U);CHECK(event.confirmed_frame==49U);
            CHECK(event.correlation>0.99999f);CHECK(!event.inverted && !event.clipped);
        } else CHECK(event.session==0U);
    }
    CHECK(events==1U);
    CHECK(ChirpMatcher_Push(&m,7U,201U,1,&event)==CHIRP_MATCH_DISCONTINUITY);
    CHECK(m.used==1U && !m.pending && !m.have_event);
    CHECK(ChirpMatcher_Push(&m,7U,201U,1,&event)==CHIRP_MATCH_DISCONTINUITY);
    CHECK(ChirpMatcher_Push(&m,8U,0U,1,&event)==CHIRP_MATCH_DISCONTINUITY);
    CHECK(ChirpMatcher_Push(&m,0U,1U,1,&event)==CHIRP_MATCH_INVALID);
    CHECK(!m.have_input && m.used==0U);
    CHECK(ChirpMatcher_Push(&m,9U,UINT64_MAX,1,&event)==CHIRP_MATCH_NONE);
    CHECK(ChirpMatcher_Push(&m,9U,0U,1,&event)==CHIRP_MATCH_INVALID);
    CHECK(m.previous_frame==UINT64_MAX);
    CHECK(ChirpMatcher_Push(&m,10U,0U,1,&event)==CHIRP_MATCH_DISCONTINUITY);
    CHECK(ChirpMatcher_Reset(&m));
    for(unsigned i=0;i<16U;++i)
        CHECK(ChirpMatcher_Push(&m,11U,i,pattern[i],&event)==CHIRP_MATCH_NONE);
    CHECK(m.pending);CHECK(ChirpMatcher_Reset(&m));CHECK(!m.pending);
    for(unsigned i=0;i<40U;++i)
        CHECK(ChirpMatcher_Push(&m,11U,i,0,&event)==CHIRP_MATCH_NONE);
    /* 元数据跨32位，最高16点窗口仍不能造成整数累加溢出。 */
    CHECK(ChirpMatcher_Reset(&m));
    const uint64_t first=UINT64_C(5000000000);
    for(unsigned i=0;i<24U;++i) {
        int16_t x=i<16U ? (int16_t)-pattern[i]:0;
        ChirpMatcherResult r=ChirpMatcher_Push(&m,12U,first+i,x,&event);
        if(i==19U) {
            CHECK(r==CHIRP_MATCH_EVENT);CHECK(event.first_frame==first);CHECK(event.inverted);
        } else CHECK(r==CHIRP_MATCH_NONE);
    }
    /* 待定匹配后跳帧，旧峰不得跨断流发布。 */
    CHECK(ChirpMatcher_Reset(&m));
    for(unsigned i=0;i<16U;++i)
        CHECK(ChirpMatcher_Push(&m,13U,i,pattern[i],&event)==CHIRP_MATCH_NONE);
    CHECK(m.pending);
    CHECK(ChirpMatcher_Push(&m,13U,17U,0,&event)==CHIRP_MATCH_DISCONTINUITY);
    CHECK(!m.pending);
    for(unsigned i=18U;i<60U;++i)
        CHECK(ChirpMatcher_Push(&m,13U,i,0,&event)==CHIRP_MATCH_NONE);
    /* 最大窗口与极值PCM，确认恰好落在UINT64_MAX；下一帧拒绝回绕。 */
    int16_t extreme[CHIRP_MATCHER_MAX_FRAMES];
    for(unsigned i=0;i<CHIRP_MATCHER_MAX_FRAMES;++i)extreme[i]=(i%2U)==0U ? INT16_MIN:INT16_MAX;
    CHECK(ChirpMatcher_Init(&m,extreme,CHIRP_MATCHER_MAX_FRAMES,&config));
    const uint64_t base=UINT64_MAX-UINT64_C(2403);
    for(unsigned i=0;i<2404U;++i) {
        int16_t x=i<2400U ? extreme[i]:0;
        ChirpMatcherResult r=ChirpMatcher_Push(&m,14U,base+i,x,&event);
        if(i==2403U) {
            CHECK(r==CHIRP_MATCH_EVENT);CHECK(event.first_frame==base);
            CHECK(event.confirmed_frame==UINT64_MAX);CHECK(event.clipped);
            CHECK(event.correlation>0.99999f);CHECK(event.rms_pcm==32767U);
        } else CHECK(r==CHIRP_MATCH_NONE);
    }
    CHECK(ChirpMatcher_Push(&m,14U,0U,0,&event)==CHIRP_MATCH_INVALID);
    printf("PASS %u C interface/stream checks; state_bytes=%u\n",checks,(unsigned)sizeof m);
    return 0;
}
