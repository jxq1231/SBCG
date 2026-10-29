/**
 * @file        temperature_service.h
 * @brief       协调DHT11采集、温度声速有效性和手动模式。
 * @details     声明本模块接口、数据类型或配置常量。前台状态机跨主循环维持启动低电平，再调用BSP限时采帧；首个合格帧仅预热，后续发布温度与声速并输出日志。
 * @note        需UID白名单与非空日志；自动值须fresh且未满15秒，失败保留旧数据但撤销有效性，手动值至AUTO或重新初始化有效。
 */
#ifndef SBCG_TEMPERATURE_SERVICE_H
#define SBCG_TEMPERATURE_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief   前台日志输出回调类型。
 * @param   line 只读日志字节，仅回调期间有效；返回前须消费或复制，不可保留指针。
 * @param   length 有效字节数，不含NUL；当前发送路径仅传正数。
 * @return  无返回值；传输错误由应用或BSP统计。
 * @note    当前应用接入SendLine，同步发送或复制入有界队列，不从IRQ调用。
 */
typedef void (*TemperatureLog)(const char *line, int length);

/**
 * @brief   温度服务面向前台消费者的值快照。
 * @note    valid为使用许可；version标识发布版本，age_ms是距发布的毫秒差，temperature_milli_c单位毫摄氏度，sound_speed_mm_s单位毫米每秒。
 *          valid=false时仍可能携带旧值，manual表示来源模式，不表示数值有效。
 */
typedef struct
{
    bool manual;
    bool valid;
    uint32_t version;
    uint32_t age_ms;
    int32_t temperature_milli_c;
    uint32_t sound_speed_mm_s;
} TemperatureSnapshot;

/* Main-loop only. Sampling is enabled only for the development UID allowlist
 * and a non-NULL logger. Reinitialization releases an owned start-low pulse. */
/**
 * @brief   复位服务并按UID白名单决定是否初始化传感器BSP。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   uid_words 只读3字芯片UID；NULL禁用，调用内比较，不保存指针。
 * @param   log 日志回调，NULL禁用服务；须返回前消费或复制字节。
 * @return  无返回值。
 * @note    应用前台初始化；重入初始化先中止本服务持有的低电平，BSP初始化失败仅计一次且不自动重试初始化。
 */
void TemperatureService_Init(uint32_t now_ms, const uint32_t uid_words[3],
                             TemperatureLog log);
/**
 * @brief   推进启动低电平、限时采帧、发布和日志状态机。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @return  无返回值。
 * @note    App_Process重复调度，非精确周期；至少20毫秒后捕获，超过30毫秒中止；上电至少1秒后首次尝试，后续至少5秒间隔。
 */
void TemperatureService_Process(uint32_t now_ms);
/* Main-loop query: application UART output must wait until DATA is released. */
/**
 * @brief   查询服务是否正持有DHT11启动低电平。
 * @return  启动低电平尚未捕获或释放时返回true。
 * @note    仅主循环查询，供输出和诊断门控；不表示全部传感器或IRQ活动。
 */
bool TemperatureService_IsStartLowActive(void);
/* Main-loop only; mode changes reject an owned start-low pulse. Manual values
 * remain valid until AUTO/reset, even if sensor initialization failed. */
/**
 * @brief   发布手动温度与声速并递增版本。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   temperature_milli_c 毫摄氏度手动温度，超配置范围拒绝。
 * @return  成功返回true；未启用、启动低电平中、版本耗尽或温度无效时false且不改服务状态。
 * @note    仅前台，不要求传感器初始化成功；湿度清零不是测量值，手动值持续至AUTO或重新初始化。
 */
bool TemperatureService_SetManual(uint32_t now_ms, int32_t temperature_milli_c);
/**
 * @brief   切回自动模式并撤销手动值及预热状态。
 * @return  启用且未持有低电平返回true；未启用或忙时false。
 * @note    仅前台；已在自动模式则不改状态，真正切换后重新预热才发布自动值。
 */
bool TemperatureService_UseAuto(void);
/**
 * @brief   复制温度、声速、版本及当前有效性。
 * @param   now_ms 当前HAL毫秒计数；使用无符号差处理常规32位回绕。
 * @param   out 可写快照；NULL直接返回，非NULL先清零；有历史值时可携带valid=false的旧温度。
 * @return  无返回值。
 * @note    仅前台；A本地值优先；B本地DHT禁用时使用15秒内的A共享值；调用方先判断valid。
 */
void TemperatureService_GetSnapshot(uint32_t now_ms, TemperatureSnapshot *out);

/**
 * @brief   B板接收A板共享温度，更新远程温度缓存。
 * @param now_ms 接收时的HAL毫秒计数，使用无符号差处理回绕。
 * @param temperature_milli_c 共享毫摄氏度，必须落在配置范围内。
 * @param sound_speed_mm_s 共享声速，必须大于0且与本地整数公式一致。
 * @param version A板温度版本，必须非0；旧版本在缓存仍新鲜时拒绝。
 * @param manual A板来源是否为手动设温。
 * @return true表示已应用；false表示范围/声速/版本/角色拒绝。
 * @note 仅B板使用；该接口不使能DHT、不改变A板本地温度，不代替温度版本一致性校验。
 */
bool TemperatureService_SetRemote(uint32_t now_ms, int32_t temperature_milli_c,
                                  uint32_t sound_speed_mm_s, uint32_t version,
                                  bool manual);

#endif
