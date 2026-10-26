# PCM 全事件账本与 TOA 离线回放 — 执行结果

> 计划：[2026-10-26_PCM_Event_Replay_Plan.md](../Plans/2026-10-26_PCM_Event_Replay_Plan.md)
> 执行日期：2026-10-26 · 状态：已完成，停止在本增量
> 运行产物：原始记录 `Logs/Detector/TOA_Replay/20261026T091225/`；P0当前证据：默认两版 `20261026T110943/`、三版 `20261026T111047/`（HEAD `c27e5e7`，manifest/ledger/replay/summary/synthetic，含快照/窗口/version_meta/validity_delta）；独立复核 `Logs/Detector/TOA_Replay/P0_review/review.json`，修复测试 `Logs/Detector/TOA_Replay/P0_review/fix-tests.log`。

## 1. 目标与边界

对九轮单板实验建立"不漏账、可追溯、可重复计算"的事件基准：分清单板声音候选、标准声身份、可用 PCM 窗口与到达时刻，冻结两版前沿估算器（`c79bf25` 峰回溯 / 工作区包络），用真实 C 实现离线回放。**不改识别门限、前沿算法、捕获/协议、时钟或屏幕；不改 CubeMX；不烧录；不改动用户未提交的 PPS/CubeMX/估算器文件。**

## 2. 输入冻结（任务1）

- HEAD：`e9013cad`；执行时 `git status` 含用户未提交 PPS/CubeMX/估算器改动，全部保持原样。
- 九轮枚举与计划 §2 基线**逐轮一致**：SOUND 102、SEARCH 115、PCM 对 115；无 BOOT 行（域=`trial::unknown::1`）。3 个无 PCM 候选为**定时对齐响应**（toaB-07 SEQ5、toaB-08 SEQ19、toaC-02 SEQ25），缺 PCM，非"背景"。
- 全部输入（run.jsonl、trial_summary.json、115 对 PCM/JSON）SHA256 已入 manifest。
- 素材 SHA 与 `candidate_manifest.json` **完全相符**（两个 WAV 均 OK）。
- 算法快照冻结：`git show c79bf25` → `source_snapshots/peak_backtrack/`；工作区 → `current_envelope/`；各自 c/h 哈希入 manifest。
- 固件绑定：九轮均无 BOOT/镜像 SHA 旁证 → `firmware_binding=UNKNOWN`；在线窗口状态一律 UNKNOWN。

## 3. 事件账本（任务2）

守恒链：串口总行 8181 中的 437 条受跟踪记录（102 SOUND_CAND + 105 SOUND_TIME + 115 SEARCH + 115 CAPTURE）→ SOUND 候选 102 → 关联到 PCM 窗口 99 → 完整 256 点窗口 99；3 个无 PCM 候选（toaB-07 SEQ5、toaB-08 SEQ19、toaC-02 SEQ25，见 replay.jsonl `no_pcm`；`toa-04` 无孤儿）。所有 SEARCH/CAPTURE/PCM 按域内 ID 精确关联并对字段核对（含 FRAMES、HINT/BEST↔hint_frame/raw_first_frame）；SOUND↔PCM 按帧包含建立候选边，多解保留 AMBIGUOUS；跨轮/域冲突显式记录（本轮 0 冲突）。账本测试 17 项全过（跨trial独立、BOOT/$BOOT分域、日志序帧/序号回归断联、SEQ≠ID保留关联、双PCM歧义、孤儿/坏JSON/非对象JSON/非数字文件名保留、FRAMES与hint/raw 冲突标记、PCM 按域不串边）。

## 4. 真实C回放（任务3）

- 对每候选取窗 `[FIRST−64, FIRST+256)`（锚=max(FIRST−64,0)），从单声道小端 int16 PCM 精确切片，**不补零、不裁窗、不拼接**；缺左沿/截尾一律 no-pad 记为原因。
- C 适配器 `toa_replay_cli.c` 只接受恰好 256 点；O0 与 O2 各编译一次（两版本各 1 次 = 4 个二进制），每窗重复 2 次。
- 结果：**重复确定性 396/396 完全一致；O0=O2 零失配**；两版本对 99 窗均全部可估算（包络 99/99，峰回溯 97/99，2 窗仅在峰回溯下失败）。
- 名义 TOA=(anchor+fraction)×1e6/48000，仅作诊断量，未宣称 PTP/物理真值。

