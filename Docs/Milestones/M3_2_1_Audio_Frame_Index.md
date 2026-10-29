# M3.2.1 音频帧编号：正常窗口验收通过

<!-- doc-status: completed-evidence; reviewed: 2026-10-23 -->

> **已归档的增量验收记录。** 仅证明本页所列固件、窗口和测试范围；不是当前待办。后续进展以现行状态页为准，旧版本的未测项和失败不改写为已通过。 当前配置/任务见[项目状态](../Project_State.md)，阅读范围见[文档导航](../README.md)。

日期：2026-10-22。用户已批准[本次单一目标](../Plans/2026-10-22_Audio_Frame_Index_Design.md)。
**Build=PASS；主机测试=PASS；Flash=PASS；Runtime=PASS_WITH_POLLING_JITTER_NOTE。**
本次编号与原业务正常窗口通过；旧codec 2050ms经验警戒未通过，详见实测边界。当前板为SBCG-M3.2-INDEX；验证结果随本增量归档提交，不推进M3.2.2。

## 行为

- 每个8192帧音频块增加本次启动内非零采集SESSION、uint64首帧和已知前台缺口；跳过两个已观察块会留下16384帧缺口，不压缩序号。
- 每次DMA启动前分配新会话，停止后重新启动从第0帧编号；失败尝试可能消耗会话号。跨会话旧token即使其它所有字段相同也拒绝。会话耗尽拒绝启动，不回绕；BOOT后必须重新分段。
- 回调顺序、超过250ms的间隔、uint64溢出、HAL错误锁存FAULTS，仅重新采集会话恢复；正常回调不会擦除故障。
- 保留原DMA覆盖保护和AUDIO幅值观测；每秒新增同SEQ的AUDIO_POS，位置无效则FIRST/COUNT/GAP/CONTIG清零，杜绝沿用旧位置。
- 缺口、编号可信和有效位置的含义分别见[协议](../Protocol.md)。IRQ完整周期可能未被观察，编号不是独立采样时钟证明；本轮不读取PTP或声学到达时刻。

## 改动文件

| 文件 | 原因 |
| --- | --- |
| App/Audio/audio_frame_index.h、audio_frame_index.c | 无HAL依赖、O(1)边界编号与异常锁存 |
| App/BSP/bsp_audio.h、bsp_audio.c | 会话分配、块元数据、成功交付末尾和token校验 |
| App/Audio/audio_capture.c | 同快照位置输出、无效值抑制、有界64位十进制转换 |
| App/app_main.c | 固件版本SBCG-M3.2-INDEX |
| cmake/audio-layout.cmake | 用户拥有的再生成安全扩展，登记新源文件 |
| Tests/Audio/run_tests.py、test_audio_frame_index.c、test_bsp_audio.c、test_audio_capture.c | 编号、溢出、缺口、覆盖、会话及日志测试 |
| Tests/App/test_app_main.c | 版本及原调度回归 |
| Analysis/audio_position_analysis.py、tests/test_audio_position_analysis.py | 原始日志顺序、同会话/序号/帧计数核对和异常拒绝 |
| Docs/Plans/2026-10-22_Audio_Frame_Index_Design.md | 已批准范围、设计、步骤和门槛 |
| Docs/Milestones/M3_2_1_Audio_Frame_Index.md、Project_State.md、Protocol.md | 当前状态、验收准备和字段定义 |

## 调用链及硬件边界

前台：main → App_Process → AudioCapture_Process → BSP_Audio_BeginBlock → AudioStatistics_Compute → BSP_Audio_EndBlock → 每秒AUDIO/AUDIO_POS。
IRQ：原DMA2_Stream7_IRQHandler → HAL DMA → SAI半满/满回调 → AudioFrameIndex_Complete及交接；原SAI2错误回调锁存无效。
IRQ只做有界状态更新，不做串口、I2C、PTP、统计或内存分配；前台短临界区复制64位共享状态。

