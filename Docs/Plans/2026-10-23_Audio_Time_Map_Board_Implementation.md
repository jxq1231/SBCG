# M3.2.4 板端时间估算接入

<!-- doc-status: archived-plan; reviewed: 2026-10-23 -->

状态：**已完成·历史方案（2026-10-23）**。本轮软件、构建/布局、独立复核及用户授权的单次Debug烧录/120秒实物验收均通过，见[验收报告](../Milestones/M3_2_4_Audio_Time_Map_Board.md)。40组真实C/Fraction一致、最大绝对留出残差14.884us。基线ef9f0b3。以下保留本轮设计与验收约定，不作为下一版本烧录授权，不重复实施。

## 设计与文件

- App/Audio/audio_time_anchor.h/.c：新增AudioTimeAnchorObservation（sequence、captured_ms、AudioTimeMapAnchor）及一次领取TakeObservation。每个尝试覆盖单槽，成功保存有效整数快照，失败保存valid=false且位置/时间清零；Init清槽。仅前台使用，无IRQ/日志解析。
- App/Audio/audio_time_map_monitor.h/.c：Init/Process，Process在DHT start-low期间不领取、不计算/输出。领取后拒绝年龄>250ms、seq不连续、会话改变、无效/倒序锚点，清空未完成窗口；有效当前锚点可成为重建第一个点。每3个连续有效锚点只做一次：用第1/3点调用原AudioTimeMap_Estimate估算第2点的整数相位中点；不修改纯C内核或参数。中点按lo+(hi-lo+1)/2，避免大绝对编号相加溢出。
- App/app_main.c：在AudioTimeAnchor_Init后初始化服务，在Anchor_Process后调度；新功能组合标识SBCG-M3.2-MAP。cmake/audio-layout.cmake用户扩展登记core/monitor及SBCG_USE_AUDIO_MAP宏；无组合时保留旧分支。
- Tests/Audio/test_audio_time_anchor.c、test_audio_time_map_monitor.c、run_tests.py：先红后绿验证单槽、失败清零、窗口/失败/重建、DHT、tick/seq wrap、格式最宽值与残差符号；Tests/App/test_app_main.c/run_tests.py验证顺序和门控。
- Analysis/audio_anchor_analysis.py增加可选expected_firmware，旧默认保持；新audio_time_map_board_analysis.py及tests核对新版BOOT、原音频位置/锚点、每个MAP/CHECK与真实C及Fraction逐项一致。
- Docs/Protocol、Project_State、README、Document_Status、Roadmap及本轮Milestone记录软件/实物边界。

## 调度与输出

调用链App_Process → Anchor_Process → MapMonitor_Process → TakeObservation → AudioTimeMap_Estimate → 原SendLine。三点观测只保存CPU整数副本，不读PCM或保留DMA指针；不是后续检测事件的完整缓存/查询API。

正常每3个锚点输出一对ASCII/CRLF，先AUDIO_MAP后AUDIO_MAP_CHECK，单行<256字节。group SEQ独立uint32模递增，L/M/R是来源锚点seq，SESSION uint32，FRAME/EST_NS/LO_NS/HI_NS/OBS_NS uint64十进制，RES_NS为有符号纳秒。64位有界转十进制，不使用nano的%lld。成功例只表示字段格式：

```text
$AUDIO_MAP,SEQ=1,STATUS=OK,CORE=0,VALID=1,SESSION=1,L=1,M=2,R=3,FRAME=95983,EST_NS=2000000000,LO_NS=1999979000,HI_NS=2000021000,SYNC=0
$AUDIO_MAP_CHECK,SEQ=1,OBS_NS=2000005000,RES_NS=5000,SYNC=0
```

失败只输出SEQ/STATUS/CORE/VALID=0/SRC/SYNC=0，不保留session、frame、时间或残差；没有CHECK。STATUS为ANCHOR、STALE、SEQUENCE、SESSION、ORDER、CORE；CORE仅纯内核失败取1..8，其他为0。重建前2点静默，不虚构结果。失败和成功都消耗group序号；最多每个新源尝试一条失败或一对成功，不重试、不突发追赶。

三点模型、宽度/率/窗口和量化继承M3.2.3。残差定义观测中间PTP−目标坐标估计；未知相位/codec/FIFO延迟不在模型区间内。仍LOCAL/SYNC=0，不宣称声学时刻、同步或课程精度。

## 资源和验收

No CubeMX change required。无GPIO/TIM/NVIC/SAI/I2C/ETH/ADC/LTDC/Cache配置修改，无新IRQ/DMA，原SAI2-B DMA2 Stream7 Channel0 halfword circular/64KiB/IRQ4/0保留。原锚点取时与DMA回调上下文不变。

- [x] 测试先失败，再实现CPU快照和monitor，原模块测试通过。
- [x] 主机检查器及坏配对/残差/旧BOOT/丢行测试先失败，再实现；旧Analysis默认兼容。
- [x] Tools/Analysis与10组模块回归、Debug/Release及两种ETH/audio布局通过，无新warning；独立复核或明确未完成范围，核对保护文件哈希。
- [x] 冻结新Debug/Release ELF和源码，在用户批准后沿原ST-LINK配置单次program/verify/reset；不改选项字节。
- [x] 约120秒COM3与三路UDP正常窗口：≥90有效锚点、≥30 MAP/CHECK、所有板端数值与真实C/Fraction一致；残差绝对值≤100000ns仅作经验健全性检查。≥90对音频位置无drop/error，原UDP110秒≥100零缺口（烧录前criteria收紧，实际110）、两组30请求及板端原估算/真实C对照、自动温度和codec无新错误；记录实际前台周期，不以旧精密经验上限冒充硬要求。
- [x] 实机通过后更新状态、以jxq提交并正常推送；交付提交由Git历史记录。

原始日志放Logs/Audio/M3_2_4，不覆盖旧数据；无需发声、改线、拔插或第二板。当前不启用LCD/TouchGFX。本轮新版本已取得明确授权后执行；历史许可不自动延续到下一版本（依据AGENTS第20节）。