## 5. 已知输入对照（任务4）

| 信号 | 规模 | 两版本 O0/O2 有效 | 相对幅度跨阈偏置 |
| --- | --- | --- | --- |
| 斜坡（64 门控相位 × 3 幅度 × 3 噪声 × 5 回声） | 2880 | 2880/2880（100%） | mean +0.31 帧（≈6.5μs），σ 1.01 帧，max 7.1 帧（≈148μs） |
| 标准 chirp 切片（9 相位 × 2 噪声 × 2 回声） | 36 | 全部有效 | vs 幅度跨阈：包络/修正 189.8μs、峰回溯 1671μs；vs 淡入起点：524/2005μs（KNOWN_LIMITATION：估算器不测物理起点） |
| 纯静音 / 单点尖峰 | 各 1 | 均 rejected（estimate_failed） | — |

饱和削顶逐窗计数入账。两版本在同一 256 点窗口上输出逐窗一致（差异仅来自相对阈值/能量段选择，见 §6）。

## 6. 全量真实输入汇总（任务5）

- **版本相对差（97 对共同有效窗）**：包络比峰回溯平均早 **−20.3 帧（≈−423μs）**，σ 31.7 帧，范围 [−153, 0]；包络从不更晚、从不更少有效。
- **分组跨轮槽一致性**（仅 B 组有干净的现场基线；A 组按计划列为历史观察不参与主比较；C 组因 C02/C03 候选结构污染无法成立）：
  - B 组 peak_backtrack：槽 3/4/7/8 σ≤10.8μs（与现场一致），槽 1/2（σ≈1283μs）、5/6（σ≈1382μs）、9（σ≈1501μs，B09 背景候选混入）超差。
  - B 组 current_envelope：槽 1/2 σ 降至 **3.7/2.8μs**，槽 3/4/7/8 保持 ≤5.2μs；**槽 5/6/9 仍 σ≈1299μs 超差**（两版本同差）。→ 包络修复了回声锚定槽 1/2，未修复槽 5/6。
- **三类结论**：
  - `TOOL_CHECK`：**PASS**（本增量测试全绿 + 回放确定性 396/396 + O0/O2 一致 + 无硬失败）。
  - `DATA_COVERAGE`：九轮逐轮列出（3 孤儿、C02 缺 2 槽）；A/B/C 均**不满足全 10 次完整唯一对齐** → 一致性指标 INSUFFICIENT。
  - `STANDARD_ID`：如实保留原识别字段（MATCH/WHY），未新增识别成功率。`PHYSICAL_TOA_ACCURACY`：**NOT_VERIFIED**（无独立播放/直达参考）。

## 7. 复核结果与下一项最小修正（任务6）

- 六项验收命令全过：`test_pcm_event_ledger`、`test_toa_replay`、`test_chirp_capture_analysis`、`test_udp_chirp_capture`、`run_toa_estimator_tests`（含包络版）、`run_tests.py --phase GREEN`；账本/回放同输入复算一致。
- 现场 toaC 串口"600ms 级偏差"与 offline 诊断对照后确认：**根源是 C02/C03 检出多余候选（15/12 vs 10）导致的播放槽映射污染，不是估算器回归**（离线两版本对 C 窗均保持同向、可复现）。
- **下一项最小修正（本增量不实施）**：将 `EstimateBackward` 的能量段起跳由"包含能量峰的分段"扩展为"**窗内最左的 ≥Emax/8 分段起点**"，目标病样为 B 组槽 5/6 窗（两版本均锚在多径回声前沿，σ≈1.3ms）与 B09 槽 9 背景候选；实施后须先在本全量基准上对拍，再做一轮带 BOOT/镜像 SHA/声道/播放参考的受控采集（计划 §6），数据不足时不抢跑。

## 8. 下一步

停止于本增量。事件身份与前沿可信后，再推进子帧→PTP、两板真实同步/方向/测距（目标路线见 [Goal_Aligned_Roadmap](../Goal_Aligned_Roadmap_2026_10_26.md)）。

---

