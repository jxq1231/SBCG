# M4.2.3-B 扫频批量接入：静默实机验收记录

<!-- doc-status: stage-b-silent-pass-active-sound-pending; reviewed: 2026-10-26 -->

## 结论

M4.2.3-B 按计划把 Chirp 扫描从逐帧 `SoundDetector_Push` 迁移到 `SoundDetector_PushBatch`，功能提交 `743e027`。最终 Debug 镜像已获用户授权烧录 A 板并完成约 70 秒静默采集：

- **SCAN_MS 最大 3ms**（DIAG 历史基线 59ms，目标 ≤10ms），34 条诊断全部 ≤3ms；SCORE_MS 最大 4ms；GAP_MS 最大 106ms（历史 172ms）。
- 环境自然候选 CAND=32，全部完成搜索 DONE=32、PENDING=0；TIMEOUT/HISTORY/FULL/RANGE/RESET 全 0，CHIRP BAD/SKIP/ERR/DROP 全 0。
- 已接纳任务完成时延 `DONE_MAX_MS=831ms`（≤1000ms 工程目标）；`DROP_MAX_MS=0`。该值从关联块 `completed_ms` 起算，含缺尾等待、缓存、调度和处理，不是纯计算耗时。
- 原业务正常：AUDIO OK=407、DROP/ERR/HAL/DMA 全 0、RATE_HZ=48000；HEARTBEAT `TX_ERRORS/LOG_FULL/LOG_WRITE/LOG_BAD` 全 0、LOG_HWM=24；UI ERRORS=0；单次 BOOT、A 登记 UID。
- **主动标准音频匹配未验收**：用户说明当前环境不能主动发声；一次 PC 扬声器试探回放未做 BOOT 同步、未确认可闻且未产生 MATCH，不作为声学失败或通过结论。MATCH 10/10 仍待用户方便发声时单独验收。
- 主机候选频率压力与日志突发合成调度测试尚未实施（计划 §5.3）；本窗口只有环境自然候选负载。

## 目标与范围

只迁移扫频扫描：完整64点窗走 M4.2.3-A 已验证的批量入口，其他输入仍由原逐帧回退；候选门控、有限搜索、两套阈值（原候选256/128、扫频128/64）、Begin/End 事务、8窗/轮、257窗/候选、1500ms 期限、4任务、32行日志队列和日志调度不变。原 `SoundDetectorService_Stage`、SOUND_TIME 及能量候选链未迁移。

## 文件与职责

| 文件 | 本轮职责 |
| --- | --- |
| `App/Detector/chirp_service.c` | `Scan()` 使用 `SoundDetector_PushBatch`，按 `consumed` 推进 `scan_offset/last_scanned`；异常计数并清历史退出；任务完成/丢弃时延测量 |
| `App/Detector/chirp_service.h` | 快照增加 `max_complete_ms/max_drop_ms`；扫描预算注释改为“每次最多推进帧数” |
| `App/app_main.c` | 仅固件标识改为 `SBCG-M4.2-BATCH`（该文件其余PPS改动属既有用户工作，未混入功能提交） |
| `Tests/ChirpService/test_scan_batch.c` | 新增 `-finstrument-functions` 钩子测试：每8192帧块128次批量推进、0次逐帧回退，跨块候选/任务保持 |
| `Tests/ChirpService/test_service.c` | 测试时钟在调用后推进，使内部tick读与入口一致；行为断言不变 |
| `Tests/ChirpService/test_diagnostics.c` | 校验新DIAG字段、完成32ms/过期1500ms/Complete历史10ms/Reset 7ms 时延 |
| `Docs/Protocol.md`、本报告、计划/进度/入口状态 | 接口与实测边界同步 |

## 实现摘要

- **批量扫描**：每次取 `available=min(64, 8192-scan_offset)`，`SoundDetector_PushBatch` 返回 `consumed` 为1或64；只有 `consumed>0 && consumed<=available` 才推进，否则计ERR并清历史返回，避免死循环。预算仍为每块8192帧；块边界连续且门控窗已清空时走快速路径，其余自动回退1点。
- **时延诊断**：完成/丢弃时实时读取HAL tick，减去任务关联块 `completed_ms`（uint32无符号差），分别累计 `DONE_MAX_MS/DROP_MAX_MS` 饱和最大值。Complete内DROP_HISTORY、End失败RESET清队列、Search内EXPIRED/HISTORY也计入；未入队的FULL候选没有任务起点，不计入。日志频率仍沿用原1Hz DIAG/STATE交替，不新增逐任务日志。
- **未改**：gate配置 `{64,128,64,2,8,24000}`、8窗/轮、257窗/候选、1500ms、4任务、32行队列、SAI2/DMA缓冲所有权、缓存配置、CubeMX生成文件。

