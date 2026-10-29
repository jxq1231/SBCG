# M4.2.4-A 前台耗时分解：测量记录

<!-- doc-status: stage-a-complete-stage-b-not-started; reviewed: 2026-10-26 -->

## 结论

M4.2.4-A 只增加低频、分模块的最大耗时诊断，未改变候选、队列、门限、日志频率、调用顺序或硬件配置。最终 Debug 镜像已获用户授权烧录 A 板并完成约70秒静默采集：

- **旧声音候选服务 `SoundDetectorService_Stage` 最大 21ms**（按8192帧整块逐帧暂算循环计）。
- **UI `UI_TouchGFX_Process` 最大 19ms**（最终修正镜像；先前同镜像原型两次为20ms，不含 `$UI` 日志格式化/发送）。
- Chirp `SCAN_MS` 最大 3ms、`SCORE_MS` 最大 4ms；`GAP_MS` 最大 60–61ms。
- 业务无回归：AUDIO OK=407、DROP/ERR/HAL/DMA=0；CHIRP BAD/SKIP/ERR/DROP=0；SOUND REJECTED/DROPPED/ERRORS=0；LOG_FULL/WRITE/BAD=0；UI ERRORS=0；单次新BOOT、A登记UID；BOOT后完整行跨度69.6秒。
- 解释：在已测模块中，原候选 Stage（21ms）和 UI 更新（19–20ms）是两个最大单次贡献；它们与 GAP 的最大值不一定发生在同一轮，不能直接相加为精确分解。剩余 GAP（约20ms量级）尚未归因到音频统计计算、网络/温度突发或日志格式/发送等模块。

**未验收**：主动标准音频 MATCH、双板同步、声学/测距精度；本次为静默测量增量。

## 实现与字段

| 文件 | 本轮职责 |
| --- | --- |
| `App/Detector/sound_detector_service.c/.h` | `$SOUND_STATE` 追加 `STAGE_MS`、`PROC_MS`；快照增加两个饱和最大毫秒字段 |
| `App/UI/touchgfx_service.c` | `$UI` 追加 `UPDATE_MS`；记录UI前台入口到返回的最大值 |
| `Tests/SoundDetectorService/test_service.c` | 受控tick验证Stage/Process计时、Init清零；验证故障快照仍按原1000ms输出状态行 |
| `Tests/TouchGFXService/test_service.c`、`run_tests.py` | 受控tick使 `UPDATE_MS` 非零并断言；新增timing场景 |

字段口径（必须与实现一起理解）：

- `STAGE_MS`：`SoundDetectorService_Stage` 内8192帧逐帧 `SoundDetector_Push` 循环的最大HAL毫秒；不含入口元数据校验和 `working=current` 96字节拷贝。
- `PROC_MS`：`SoundDetectorService_Process` 入口守卫后到返回的最大HAL毫秒；包含本次状态日志格式化/发送（正常约1ms，日志阻塞时可能更高）。
- `UPDATE_MS`：`UI_TouchGFX_Process` 入口到返回的最大HAL毫秒；**不含** `Report()` 的 `$UI` 格式化/发送，因此与`PROC_MS`的统计区间不同，不能当作同一口径比较。
- Chirp已有字段：`SCAN_MS`、`SCORE_MS`、`GAP_MS`、`DONE_MAX_MS`；其中`GAP_MS`是相邻`ChirpService_Process`入口最大间隔，不是任务总耗时。
- 所有值为uint32无符号差、饱和最大值，1ms分辨率；0表示未跨HAL tick或未发生。计时读取只在原有调用入口/返回处增加固定次数，无逐样本读取。

## 调用链与硬件边界

```text
App_Process()
  → AudioCapture_Process()
      → SoundDetectorService_Stage()   （STAGE_MS）
      → ChirpService_Stage/Complete/Process()（SCAN_MS/SCORE_MS）
      → SoundDetectorService_Process() （PROC_MS）
  → UI_TouchGFX_Process()              （UPDATE_MS）
```

**No CubeMX change required。** 无新GPIO、TIM、IRQ、DMA、SAI、LTDC、ETH或Cache配置；无DMA所有权变化。A镜像沿用固件标识 `SBCG-M4.2-BATCH`，以ELF SHA256区分本轮覆盖镜像；未修改 `app_main.c`。

## 验证

### 主机