# 跟进增量（2026-10-26 下午）：最左能量段起跳修正与在线验证

## 根因修正

B 组槽 5/6/9 在两版算法下均超差（σ≈1.3–1.5ms）。对冻结窗口逐窗检查发现：每个窗口都由"直达波段（幅值约 2000）→ 能量凹 → 回声段（峰约 5400）"组成；`EstimateBackward` 的能量段起跳只从"能量峰所在段"向左找，当直达与更强回声之间的能量凹低于 Emax/8 时会把锚点落在**回声段起点**（跨轮 seg_start 在 98~227 帧间 ±1.9ms 跳动），而**最左 ≥Emax/8 段起点（直达波）跨轮稳定在 75~91 帧**。

修正：能量段起跳取**窗内最左的 ≥Emax/8 段起点**（`App/Detector/toa_estimator.c`）；单测/服务断言值全部不变（28.65/44.475/188μs 等）。

## 离线三版对拍（同一批冻结 B/C 窗口）

| B 组槽 | peak_backtrack(c79bf25) σ | db441e0(leftmost) σ | current(5471919) σ |
| --- | --- | --- | --- |
| 1 | 1282.92 | 3.68 | **3.68** |
| 2 | 1282.03 | 0.42 | **0.42** |
| 4 | 2.18 | 1.54 | **1.54** |
| 5 | 1381.95 | 3.01 | **3.01** |
| 6 | 1385.31 | 2.63 | **2.63** |
| 8 | 0.81 | 1.45 | **1.45** |
| 9 | 1501.02 | 3.03 | **3.03** |

B 组可判槽（槽 3/7 因候选缺 PCM、B07/B08 各缺一轮 → 无完整三轮，不可判）修正版 σ≤3.68μs；重复 18102/18102、O0=O2、TOOL_CHECK=PASS；合成 2918 窗全部正常（含 36 个标准 chirp 噪声/回声切片与相关峰参照 lag0/corr1.0）。对拍工具 `toa_replay.py --prior-envelope` 三版回放已入库。

> **P0标签修正**：上表原“original_envelope”列实际冻结的是`db441e0`（最左能量段修正）快照；真正的修正前包络`48aec5…`只存在于旧`091225/092458`运行快照，未提交为git对象，其B组槽5/6/9约为1299.56/1299.19/1299.21µs。三版证据上`original_envelope`→`current_envelope`的fraction差为0，但`toaC-02 SEQ21`（PEAK=1975弱候选）出现一次valid→invalid（valid 98/99→97/99），不能把该表当成“原始包络 vs 修正”的before/after。

## 在线受控采集（烧录修正版，toaD/toaD2/toaD3 各 3 轮，卷 100、标准 10×20s）

- 槽 5/6（目标）：在 toaD 批次 **σ=1.5/4.0μs**（n=2，另一轮被环境声污染），修正版在真实采集上也复现稳定。
- 环境噪声：toaD-03 播放结束后出现 11 个额外候选；toaD2 每轮 23~27 个候选且播放前即有候选；toaD3 17/12/10。探测器在环境声中满负荷触发，聚合槽统计被候选结构污染（toaD3 槽 3–8 出现 13.3ms 统一偏移 = 额外候选合并进槽的映射伪影），全 10 槽一致性不可判。
- **板端↔离线一致性（关键验证，范围如实）**：toaD/toaD3 共 76 候选 → 71 完整离线窗 → **67 个带在线数值 TOA 的窗口与离线修正版偏差 ≤0.5μs（均值 0）**；另有 4 个完整离线窗在线 TOA_US=NA（toaD-01 SEQ3、toaD3-01 SEQ263/264、toaD3-02 SEQ281）与 5 个无 PCM 候选。67 窗亚微秒一致仅证明**成功子集的算法一致性**（行为级绑定），不能据此宣称现场槽统计的失败"全部"来自探测器。
- 结论（收窄）：修正版由 ①冻结窗口 B 组可判槽 σ≤3.68μs、②在线槽 5/6 σ≤5μs、③67 窗板端/离线亚微秒一致共同验证；全 10 槽聚合统计因房间噪声与候选缺口为 INSUFFICIENT；4 个在线 NA 窗与 3 个无 PCM 候选的在线资格留待计划 §6 受控采集项（BOOT/镜像SHA/声道/播放参考）解释。
- 烧录镜像：`build/Debug/SBCG.elf` SHA256=`286940AEEF473BF79595D095A232ED447E2BF1C94E5F85F0AE3CEF0D5BA18937`。

