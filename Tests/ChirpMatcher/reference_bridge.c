/**
 * @file        reference_bridge.c
 * @brief       在原生进程中运行真实C模板匹配器并输出事件。
 * @details     以受限十进制输入传递模板/配置和连续PCM，输出帧号高低字避免MinGW格式差异。
 * @note        仅电脑端测试，不加入固件，无硬件访问。
 */
#include "Detector/chirp_matcher.h"
#include "Detector/chirp_template.h"
#include <inttypes.h>
#include <stdio.h>

/**
 * @brief 执行单个连续样本用例，打印已确认事件和结束标记。
 * @return 0表示输入/流处理正常；1表示格式/范围错误，2表示意外流状态。
 * @note 输入上限1000000帧；模板上限2400；帧号从高低32位组成，样本INT16范围。
 */
int main(void)
{
    uint32_t n,rms,search,holdoff,high,low,count;float correlation;
    if(scanf("%" SCNu32 " %" SCNu32 " %f %" SCNu32 " %" SCNu32 " %" SCNu32
             " %" SCNu32 " %" SCNu32,&n,&rms,&correlation,&search,&holdoff,&high,&low,&count)!=8)
        return 1;
    if(n!=CHIRP_TEMPLATE_FRAMES || rms>32768U || search>2400U ||
       count==0U || count>1000000U)return 1;
    const uint64_t first=((uint64_t)high<<32)|low;
    if(first>UINT64_MAX-(count-1U))return 1;
    int16_t template_pcm[CHIRP_MATCHER_MAX_FRAMES];
    for(uint32_t i=0U;i<n;++i) {
        int x;if(scanf("%d",&x)!=1 || x<INT16_MIN || x>INT16_MAX)return 1;
        template_pcm[i]=(int16_t)x;
        if(template_pcm[i]!=ChirpTemplate_PCM[i])return 1;
    }
    const ChirpMatcherConfig config={(uint16_t)rms,correlation,(uint16_t)search,holdoff};
    ChirpMatcher matcher;
    if(!ChirpMatcher_Init(&matcher,ChirpTemplate_PCM,(uint16_t)n,&config))return 1;
    for(uint32_t i=0U;i<count;++i) {
        int x;if(scanf("%d",&x)!=1 || x<INT16_MIN || x>INT16_MAX)return 1;
        ChirpMatch event;
        ChirpMatcherResult result=ChirpMatcher_Push(&matcher,7U,first+i,(int16_t)x,&event);
        if(result==CHIRP_MATCH_EVENT)
            printf("E %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32
                   " %" PRIu32 " %.9g %u %u %u\n",
                (uint32_t)(event.first_frame>>32),(uint32_t)event.first_frame,
                (uint32_t)(event.last_frame>>32),(uint32_t)event.last_frame,
                (uint32_t)(event.confirmed_frame>>32),(uint32_t)event.confirmed_frame,
                (double)event.correlation,(unsigned)event.rms_pcm,
                event.inverted ? 1U:0U,event.clipped ? 1U:0U);
        else if(result!=CHIRP_MATCH_NONE)return 2;
    }
    puts("END");return 0;
}