DMA仍为SAI2-B接收、DMA2 Stream7 Channel0、peripheral→memory、halfword、circular；32768元素，半满/满各8192双通道帧。
原64KiB/32字节对齐SRAM1缓冲区0x20010000..0x20020000不变；I/D Cache仍关闭，无新增缓存维护。
No CubeMX change required。未改.ioc、Core、Drivers、LWIP、生成CMake或初始化；未改变GPIO、TIM、NVIC、ETH、SAI、I2C、ADC或LTDC配置。新增纯计算器通过用户CMake扩展纳入，无生成文件补丁。

## 软件验证与证据

既有构建动作：

```powershell
python -B Tools/Monitor/autodebug.py build --preset Debug
python -B Tools/Monitor/autodebug.py build --preset Release
```

均BUILD_OK、无新增编译/链接warning；Debug FLASH 100848B、Release 60684B，音频DMA64KiB。
冻结ELF SHA256：

- Debug：906f02c0002b05c19f7d25621fe224087fa8a19d71d8a51045e157d72788e782
- Release：2e9ead997b3d58f5541256bfb7edd69eaa8c93fa24b3a40e25ef6b9ee309ddd4

10组原生C模块（Audio含7个测试程序）、工具unittest83项、分析最终53项、两配置ETH/音频布局均通过。
软件证据在Logs/Audio/M3_2_1/software-01：checks.json及16份日志，另有supplemental-checks.json记录最终Audio和Analysis重跑。
构建报告在Logs/AutoDebug/20261022T153017-f278d0d4和20261022T153047-20cb01c3。
测试先确认缺失会话、缺失位置输出和日志顺序契约导致失败，再完成实现；新增纯编号器越过uint32帧边界、uint64溢出、HAL tick回绕等边界均通过。
变异测试仅在Logs源码副本移除session比较，准确触发“仅会话不同”旧token断言；生产源码未动，记录session-mutation.json。
独立只读审查及两项补强聚焦复核均无P1/P2阻断；不将代码复核替代实物测试。

## 已执行的本版本实机联合验收步骤

1. 确认Debug ELF哈希及源码冻结记录，关闭占用COM3的串口页面；使用原ST-LINK配置烧录、verify、reset，不改选项字节。
2. 同步采集约90秒COM3（115200/8N1、DTR/RTS关闭）与原UDP5005、6006、6007观察链。保持PG7/D4温度模块3.3V原接线，无需发声或拔插。
3. 必须从预期版本BOOT开始；至少60对完整且顺序匹配的AUDIO/AUDIO_POS。SESSION不变；OBS_FRAMES/FIRST持续增长并对齐8192，COUNT=8192，GAP/FAULTS/DROP/ERR/HAL/DMA为0，INDEX_VALID/CONTIG/位置VALID为1；H/F/OK与帧范围对应。原采样率健全性范围仍按约47991Hz的±1%。
4. 原UDP正常窗口至少70包无缺口/重复/乱序，原30组RX/TX/ACK、30组ACK/REPORT/四时刻及板端与真实C估算器对应；温度自动更新、codec正常，UART/PTP错误无新增。记录心跳间隔实测值，不把旧窗口1020ms经验值冒充课程硬门槛。
5. 用新检查器核对内容，再联合原始字节、采集实际时长、BOOT/固件哈希及旧业务记录形成实机结论。主机检查器hardware_verified恒false，60行不替代90秒实际运行。
6. 完成后更新本报告和Project_State，以jxq提交验证后的增量，停止在M3.2.1。正常窗口不能代替人为注入故障的实物验证，异常/重新启动/溢出目前仅为主机测试证据。

## 实机结果（用户授权后单次烧录）

2026-10-22收到本版本明确授权，原ST-LINK配置Programming/Verified OK/Reset全部成功，未改选项字节。烧录后采集90.090823秒，全程约93.298秒；COM3与三个UDP子进程均正常关闭，没有追加复位、发声、改线或拔插。