---

# 只读审查后修复（2026-10-26 深夜）

外部只读审查指出账本/回放工具与文档的多处缺陷，已全部修复并补测试/重跑（无硬件变更）：

- **P1 估算器尖峰回归**：`EstimateBackward` 的 run_samples 由"跨峰跨度计数"改为**真实连续过阈样本计数**；两孤立尖峰窗现在正确拒绝（新增单测）。
- **P1 账本结构**：识别固件真实输出的 `$BOOT`；回归检查改按**日志顺序**（CAPTURE ID/FIRST、SOUND SEQ/FIRST）；事件键含**域+FIRST**（跨域不串边、窗口文件名不冲突）；PCM 关联保留**各域候选**（不再被后域覆盖，多解标 AMBIGUOUS）；补齐 FRAMES、SEARCH HINT/BEST↔hint_frame/raw_first_frame 交叉核对；坏 JSON、非对象 JSON、非数字 PCM 文件名容错入账。
- **P1 回放门控/输出**：`ledger.json` 现序列化**全事件/边/域/孤立项**；新增 `records.jsonl`/`ledger.csv`；`ledger.problems` 已并入`per_trial.problems/format_errors/TOOL_CHECK`（P0-F1修复）；账本级regression测试验证exit≠0；parse/scan级格式错误同样使TOOL_CHECK=FAIL并返回非零（合法覆盖缺口不误判）；槽一致性门控要求**每轮各贡献唯一值**；AMBIGUOUS/无效 PCM 不再计入 unique_windows；`trial_summary.json` 从正确目录冻结。
- **P2**：`git show` 子进程 UTF-8；CLI `FILE=` 移行尾（空格路径安全）+ `isnan` 拒绝 NaN；合成补齐标准 chirp 的幅度跨阈/噪声/回声切片、`norm_peak` 相关峰参照与条件级偏置/跳峰统计，并纳入重复/O0-O2 确定门控。
- **文档纠正**：撤销"所有 10 槽 σ≤3.7μs/3 孤儿=背景/toa-04 有孤儿/summary 已冻结"等过度声明；B 组改为"可判槽 σ≤3.68μs、槽 3/7 无完整三轮不可判、全 10 槽 INSUFFICIENT"；3 个无 PCM 候选按定时对齐响应保留。
- 修复后重跑（run `20261026T102536`）：TOOL_CHECK=PASS、format_errors=[]、重复 18102 窗 0 失配、O0=O2 零失配、B 组修正版可判槽 σ≤3.68μs、守恒 437/102/99/16（16 = 未参与 SOUND 回放的 PCM）。P0当前证据改用修复后`20261026T110943`（默认两版）/`20261026T111047`（三版），见下。

## P0证据收口（2026-10-26）

- 默认两版当前代码基线：`Logs/Detector/TOA_Replay/20261026T110943/`（manifest HEAD=`c27e5e7`，Analysis无未提交修改）；三版：`20261026T111047/`（HEAD=`c27e5e7`；`--prior-envelope 20261026T094022`）。
- 两run均 `TOOL_CHECK=PASS`、`format_errors=[]`、`hard_failures=[]`；守恒 8181/437/102/99/115/16；重复 12068/18102 行、0失配、O0/O2零失配；合成2918窗；summary含`version_meta`、`version_validity_delta`，B组`missing_slots=[3,7]`，账本内部problems已入TOOL_CHECK。
- 独立复核报告：`Logs/Detector/TOA_Replay/P0_review/review.json`，verdict=**PASS_WITH_RESERVATIONS**（基于pre-fix `104613/105129`，1×P1、3×P2、4×P3）；其P0-F1/F2/F3/F5/F6/F7/F8已由`c27e5e7`修复/更正并重跑验证，见`P0_review/fix-tests.log`与审计文档。
- 修复后重跑只证明离线工具/证据链；标准音识别、物理TOA/PTP/双板未验收。
