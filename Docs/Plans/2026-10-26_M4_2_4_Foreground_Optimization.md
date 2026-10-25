# M4.2.4 前台耗时分解与原声音候选批量迁移方案

<!-- doc-status: stage-a4-complete-c2-pending; reviewed: 2026-10-26 -->

方案始于2026-10-26。M4.2.3-A/B已完成；M4.2.3-B主动标准音频验收因当前环境不能发声暂缓，本方案只安排无需主动发声即可验收的软件增量。

**目标：** 在不改变候选、门限、搜索、日志和时序语义的前提下，先定位并降低音频前台剩余耗时，为日后标准音频与双板验收留出稳定余量。

**当前阶段：** A/B/A2/A3已完成；A4完成对照基线+音频统计优化（STAT15→8–9ms、AUDIO27→18ms、LOOP40→34ms），当前最大单项转为OTHER/FLUSH≈15ms。下一步评估C2日志策略；C1 UI继续暂缓。主动标准音频仍因环境不能发声阻塞。

**Architecture：** 先增加只读、低频的分模块耗时分解；证据确认后，用与M4.2.3-A相同的逐字段等价方法把原声音候选链迁移到64点批量入口；UI、日志和搜索只在各自证据指向时另立增量。

**Tech Stack：** 现有C11、ARM GCC/CMake、原生GCC O0/O2测试、COM3静默采集；不新增RTOS、DSP库或构建系统。

## 规划进度

- [x] 复核M4.2.3-B静默实测、主机合成压力和仍未验收的边界。
- [x] 确认“不能主动发声”条件下可独立验收的下一批工作。
- [x] 划分M4.2.4-A/B及条件增量C，写清停止条件和资源预算。
- [x] 自查文件边界、验收命令和与既有用户改动的隔离方式。

## 执行进度

- [x] M4.2.4-A：已实现Stage/Process/UI计时并通过主机测试、构建与静默实测；详见[静默记录](../Milestones/M4_2_4_A_Foreground_Profile.md)。
- [x] M4.2.4-B：已完成原声音候选链64点批量迁移、主机等价测试和两次静默对比；详见[批量Stage记录](../Milestones/M4_2_4_B_Batch_Stage.md)。
- [x] M4.2.4-A2（补充测量）：同循环归因完成，最大循环内AUDIO21–25ms、UI1ms、OTHER16ms；UI最大20ms不在最慢轮。详见[A2记录](../Milestones/M4_2_4_A2_Loop_Attribution.md)。
- [x] M4.2.4-A3（补充测量）：AUDIO/OTHER内部分解完成；AUDIO最大轮28ms中STAT15、SD_STAGE6、CH_PROC6；同轮OTHER13中FLUSH12。详见[A3记录](../Milestones/M4_2_4_A3_Audio_Other_Profile.md)。
- [x] M4.2.4-A4（对照+优化）：控制镜像排除A3诊断自扰动；统计优化后STAT15→8–9ms、AUDIO27→18ms、LOOP40→34ms。详见[A4记录](../Milestones/M4_2_4_A4_Control_Statistics.md)。
- [x] M4.2.4-C2（评估）：日志量化完成，UART占用约21.5%、23.7行/秒；建议集中quiet/full策略。详见[C2评估](../Milestones/M4_2_4_C2_Log_Evaluation.md)。
- [ ] M4.2.4-C：C1 UI继续暂缓；C2实施待用户选择方案并另行授权烧录。

## 执行边界

- 本方案不要求主动发声，不宣称标准音频MATCH、声学到达精度、双板配对或测距。
- 只做静默可验收的软件范围；**新镜像烧录需另行取得授权**，本文件不是烧录授权。
- No CubeMX change required；不改GPIO、时钟、SAI、DMA、NVIC、ETH、LTDC或Cache配置。
- 保留既有PPS/CubeMX/协议等用户未提交改动，不混入本方案提交。
- 当前A板为M4.2.4-A4优化镜像（固件标识仍`SBCG-M4.2-BATCH`，Debug SHA256 `de2120055c2c681abeb7935070bb1de8a175012113bfbc6160032a252bde2b9f`）；C未开始，不烧录新镜像。
- 一次只实施一个增量；A通过后再决定是否启动B，B通过后再决定C；不自动进入下一增量。
- 不扩大日志队列、不延长1500ms期限、不减少257窗/候选或8窗/轮、不改变两套阈值。

## 1. 当前证据与缺口