```powershell
python -B Tests/SoundDetectorService/run_tests.py
python -B Tests/TouchGFXService/run_tests.py
python -B Tests/SoundDetector/run_tests.py
python -B Tests/ChirpService/run_tests.py
python -B Tests/ChirpMatcher/run_tests.py
python -B Tests/Audio/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/SoundEventTime/run_tests.py
cmake --build --preset Debug --parallel
cmake --build --preset Release --parallel
python -B Tools/Monitor/check_audio_layout.py --preset Debug
python -B Tools/Monitor/check_eth_layout.py --preset Debug
python -B Tools/Monitor/check_audio_layout.py --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

| 项目 | 结果 |
| --- | --- |
| SoundDetectorService | 5组+timing+state log全PASS；故障持续时仍按原1000ms输出`$SOUND_STATE` |
| TouchGFXService | 9场景+timing全PASS；`UPDATE_MS`可产生非零值 |
| 其他回归 | SoundDetector、ChirpService、ChirpMatcher、Audio、App、SoundEventTime全部退出0 |
| 构建 | Debug Flash 275928、Release 185116字节；RAM 317920/327680；仅原RWX LOAD警告 |
| 布局 | 音频/以太网两配置四项通过 |

最终Debug ELF SHA256：`3cca12cef6c47126bb6d223072e66bdbcfe55cbf3f221d92370072c955114365`。

### 静默实机

用户授权后烧录最终Debug镜像，OpenOCD `Verified OK`、复位成功；未改选项字节、未主动发声。采集证据：

| 采集 | 镜像 | BOOT后跨度 | STAGE_MS | PROC_MS | UPDATE_MS | SCAN_MS | SCORE_MS | GAP_MS |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| quiet-m424a-01 | 原型 82035918（含F1，未故障） | 69.6s | 21 | 1 | 20 | 3 | 4 | 61 |
| quiet-m424a-02（续采，无复位） | 同上 | 190s累计 | 21 | 1 | 20 | 3 | 4 | 61 |
| quiet-m424a-03 | 最终 3cca12ce | 69.6s | 21 | 1 | 19 | 3 | 4 | 60 |

最终采集（`quiet-m424a-03`，raw SHA256 `687bc4f82e01bb02d6f628de782732c1b842dea43b7ae1ee3376e007a98b2caa`，183525字节，无非ASCII）：

- BOOT：`$BOOT,FW=SBCG-M4.2-BATCH`，UID `001A002D:30365105:36363631`，单次新BOOT。
- CHIRP_DIAG 34条：SCAN≤3、SCORE≤4、GAP=60；DONE=2、DONE_MAX_MS=157、DROP_MAX_MS=0、丢弃原因全0。
- `$SOUND_STATE`：VALID_BLOCKS=407、REJECTED/DROPPED/ERRORS=0、STAGE_MS=21、PROC_MS=1。
- `$UI`：FRAMES=3614、ERRORS=0、UPDATE_MS=19。
- `$AUDIO`：OK=407、DROP/ERR/HAL/DMA=0、RATE_HZ=48000。
- `$HEARTBEAT`：TX_ERRORS=LOG_FULL=LOG_WRITE=LOG_BAD=0、LOG_HWM=23。
- 证据文件：`Logs/Chirp/quiet-m424a-03-serial.bin`、`-serial.jsonl`、`-summary.json`、`-flash.txt/.json`。

### 前置原型与复核

初版实现被独立复核发现一个阻塞问题 F1：`SoundDetectorService_Process` 在不健康快照分支新增提前`return`，会抑制原有周期`$SOUND_STATE`；已在提交前恢复原控制流，并把计时记录移到原有返回点。同时补了故障状态日志、`$SOUND_STATE`字段、UI数值、tick回绕和最大值保持测试，更新字段口径注释。复核报告：`Logs/Detector/M4_2_4_A/review.json`（首轮FAIL，最终按当前工作树记录为PASS_WITH_RESERVATIONS）。

最终delta复核：F1已消除（测试对旧return变异变红）、Sound 7组与TouchGFX 14场景通过、回绕/最大保持/故障状态日志均有测试；保留项为低风险口径/覆盖：Stage只测逐帧循环（头文件已明确限定）、`$AUDIO,STAT_MS`未实施（GAP仍缺音频统计归因）、`PROC_MS`含日志发送而`UPDATE_MS`不含，不能直接相加比较。

## 归因结论与限制（不夸大）

- 已测最大值中，`SoundDetectorService_Stage`（21ms）和UI更新（19–20ms）明显大于Chirp扫描/评分（3/4ms），且 `SCAN_MS` 与之前B阶段一致；**因此下一个最明确的优化是把原声音候选链迁移到64点批量入口（M4.2.4-B）**。UI（19–20ms）可列为条件增量C1的候选，但需更多样本确认是否为主要GAP来源。
- 这些最大值是不同调用轮次的累计最大值，不能相加；`GAP_MS` 60–61ms 中仍有一部分未归因（音频统计计算、网络/温度、日志格式/发送、其他App模块）。
- HAL毫秒分辨率为1ms；短模块可能显示0，不能据此证明零耗时；测量含SysTick抢占的墙钟时间，不是纯CPU/最坏执行时间。
- 没有主动声音，无MATCH/声学时延结论；没有双板、PPS波形或测距结论。
- 未修改日志频率、队列、搜索或阈值；不把本报告写成性能修复。

## 下一步

- **M4.2.4-B**：原声音候选链64点批量迁移，需按计划另行确认/授权；预期降低STAGE_MS，主机等价测试 + 静默复测。
- **条件C1**：若后续静默样本继续显示UI 19–20ms且为主要GAP来源，再评估分片/降频；不先改显示。
- 主动标准音频验收仍待环境允许发声；本增量不改变该阻塞状态。
