/**
 * @file        test_sound_detector.c
 * @brief       用合成PCM验证真实候选检测内核的流边界、能量和去重。
 * @details     不含HAL替身；样本及帧坐标由测试生成，任何失败直接退出而不弹出Windows断言对话框。
 * @note        60秒数字静默测试不是教室实物无误报验收。
 */
#include "Detector/sound_detector.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, events;
#define CHECK(expr) do { ++checks; if (!(expr)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr);exit(1); } } while(0)
static SoundDetector detector;
static SoundCandidate event;
static uint64_t frame;
static uint32_t session;
static const SoundDetectorConfig config={4U,8U,2U,2U,2U,8U};
/** @brief 复位真实内核和测试流；first是首帧坐标，无返回值。 */
static void Start(uint64_t first)
{ CHECK(SoundDetector_Init(&detector,&config));events=0;frame=first;session=1U;event=(SoundCandidate){0}; }
/** @brief 检查无效输出被全部清零；out为只读本次输出，无返回值。 */
static void Empty(const SoundCandidate *out)
{ CHECK(out->session==0U && out->onset_frame==0U && out->confirmed_frame==0U && out->mean_square_ac==0U && out->peak_abs==0U && !out->clipped); }
/** @brief 输入一帧并检查结果的公开契约；sample为PCM16，返回真实处理结果。 */
static SoundDetectorResult Feed(int16_t sample)
{
 SoundCandidate out={99U,99U,99U,99U,99U,true};
 SoundDetectorResult result=SoundDetector_Push(&detector,session,frame,sample,&out);
 if(frame!=UINT64_MAX)++frame;
 if(result==SOUND_DETECTOR_EVENT){++events;event=out;CHECK(out.session==session);}
 else Empty(&out);
 return result;
}
/** @brief 生成count帧交替符号AC及直流dc；amp为PCM幅值且由用例确保范围，无返回值。 */
static void Wave(unsigned count,int amp,int dc)
{ for(unsigned i=0;i<count;++i){int sample=dc+(i%2U ? amp:-amp);CHECK(sample>=INT16_MIN && sample<=INT16_MAX);SoundDetectorResult r=Feed((int16_t)sample);CHECK(r==SOUND_DETECTOR_NONE||r==SOUND_DETECTOR_EVENT);} }
/** @brief 测配置、NULL、Reset及自配置复制边界；无参数，无返回值。 */
static void TestConfiguration(void)
{
 SoundDetectorConfig bad=config;
 CHECK(!SoundDetector_Init(NULL,&config));CHECK(!SoundDetector_Init(&detector,NULL));
 CHECK(!SoundDetector_Reset(NULL));CHECK(!SoundDetector_Reset(&detector));
 for(unsigned i=0;i<7;++i){bad=config;
  switch(i){case 0:bad.window_frames=1;break;case 1:bad.window_frames=257;break;case 2:bad.on_rms_pcm=0;break;case 3:bad.on_rms_pcm=32769;break;case 4:bad.off_rms_pcm=8;break;case 5:bad.on_windows=0;break;default:bad.quiet_windows=0;break;}
  CHECK(!SoundDetector_Init(&detector,&bad));CHECK(!detector.configured);
 }
 SoundCandidate out={1,2,3,4,5,true};CHECK(SoundDetector_Push(NULL,1,0,0,&out)==SOUND_DETECTOR_INVALID);Empty(&out);
 Start(0);CHECK(SoundDetector_Push(&detector,1,0,0,NULL)==SOUND_DETECTOR_INVALID && !detector.have_input);
 CHECK(SoundDetector_Init(&detector,&detector.config));CHECK(detector.config.window_frames==4U);
 CHECK(SoundDetector_Reset(&detector));CHECK(!detector.armed && !detector.have_input);
 puts("PASS config: ranges, NULL, reset and aliased config are explicit");
}
/** @brief 合成60秒带直流/有界噪声并确认已布防仍不触发；无参数，无返回值。 */
static void TestQuietAndDC(void)
{
 Start(0);Wave(8,0,30000);CHECK(detector.armed);
 uint32_t rng=1;
 for(unsigned i=0;i<60U*48000U;++i){rng=rng*1664525U+1013904223U;int16_t x=(int16_t)(30000+(int)(rng%11U)-5);CHECK(Feed(x)==SOUND_DETECTOR_NONE);}
 CHECK(events==0 && detector.armed);
 Start(0);Wave(400,1000,0);CHECK(events==0 && !detector.armed);
 Wave(8,0,0);Wave(8,8,0);CHECK(events==1);
 puts("PASS silence: 60s synthetic DC/noise, startup loud requires quiet before candidate");
}
/** @brief 验证精确阈值、去直流质量及跨8192帧边界；无参数，无返回值。 */
static void TestLocation(void)
{
 Start(8180);Wave(8,0,-30000);Wave(4,7,-30000);CHECK(events==0);Wave(8,8,-30000);
 CHECK(events==1 && event.onset_frame==8192U && event.confirmed_frame==8199U);
 CHECK(event.mean_square_ac==64U && event.peak_abs==30008U && !event.clipped);
 Start(UINT64_C(5000000000));Wave(8,0,0);Wave(8,8,0);
 CHECK(event.onset_frame==UINT64_C(5000000008) && event.confirmed_frame==UINT64_C(5000000015));
 for(unsigned phase=0;phase<16;++phase){Start(8180U+phase);Wave(8,0,0);Wave(8,8,0);CHECK(events==1 && event.onset_frame==8188U+phase && event.confirmed_frame==8195U+phase);}
 puts("PASS location: equality threshold, DC removal, DMA boundary phases and uint64 frame coordinates");
}
/** @brief 验证瞬态、滞回区、持续声音及重新安静门控；无参数，无返回值。 */
static void TestDeduplication(void)
{
 Start(0);Wave(8,0,0);Wave(4,100,0);Wave(4,3,0);CHECK(events==0);Wave(8,8,0);CHECK(events==1 && event.onset_frame==16U);
 Wave(400,1000,0);CHECK(events==1);Wave(40,3,0);CHECK(!detector.armed && events==1);
 Wave(8,2,0);CHECK(detector.armed);Wave(8,8,0);CHECK(events==2);
 puts("PASS dedup: transient rejection, hysteresis, sustained sound once, exact quiet threshold rearms");
}
/** @brief 验证冷却期限和静默双条件且不阻断正常第二事件；无参数，无返回值。 */
static void TestRefractory(void)
{
 SoundDetectorConfig cfg=config;cfg.refractory_frames=20;
 Start(0);CHECK(SoundDetector_Init(&detector,&cfg));Wave(8,0,0);Wave(8,8,0);CHECK(events==1 && event.confirmed_frame==15U);
 Wave(16,0,0);CHECK(!detector.armed);Wave(4,0,0);CHECK(detector.armed);Wave(8,8,0);CHECK(events==2 && event.onset_frame==36U);
 Start(0);cfg.refractory_frames=0;CHECK(SoundDetector_Init(&detector,&cfg));Wave(8,0,0);Wave(8,8,0);Wave(4,0,0);CHECK(!detector.armed);Wave(4,0,0);CHECK(detector.armed);
 puts("PASS refractory: elapsed frame boundary plus consecutive quiet, zero holdoff still requires quiet");
}
/** @brief 验证缺口、重复、倒序、会话切换和非法会话均不沿用旧候选；无参数，无返回值。 */
static void TestDiscontinuity(void)
{
 for(unsigned kind=0;kind<4;++kind){Start(0);Wave(8,0,0);Wave(4,8,0);
  if(kind==0)++frame;else if(kind==1)--frame;else if(kind==2)frame=2;else ++session;
  CHECK(Feed(8)==SOUND_DETECTOR_DISCONTINUITY);Wave(100,8,0);CHECK(events==0);
  CHECK(SoundDetector_Reset(&detector));Wave(8,0,0);Wave(8,8,0);CHECK(events==1);
 }
 Start(0);Wave(8,0,0);session=0;CHECK(Feed(0)==SOUND_DETECTOR_INVALID && !detector.have_input);session=1;Wave(8,8,0);CHECK(events==0);
 puts("PASS discontinuity: gap/repeat/reverse/session invalidates evidence and output, explicit reset works");
}
/** @brief 验证PCM极值、窗长边界及uint64末帧后不回绕；无参数，无返回值。 */
static void TestLimits(void)
{
 SoundDetectorConfig cfg={256U,1U,0U,1U,1U,0U};Start(0);CHECK(SoundDetector_Init(&detector,&cfg));Wave(256,0,INT16_MIN);
 CHECK(detector.armed);for(unsigned i=0;i<256;++i)(void)Feed(i%2U?INT16_MAX:INT16_MIN);
 CHECK(events==1 && event.mean_square_ac==1073709056U && event.peak_abs==32768U && event.clipped);
 Start(UINT64_MAX-15U);Wave(8,0,0);Wave(8,8,0);CHECK(events==1 && event.confirmed_frame==UINT64_MAX);
 frame=0;CHECK(Feed(0)==SOUND_DETECTOR_INVALID);CHECK(Feed(0)==SOUND_DETECTOR_INVALID);
 ++session;frame=0;CHECK(Feed(0)==SOUND_DETECTOR_DISCONTINUITY);CHECK(!detector.armed);
 Start(0);cfg=(SoundDetectorConfig){2U,32768U,32767U,UINT16_MAX,UINT16_MAX,UINT32_MAX};CHECK(SoundDetector_Init(&detector,&cfg));
 puts("PASS limits: PCM16 extremes, 256-frame variance, clipping and terminal uint64 index");
}
/** @brief 验证非整方差不被提前取整，断流当前样本成为新窗起点；无参数，无返回值。 */
static void TestFractionAndNewOrigin(void)
{
 SoundDetectorConfig cfg={3U,1U,0U,1U,1U,0U};Start(0);CHECK(SoundDetector_Init(&detector,&cfg));
 (void)Feed(-1);(void)Feed(0);(void)Feed(0);CHECK(!detector.armed);
 Wave(3,0,0);CHECK(detector.armed);
 (void)Feed(-1);(void)Feed(1);(void)Feed(1);CHECK(events==0);
 (void)Feed(-2);(void)Feed(1);(void)Feed(1);CHECK(events==1 && event.onset_frame==9U && event.confirmed_frame==11U && event.mean_square_ac==2U);
 Start(0);Wave(8,0,0);Wave(4,8,0);frame+=5;
 CHECK(Feed(0)==SOUND_DETECTOR_DISCONTINUITY && detector.used==1U);
 Wave(7,0,0);CHECK(detector.armed);Wave(8,8,0);
 CHECK(events==1 && event.onset_frame==25U && event.confirmed_frame==32U);
 puts("PASS fractional energy and discontinuity origin: no early floor, current frame consumed exactly once");
}
/** @brief 将安静及确认计数实际推进到65535窗，验证不回绕；无参数，无返回值。 */
static void TestMaximumWindowCounts(void)
{
 SoundDetectorConfig cfg={2U,1U,0U,UINT16_MAX,UINT16_MAX,0U};
 Start(0);CHECK(SoundDetector_Init(&detector,&cfg));
 Wave(2U*(UINT16_MAX-1U),0,0);CHECK(!detector.armed && detector.quiet_count==UINT16_MAX-1U);
 Wave(2,0,0);CHECK(detector.armed);
 Wave(2U*(UINT16_MAX-1U),1,0);CHECK(events==0 && detector.on_count==UINT16_MAX-1U);
 Wave(2,1,0);CHECK(events==1 && event.onset_frame==131070U && event.confirmed_frame==262139U);
 puts("PASS counter limits: 65535 quiet and confirming windows reach exact boundary without wrap");
}
/** @brief 运行全部纯C公开行为测试；无参数，成功返回0。 */
int main(void)
{
 TestConfiguration();TestQuietAndDC();TestLocation();TestDeduplication();TestRefractory();TestDiscontinuity();TestLimits();TestFractionAndNewOrigin();TestMaximumWindowCounts();
 printf("PASS SoundDetector: 9 groups, %u checks\n",checks);return 0;
}