| 项目 | 已测结果 | 仍未回答的问题 |
| --- | --- | --- |
| 扫频扫描 | SCAN最大3ms（原59ms，目标≤10ms） | 无；扫描本身已无加速必要 |
| 搜索/完成 | 静默DONE_MAX=462ms（A最终采集157ms；目标≤1000ms），候选全完成、丢弃0 | 音频事件负载下是否仍≤1000ms |
| 前台GAP | M4.2.4-A实测最大60–61ms；B后49ms | 剩余未归因部分（音频统计/网络/日志等） |
| 原声音候选链 | B已迁移为64点批量：`STAGE_MS` 21→7ms；非64窗口自动回退 | 其他配置/极端流下需继续回归 |
| UI更新 | A实测19–20ms；B采集26ms（B未改UI，运行间波动） | 是否长期为主要GAP来源 |
| 日志 | 静默窗口LOG_FULL/WRITE/BAD=0 | 主动事件/长窗口下是否队满 |
| 主机压力 | 约2候选/s在1ms级循环可完成（max 203ms）；34ms慢循环超载；32行/s突发令模拟队列溢出7行 | 真实日志突发和真实主循环分布未知 |
| 主动音频 | 环境不能发声，MATCH 10/10未验收 | 标准音频识别、漏检/重复、声学完成时延 |

2026-10-26 A实测（最终Debug SHA256 `3cca12ce…`，静默69.6秒）：STAGE=21ms、PROC=1ms、UPDATE=19ms、SCAN=3ms、SCORE=4ms、GAP=60ms；业务/日志/UI错误全0。先前同镜像原型两次采集为STAGE=21/UPDATE=20/GAP=61，可复现。结论：原候选Stage与UI是两个最大已测贡献，B批量迁移有明确依据；GAP仍有约20ms量级未归因。

结论：继续优化扫描没有意义；在不能发声的前提下，应先量化“剩余前台时间花在哪里”，并迁移唯一仍逐帧处理的原候选链。UI、日志和搜索优化都必须由分解证据或真实日志数据触发。

## 2. 方案比较与选择

| 方案 | 作用与代价 | 决策 |
| --- | --- | --- |
| 分模块最大耗时分解 | 只增加低频计时字段，解释106ms GAP；不改变业务 | **首先实施（A）** |
| 原声音候选链64点批量 | 预计降低每块逐帧调用开销；需等价测试和静默实测 | **第二步（B）** |
| UI分片/降低非关键刷新 | 仅在A证明UI为主因时有用；影响显示交互 | 条件增量（C1） |
| 日志降频/合并 | 仅在真实LOG_FULL出现时需要；影响分析证据完整性 | 条件增量（C2） |
| 搜索自适应/增量缓存/FFT | 当前SCORE最大4ms、标准音频候选率约0.5/s，暂无证据 | 暂缓 |
| 扩大队列或延长期限 | 隐藏积压，增加迟到/过期风险 | 不采用 |
| 开Cache/改时钟/改DMA | 涉及一致性与生成配置，风险高 | 不采用，另立方案 |

## 3. 第一个增量：M4.2.4-A前台耗时分解

**独立目标：** 在不改变任何业务输出的前提下，给出音频前台各主要模块的HAL毫秒最大耗时，解释剩余GAP并决定是否需要B以外的优化。

**范围：** 只增加计时与诊断字段；不移动调用顺序、不改变队列/门限/日志频率。测量点为现有函数的进入/退出，计时差值使用uint32无符号差。

建议最小字段集（最终以代码可读性和行长<256为准）：

- `$SOUND_STATE`追加`STAGE_MS`（`SoundDetectorService_Stage`内8192帧逐帧暂算循环最大耗时，不含入口校验/工作副本拷贝）和`PROC_MS`（同服务Process入口守卫后到返回最大耗时，含日志格式化/发送）。
- `$UI`追加`UPDATE_MS`（`UI_TouchGFX_Process`入口到返回最大耗时，不含`Report()`日志格式化/发送）。
- 已有CHIRP `SCAN_MS/SCORE_MS/COPY_MS/WORK_MS`和`GAP_MS`继续保留；不新增周期日志行。
- `$AUDIO`的`STAT_MS`**未实施**：该行最坏长度已接近256字节，追加字段可能越界；本次先不做，若剩余GAP仍需归因，另立短行/缩短字段方案，不擅自改现有AUDIO格式。

文件：

- `App/Detector/sound_detector_service.c/.h`：Stage/Process计时字段及快照说明。
- `App/UI/touchgfx_service.c`（及必要头文件）：UI处理计时字段。
- `Tests/SoundDetectorService/test_service.c`、`Tests/TouchGFXService/test_service.c`、对应run_tests.py：验证计时字段、回绕/清零、故障状态日志仍按原周期输出，不把主机耗时当板端值。
- 本计划、`Docs/Project_State.md`、里程碑报告及索引文档：区分“测量增量”与“性能修复”。

