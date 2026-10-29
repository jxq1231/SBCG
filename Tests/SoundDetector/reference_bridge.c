/**
 * @file        reference_bridge.c
 * @brief       通过独立主机进程运行真实C检测器，避免Python与编译器位宽耦合。
 * @details     输入每用例一组配置、首帧和样本；输出六字段事件及用例分隔，不复制生产状态布局。
 * @note        仅主机测试，不参与固件构建或访问开发板。
 */
#include "Detector/sound_detector.h"
#include <inttypes.h>
#include <stdio.h>

/**
 * @brief 从标准输入读合成用例，逐帧执行真实内核并输出事件。
 * @return 0表示正常文件末尾；1输入格式/范围非法，2出现意外流状态。
 * @note 每例输入六配置、首帧高/低32位、uint32样本数及PCM16十进制值；最多100000帧，不分配堆内存。
 */
int main(void)
{
    uint32_t values[6],count,first_high,first_low;
    uint64_t first;
    int header;
    while ((header=scanf("%" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
                         " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32,
        &values[0],&values[1],&values[2],&values[3],&values[4],&values[5],&first_high,&first_low,&count))==9)
    {
        first=((uint64_t)first_high<<32)|first_low;
        for (unsigned i=0;i<5U;++i) { if (values[i]>UINT16_MAX) { return 1; } }
        if (count==0U || count>100000U || first>UINT64_MAX-(count-1U)) { return 1; }
        const SoundDetectorConfig config={(uint16_t)values[0],(uint16_t)values[1],(uint16_t)values[2],
            (uint16_t)values[3],(uint16_t)values[4],values[5]};
        SoundDetector detector;
        if (!SoundDetector_Init(&detector,&config)) { return 1; }
        for (uint32_t i=0;i<count;++i)
        {
            int sample;
            if (scanf("%d",&sample)!=1 || sample<INT16_MIN || sample>INT16_MAX) { return 1; }
            SoundCandidate event;
            SoundDetectorResult status=SoundDetector_Push(&detector,7U,first+i,(int16_t)sample,&event);
            if (status==SOUND_DETECTOR_EVENT)
            {
                /* 64位坐标分高低字输出，避免不同MinGW运行库的I64格式差异。 */
                printf("E %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32
                       " %" PRIu32 " %" PRIu32 " %u\n",
                    event.session,(uint32_t)(event.onset_frame>>32),(uint32_t)event.onset_frame,
                    (uint32_t)(event.confirmed_frame>>32),(uint32_t)event.confirmed_frame,
                    event.mean_square_ac,event.peak_abs,event.clipped ? 1U:0U);
            }
            else if (status!=SOUND_DETECTOR_NONE) { return 2; }
        }
        puts("END");
    }
    return header==EOF ? 0:1;
}