| 检查项 | 本次观察 |
| --- | --- |
| 新版BOOT | SBCG-M3.2-INDEX，HCLK=200MHz，UID=001A002D:30365105:36363631 |
| 帧位置 | 89对AUDIO/AUDIO_POS、SEQ1～89、SESSION=1；全部位置有效、CONTIG=1、GAP/FAULTS=0 |
| 采集计数 | H/F各261，累计522块、4276224帧；最后FIRST=4268032、COUNT=8192，DROP/ERR/HAL/DMA=0 |
| 帧率健全性 | 47953～48018Hz；不是精密采样率标定 |
| 原逐秒UDP | 80包SEQ6～85、缺口/重复/乱序/非法/外来包为0；逐包tick与串口对应，步长992～1008ms |
| 原请求/硬件时间戳 | 30组RX/TX/ACK完整，对应板端统计无错 |
| 时间交换/板端估算 | 30组ACK/REPORT/四时刻、30个板端结果与真实C内核逐项一致；仍LOCAL/SYNC=0 |
| 温度 | 17个连续版本，25.3～25.4℃、声速346632～346692mm/s；全部温度错误0，最终AUTO/DHT11有效 |
| codec | 44次ID=8994，错误0，READ_MS=0～1ms；轮询间隔2000～2051ms |
| 心跳/PTP | 心跳89条、PTP90条各连续，发送/PTP错误0；心跳间隔1000～1052ms，平均1001.818ms |
| 原始证据 | 串口133253字节与1421行JSONL逐字节重构一致，无残尾；新BOOT前11条旧版记录仅解释时排除，原始文件不改 |

### 轮询抖动与严格检查失败

复用旧M3.1严格联合脚本时，codec SEQ2→3的TICK_MS为4110→6161，间隔2051ms，超过旧2050ms经验门槛1ms；原脚本、失败回放和stderr均保留在runtime-01/strict-analysis-original.py、strict-replay.py、strict-analysis-failure.json/.txt。
一次直接重跑亦遇到native输出已存在保护；未删除/覆盖原native产物，后续严格回放与需求范围分析分别使用独立新路径。

现有codec_monitor契约为前台至少2000ms且允许DHT起始低电平避让，没有2050ms精密上限。本次已批准方案要求codec正常和音频/网络连续，不规定精密前台轮询周期。
另存的需求范围分析保留所有编号、幅值、硬件ID、错误、温度和网络/时间戳条件，并完整记录周期，不将旧门槛改成更大的数值冒充原检查通过。
心跳最大1052ms也超过旧1020ms经验比较值；两项均如实记录，不以其作为测量时间精度证据。现有数据不足以归因到某个服务的具体耗时，未宣称根因已定位。
独立复核重算原始字节、帧位置、网络与真实C结果后，确认这两项前台轮询抖动不阻断本次M3.2.1编号目标。

### 证据与最终边界

全部原始捕获及派生报告位于Logs/Audio/M3_2_1/runtime-01：runtime-run.json、capture-serial.bin/.jsonl、三路UDP JSONL、flash日志、acceptance.json、position-window-check.json、requirements-comparison.json。
serial-window.jsonl是从唯一新版BOOT开始的派生窗口；源哈希、排除行数和窗口哈希记录于serial-window-provenance.json，未改原始证据。六种基于本次真实捕获的负例（无效编号、缺口、旧位置、溢出、会话变化、原AUDIO丢块）均被检查器拒绝。
独立审查重新核对133253字节、全部89对位置、80包及两组30请求/真实C参考，未发现P1/P2阻断。软件/生成配置及两种ELF仍与授权冻结记录一致。

最终：本次编号与原业务正常窗口通过，旧codec 2050ms经验警戒未通过并保留。异常注入、重新采集/跨会话及溢出目前为主机测试证据，未人为触发实物故障；未验证PTP样本时间映射、精密到达时间、事件检测、双板同步或测距。当前正常运行并关闭主机采集端口，提交后停止在M3.2.1。