A实际结果：Stage 21ms、Process 1ms、UI 19–20ms、Scan 3ms、Score 4ms、GAP 60–61ms；最终镜像静默采集69.6秒，业务/日志/UI错误0。详见[M4.2.4-A测量记录](../Milestones/M4_2_4_A_Foreground_Profile.md)。独立复核首轮发现F1（不健康快照分支提前return改变状态日志），已修正并补测试后重新烧录最终镜像；delta复核PASS_WITH_RESERVATIONS，保留项为低风险口径/覆盖（Stage仅测循环、AUDIO STAT_MS未测、PROC/UPDATE统计区间不同）。

验收：

- 主机O0/O2、相关回归、两配置构建和四项布局检查全部通过。
- 新增计时只增加有界读取（每块/每轮固定次数），不引入逐样本计时、动态内存或日志洪泛。
- 获得一次静默烧录（需另行授权）后，能给出各模块最大值和GAP的归因；不要求GAP指标本身立即下降。
- 如果分解显示某模块最大耗时<1ms或无法解释GAP，则停止该模块的后续优化，不制造伪需求。

停止条件：A只能回答“时间花在哪里”；若没有足够证据指向UI或原候选链，则B前重新评估，不直接做C。

## 4. 第二个增量：M4.2.4-B原声音候选链64点批量迁移

**独立目标：** 把`SoundDetectorService_Stage`对每块8192帧的逐帧`SoundDetector_Push`替换为完整64点批量入口，使候选、质量字段、队列和状态与原实现逐字段一致，同时降低Stage耗时。

**约束与设计：**

- DMA借用、工作副本`working=current`、边界复位、暂存候选、End后才提交、失败清理和日志频率全部保持。
- 现有RAM使用率约97%，不允许再新增16KB整块单声道缓冲。实现时每次从交错PCM抽取64点到128字节局部暂存，再调用`SoundDetector_PushBatch`；`available=min(64,剩余帧数)`，按`consumed`推进。
- `consumed==0`、返回INVALID/DISCONTINUITY或元数据非法时，沿用现有`return false`→`Complete`拒绝路径，不吞样本、不无限循环。
- 不改`SoundDetector`核心、门限、窗长、冷却、候选队列4条、事件时间服务。

文件：

- `App/Detector/sound_detector_service.c/.h`：Stage批量实现及必要的时延计数字段。
- `Tests/SoundDetectorService/test_service.c`：保留原DMA归还/跨块/失效/队列覆盖；新增或复用逐字段对照。
- 可新增`Tests/SoundDetectorService/test_batch_service.c`：用同一合成PCM和End结果分别驱动原逐帧参考与批量实现，比较候选序列、session/frame、质量字段、暂存/发布计数和快照状态；O0/O2各一遍。
- 本计划、`Docs/Protocol.md`（仅若字段/固件标识变化）、状态文档。

验收：

- 主机：原SoundDetectorService全部用例、新等价对照、真实20秒WAV路径和ChirpService回归通过；不修改任何现有数据来适配。
- 静默实机（需另行授权）：STAGE_MS相对A明显下降；GAP/DONE_MAX不退化；AUDIO/CHIRP/LOG错误全0；单BOOT、A UID。
- 不宣称标准音频MATCH或声学精度；主动声音验收仍单独阻塞。

停止条件：若批量实现后Stage耗时没有可测下降，或等价测试无法覆盖现有发布/拒绝语义，保留原实现并停止B，不以测试通过冒充收益。

**B实际结果（2026-10-26）：** 主机新增真实服务 vs 独立逐帧参考的逐字段对照（3候选SESSION/FIRST/CONFIRMED/E_AC/PEAK/CLIP一致；窗口64整块Stage中逐帧Push=0、PushBatch>0；window4/失败/边界路径由原测试覆盖）。烧录B镜像后两次静默采集：`STAGE_MS` 21→7ms、`GAP_MS` 60–61→49ms，`SCAN_MS` 3、`SCORE_MS` 5；SOUND/CHIRP/AUDIO/LOG/UI错误全0，候选零丢弃。详见[M4.2.4-B记录](../Milestones/M4_2_4_B_Batch_Stage.md)。

## 5. 条件增量：M4.2.4-C

只有在A/B证据明确时才另立，不与A/B同时提交。

### C1 UI耗时

触发：A显示UI更新是GAP主要贡献。**A2补充测量未满足该触发**：最大循环内UI仅1ms，UI自身最大20ms出现在其他轮次；直接改UI缺少最慢轮证据，C1暂不启动。

### C2 日志策略