**No CubeMX change required。** 无新GPIO、TIM、IRQ、DMA、SAI、LTDC、ETH或Cache配置；无新DMA关系。

## 调用链与硬件边界

```text
App_Process()
  → AudioCapture_Process()
      → BSP_Audio_BeginBlock()/ChirpService_Stage()/BSP_Audio_EndBlock()/ChirpService_Complete()
      → ChirpService_Process()
          → Scan()  → SoundDetector_PushBatch()（完整64点，回退原Push）
          → Search()→ ChirpWindow_Score()
```

原 `SoundDetectorService_Stage` 仍在同一 Begin/End 内逐点处理，未迁移；没有新IRQ/DMA；未通过End的数据仍不发布。

## 主机验证

```powershell
python -B Tests/ChirpService/run_tests.py
python -B Tests/SoundDetector/run_tests.py
python -B Tests/SoundDetectorService/run_tests.py
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

| 验证项 | 结果 |
| --- | --- |
| ChirpService O0/O2 | 服务行为、跨块、End拒绝/覆盖、预算全PASS |
| 批量路径证明 | `test_scan_batch` PASS：2×8192帧，每块128次PushBatch、0次逐帧Push，候选与任务保持 |
| 真实20秒WAV | 10/10精确位置，64与5 calls/171ms两调度均PASS |
| 诊断时延 | 完成32ms/过期1500ms/Complete历史10ms/Reset 7ms 语义PASS |
| 其他6组回归 | SoundDetector、SoundDetectorService、ChirpMatcher、Audio、App、SoundEventTime全部退出0 |
| Debug构建 | Flash 275400字节（DIAG 274568，+832）；RAM 317920/327680；仅原RWX警告 |
| Release构建 | Flash 184924字节（DIAG 184188，+736） |
| 布局检查 | 两配置音频/以太网四项均通过 |

新增Flash主要来自批量入口本体、时延诊断及CHIRP_DIAG字段；RAM不变。

## 独立复核

复核者独立读代码、`git show HEAD`对比、重跑测试，并自建差分与探针，结论 **PASS_WITH_RESERVATIONS / code PASS / 无blocker或major**：

- HEAD逐帧服务 vs 工作树批量服务：O0/O2各282行状态/事件/日志轨迹完全一致；真实WAV逐步轨迹7761/791行完全一致；旧/新实现均10/10。
- 钩子探针确认两个入口地址不同、无嵌套误计；`test_scan_batch` 对HEAD逐帧服务变红、对批量实现变绿；诊断测试对新字段变红/变绿。
- 时延探针8场景PASS（完成32、过期1500、Complete历史10、Reset 7、FULL排除、回绕等）。
- **唯一minor**：完成时延终点原为Process入口tick，可能少计本调用内扫描/评分时间。提交前已改为在完成/丢弃分支实时读取tick，并补Complete历史/Reset两条回归断言；修正后重跑主机测试、重建并重采静默证据。其余为info（异常清历史路径才读tick、HEAD遗留不可达DROP_RANGE、原测试只覆盖部分时延分支等），前两项已在注释/测试中处理。
- **修正后delta复核PASS**：复核者逐blob核对功能提交`743e027`的8个in-scope文件与磁盘一致；确认Search内4处实时tick读取有界（Scan内0次、正常扫描每块新增0次、仅任务终结/清历史时读取）、快照88→96字节、CHIRP_STATE格式未变、DIAG格式11符/11参数/最坏220<256，并重跑差分/真实WAV/时延探针全部通过。delta报告：`Logs/Detector/M4_2_3_B/review-delta.json`。
- 复核未做ARM构建/烧录；本报告的构建与实机结果由本轮另行执行。

报告：`Logs/Detector/M4_2_3_B/review.json`；差分/探针证据：`Logs/Detector/M4_2_3_B/review-fuzz/`。

## 实机静默验收

用户授权烧录后，仅 A 板执行一次最终镜像烧录，未改选项字节、未主动播放声音（用户说明环境不能发声）。烧录前启动COM3采集，OpenOCD `program ... verify reset exit` 退出0，`Verified OK`，复位成功。

| 项目 | 结果 |
| --- | --- |
| 固件/BOOT | `$BOOT,FW=SBCG-M4.2-BATCH`，单次BOOT，A UID `001A002D:30365105:36363631` |
| 采集窗口 | 85.0秒采集；BOOT后完整行跨度69.567秒（≥60秒） |
| CHIRP_DIAG | 34条；SCAN_MS最大3、SCORE_MS最大4、GAP_MS最大106；DONE=32、DONE_MAX_MS=831、DROP_MAX_MS=0 |
| 丢弃原因 | TIMEOUT/HISTORY/FULL/RANGE/RESET全0 |
| CHIRP_STATE | OK=401、BAD=0、CAND=32、MATCH=0、DROP=0、SKIP=0、ERR=0、EVAL=8224、COPY_MS=1、WORK_MS=7、S_MAX=8192、E_MAX=8、PENDING=0 |
| HEARTBEAT | SEQ=68、TX_ERRORS=0、LOG_FULL=0、LOG_WRITE=0、LOG_BAD=0、LOG_Q=5、LOG_HWM=24 |
| AUDIO | RUN、OK=407、DROP/ERR/HAL/DMA=0、RATE_HZ=48000、VALID=1 |
| UI | RUNNING、FRAMES=2591、ERRORS=0、DISCARDS=0 |

`DONE=32` 来自环境自然候选，全部完成、无积压；无标准音频刺激，MATCH=0不代表识别质量。SCAN_MS=3ms不包含串口格式化/发送或其他模块，也不是严格最坏执行时间；DONE_MAX_MS=831ms包含缺尾等待和调度，不是声学到达精度。

证据（Logs由Git忽略）：`Logs/Chirp/quiet-batch-02-serial.bin`（189370字节，SHA256 `70cdf126309c02179592d07206d53aa2e614a618a289442b053b07e0a0403763`）、`-serial.jsonl`、`-summary.json`（`PASS_SILENT_SCAN`）、`-flash.txt/.json`。完整性：raw与逐行投影逐字节一致、无非ASCII完整行、末尾63字节残片按原样保留并排除。

### 主动声学（未验收）

当前环境不能主动发声，用户已明确说明。因此未执行计划 §5.2 的标准20秒/10次文件验收；MATCH 10/10、漏检/重复和声学完成时延仍待验。此前一次无BOOT同步的PC扬声器试探回放未产生MATCH，且未确认可闻，不纳入结论。不能把本次静默结果写成标准音频识别或声学到达精度通过。

## 已知限制与未验证边界

- 主动标准音频回放、双板配对、外部同步/PPS波形、距离/方向、物理到达精度均未测。
- 只有A板；交换机和B板不在本次范围；网络在静默窗口正常（HEARTBEAT/UDP/PTP行持续），未做网络专项。
- 主机合成压力（候选频率、172ms间隔+日志突发）尚未实施；静默自然候选32次/69.6秒，不能代表课程标准音频的候选压力上限。
- 块尾不足64帧的逐点回退、consumed异常守卫、任务级DROP_RANGE在当前配置下不可达或仅静态论证；未做故障注入。
- 板端计时为HAL毫秒；不能证明微秒级实时预算或最坏执行时间。
- 烧录镜像由包含既有用户PPS/CubeMX未提交改动的工作树构建；功能提交`743e027`只含本增量代码，不含这些既有改动，检出纯提交构建的镜像不能冒充本次实测镜像。

## 下一步

1. 待用户环境允许发声时，按计划 §5.2 播放标准20秒/10次文件，核对 MATCH 10/10、DONE/丢弃/完成时延及原业务共存；MATCH 不通过时单独定位声学，不篡改资源通过结论。
2. 主机补候选频率压力与日志突发合成调度测试（§5.3），报告可持续处理能力。
3. 不延长1500ms期限、不缩小搜索范围、不跳窗；后续增量另行授权。本增量到此为止。
