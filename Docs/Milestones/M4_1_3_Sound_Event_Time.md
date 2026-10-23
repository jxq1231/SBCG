# M4.1.3 本地PTP传输模型与串口突发修复

<!-- doc-status: verified-model-and-quiet-pipeline; reviewed: 2026-10-24 -->

本轮完成候选FIRST到本地PTP模型的关联，并修复新增周期日志使PCM归还超时的问题。用户2026-10-24明确不能进一步使用声音测试；之后未播放任何测试音。当前A板为 **SBCG-M4.1-TIME-R1**。静默表示未主动播放声音，不表示房间无环境声。

## 语义与调用链

FIRST为从0开始的PCM双声道组帧号，查询坐标为`FIRST+1`，即该组完整传输后的进度。时间仅来自已有AudioTimeMap_Estimate的线性本地PTP模型，不是物理采样/声学到达时刻。uint64帧/纳秒用十进制输出，LOCAL=1/SYNC=0；模型区间不包含codec/SAI/FIFO、声传播或同步误差。

`main → App_Process → AudioCapture唯一Begin/End → SoundDetectorService归还后出队 → SoundEventTime_Submit`；锚点由App唯一TakeObservation后同副本分发给原AudioTimeMapMonitor_Observe与SoundEventTime_Observe，最后SoundEventTime_Process查询并排队日志。保留4锚点/4候选，2500ms候选超时，末锚年龄>1500ms拒绝，250ms至多一结果、1秒至多一状态。失败仅身份/原因/CORE，不沿用时间；队满显式累计DROPPED。

任何采集缺口/健康故障/会话变化或校时/调频代际都撤销旧配对。代际撤销先于可失败的PPS/UDP清理，即使安全停止失败也不会继续发布旧模型。无新IRQ、DMA或外设配置；DMA2 Stream7/Channel0环形SAI接收、64KiB音频缓冲及ETH布局不变；I/D Cache仍关闭。7份CubeMX生成配置/文件哈希保持。

## 文件

- `App/Detector/sound_event_time.c/.h`：有界查询、失效、结果与状态。
- `App/Detector/sound_detector_service.c`：已验证且已发布的元数据交接；流边界通知。
- `App/Audio/audio_time_map_monitor.c/.h`：原三点自检的副本入口，保留旧Process包装。
- `App/app_main.c`：单次锚点分发、快照后取tick、TIME-R1标识及仅本组合的32行日志队列。
- `App/Sync/time_sync.c`：在所有时钟边界先撤销事件模型；已有PPS改动另行保留。
- `cmake/audio-layout.cmake`：用户自有源列表和SBCG_USE_SOUND_TIME宏，不改生成CMake。
- `Tests/SoundEventTime/test_event_time.c`、`test_app_time.c`、`test_clock_time.c`、`reference_bridge.c`、`reference_check.py`、`run_tests.py`：真实模块/应用/协调器、独立数学参考。
- `Analysis/sound_event_time_analysis.py`、`Analysis/tests/test_sound_event_time_analysis.py`：显式启动边界、原始数据一致性及静默/主动刺激分层验收。
- 本报告、`Docs/Plans/2026-10-23_Sound_Event_Time.md`、协议与现行状态入口：范围、结果、历史失败和后续显示准备。

## 修复及分版证据

| 窗口 | 结果 | 边界 |
| --- | --- | --- |
| TIME / trial-01 | 第二个明确人工准备BOOT后89.519秒，6候选均有模型且与Fraction逐项相同；5次短音各有成功模型；音频归还拒绝5次 | **FAIL_OR_INCOMPLETE**原样保留。两次准备BOOT在0.502秒内，由烧录复位及额外确认复位导致，分析显式指定2次准备启动，不自动删重启 |
| TIME-R1 / quiet-R1-01 | 首次静默采集75秒含烧录/准备时间 | 短窗口辅助记录，非最终验收；没有主动刺激 |
| TIME-R1 / quiet-R1-02 | 85秒原始采集，单次明确BOOT后**67.933秒**，66条音频RUN/VALID，395有效块、DROP/ERR/HAL/DMA0；66条位置连续、GAP/FAULTS0；66条心跳序号连续、TX_ERRORS0 | **PASS_QUIET_PIPELINE_SCOPE**，没有播放声音 |

归还失败发生在原串口同步集中发送场景。R1在SBCG_USE_SOUND_TIME组合中复用现有队列扩大至32行，所有SendLine统一排队，初始化及每App_Process最多排空一行；其他构建组合仍用原策略。真实App测试检查单轮写次数、DHT低态不写及单次锚点分发。修复未改DMA、IRQ或检测阈值。

最终静默窗口存在10个自然环境候选，来源未标注：9条成功模型的EST/LO/HI与独立Fraction逐项相同；启动早期第1条因NO_LEFT明确拒绝、无时间字段。末计数QUEUED10/OK9/REJECT1/DROPPED0/PENDING0；原三点MAP/CHECK22组继续运行。不能将自然环境候选当作标准音频检测成功或误报率测量。PPS末EMITTED67/MISSED0/ERRORS0仍仅寄存器证据；无网线，温度UNREADY/TIMEOUT，未宣称网络/温度本版联合实测通过。

原始数据`Logs/Detector/M4_1_3/`。最终原始串口SHA256 `2bc0bb56e61bfa5dc5619747da9e027638a6265d5a7983df6ba5c871e3414104`。BOOT前344行仅为准备，BOOT后1438完整行ASCII、原始二进制与JSON逐行相符，无尾部残片。没有改写失败数据。

## 软件与冻结版本

- `python -B Tests/SoundEventTime/run_tests.py`：生命周期、等待右锚、FIRST+1、队列/限速、无左锚、过期、时钟/流/会话/缺口、整数/回绕通过；64组真实C服务/Fraction对照（含2^53以上帧及0..4相位宽度）一致；真实App和协调器集成通过。
- 原Audio11组合、App、ClockEpoch及PPS集成回归通过。Tools115项、Analysis121项通过；新增分析11项反例包含丢块、旧状态、重复候选、错误模型、未来源锚、额外BOOT及失败时间泄漏。
- 原CMake Debug/Release预设构建及两种ETH/音频布局检查通过；无新增未解释诊断。核心/边界代码经独立复核；R1排队及静默分析完成本地差异/测试复核，未额外声称独立复核已覆盖R1。
- 冻结 `build/Firmware/SBCG-M4.1-TIME-R1/manifest.json`，110输入哈希与实测源码相符。Debug SHA256 `789c0c92ef2f9b9c6da0020c0cbfa3d4714fab071e377bfcad7593f06041e6fd`，Release `732dc4fc964689196143fd35aacfd60c4c1d3a2c068768f51b69ff9d972e8ee5`。精确镜像包含工作区原有PPS快照，不能仅从检测增量提交推定完整冻结树。
- 按已有授权使用原ST-LINK配置，先读UID确认A，再`program {build/Firmware/SBCG-M4.1-TIME-R1/SBCG-M4.1-TIME-R1-Debug.elf} verify reset exit`；Verified OK，不改选项字节。静默最终窗口仅另作一次复位，无重复烧录。

本增量的软件关联与静默运行范围完成。R1受控短音、标准音频识别、声学校准、双板±10us/PPS波形和测距仍未验收；上一M4.1.2背景候选失败不因本轮模型通过而变绿。用户已改为先推进LTDC RGB565状态页，后续再迁移TouchGFX，显示硬件须走CubeMX。