触发：真实静默/长窗口或主动音频窗口出现LOG_FULL/WRITE/BAD>0，或A/压力测试证明日志排空挤占前台。**A4后FLUSH≈15ms已成为最大单项且日志错误仍为0；C2评估测得70秒内UART占用约21.5%、23.7行/秒，最大单行`$UI`174B≈15.1ms。** 结论：单纯降频不能降低最大FLUSH，需“集中quiet/full过滤+关键行保留”，必要时压缩长行；详见[C2评估](../Milestones/M4_2_4_C2_Log_Evaluation.md)。实施前需用户选择quiet/full方式并另行授权烧录；不得仅凭单次最大值直接改策略。措施：默认quiet压制纯周期状态但保留BOOT/RESET/命令回复/声音事件及错误摘要；full模式恢复完整日志供分析。验收：日志错误0、关键事件可核对、UART占用和FLUSH下降、full模式字段可复现；不得用主机合成32行/s结果直接改板端策略。

## 6. 暂缓与不采用

- **M4.2.3-B主动标准音频验收**：环境不能发声；待用户环境允许后单独执行MATCH 10/10、漏检/重复和声学完成时延。它不属于A/B的验收前置，但SBCG-M4.2-BATCH的“搜索完整完成”正式结论仍待它。
- 双板同步/PPS波形/物理到达/距离方向：独立验收，不使用本方案的静默CPU数据代替。
- 两条门控共享同窗统计：B完成后再评估，需证明窗原点、声道、流边界和有效期一致。
- 搜索算法（FFT/粗精/自适应评分）：SCORE最大4ms且标准音频候选率低，暂无收益证据。
- Cache/时钟修改：需要内存一致性、DMA和CubeMX影响分析，另立方案。
- 队列扩容、延长期限、降低门限或跳窗：文档已判定为掩盖问题，不采用。

## 7. 验收指标与命令

| 指标 | A验收 | B验收 |
| --- | --- | --- |
| 行为 | 输出/状态/日志完全不变 | 候选/质量/状态与逐帧参考完全一致 |
| 计时 | 已完成：各模块最大值可解释GAP（STAGE21/UI19–20/SCAN3/SCORE4/GAP60–61ms） | STAGE_MS可测下降且GAP/DONE_MAX不退化 |
| 原业务 | AUDIO零丢块/错误，CHIRP/LOG零错误 | 同左 |
| 构建 | Debug/Release及布局通过 | 同左 |
| 烧录 | 新镜像另行授权；静默采集≥60s | 同左 |

```powershell
python -B Tests/SoundDetectorService/run_tests.py
python -B Tests/SoundDetector/run_tests.py
python -B Tests/ChirpService/run_tests.py
python -B Tests/Audio/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/ChirpMatcher/run_tests.py
python -B Tests/SoundEventTime/run_tests.py
cmake --build --preset Debug --parallel
cmake --build --preset Release --parallel
python -B Tools/Monitor/check_audio_layout.py --preset Debug
python -B Tools/Monitor/check_eth_layout.py --preset Debug
python -B Tools/Monitor/check_audio_layout.py --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

每增量使用新的日志前缀，保留现有证据；全部退出0后再准备独立复核和烧录申请。不能把主机模拟耗时或静默诊断写成声学/实时最坏时间证明。

## 8. 交付与提交顺序

| 增量 | 交付物 | 提交 |
| --- | --- | --- |
| M4.2.4-A | 分模块计时代码、对应测试、静默分解报告、文档 | **已完成**；镜像`3cca12ce…` |
| M4.2.4-B | 批量Stage实现、逐字段等价测试、静默前后对比、文档 | **已完成**；镜像`192c6a33…` |
| M4.2.4-A2 | 同循环LOOP/AUDIO/UI/OTHER归因、主机测试、静默记录 | **已完成**；镜像`d19a0005…` |
| M4.2.4-A3 | AUDIO内部分解、Flush/OTHER拆分、静默记录 | **已完成**；镜像`4e8a53f5…` |
| M4.2.4-A4 | 关闭诊断对照、AudioStatistics优化、静默复测 | **已完成**；镜像`de212005…` |
| M4.2.4-C | 仅在触发后退成本独立的UI或日志增量 | 未开始（C1暂缓；C2候选） |

A/B/A2/A3/A4已提交对应功能与文档；C未开始。下一步评估C2日志策略，需用户确认并另行取得烧录授权。

## 9. 自查与实施入口

- [x] 每个优化都对应现有观测或明确缺口，未把不同最大值相加或承诺加速比例。
- [x] 明确“不能发声”下哪些结论仍可独立验收，哪些必须继续阻塞。
- [x] A/B均给出文件边界、等价方法、停止条件和需要新增授权的烧录关口。
- [x] 不改变硬件配置、队列/期限/阈值/搜索窗口；UI/日志/搜索只在证据指向时另立。
- [x] 说明合成压力和HAL毫秒计时的能力边界，不把它们写成声学精度或最坏执行时间。
