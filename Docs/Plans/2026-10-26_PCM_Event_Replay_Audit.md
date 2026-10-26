# 全事件对账与TOA回放：执行审计与后续规划

<!-- doc-status: audited-implemented-next-planning; reviewed: 2026-10-26 -->

## 结论

- **原计划的六个步骤均已实现**；独立复核结论为 **PASS_WITH_RESERVATIONS**（无事件丢失、强制最近邻配对或跨域/跨版本混算；保留门控/标签/诊断字段等低中风险项，见下文）。
- **代码基线 `74e563e`，字面HEAD `b3f98ff`（仅Docs提交）**。默认两版运行：`Logs/Detector/TOA_Replay/20261026T104613/`（manifest HEAD=`74e563e`，Analysis无未提交修改）：`TOOL_CHECK=PASS`、`format_errors=[]`、`hard_failures=[]`；串口总行8181中437条受跟踪记录→102 SOUND→99窗；115 PCM/16未用；重复12068行0失配；O0/O2零失配；合成2918窗。
- 三版基准：`Logs/Detector/TOA_Replay/20261026T105129/`（HEAD=`b3f98ff`，相对`74e563e`仅文档提交，Analysis/估算器代码相同；`--prior-envelope Logs/Detector/TOA_Replay/20261026T094022`）：版本=`peak_backtrack/original_envelope/current_envelope`；`TOOL_CHECK=PASS`、重复18102行0失配、O0/O2零失配；同守恒链。
- **独立复核报告**：`Logs/Detector/TOA_Replay/P0_review/review.json`（`PASS_WITH_RESERVATIONS`）；未发现事件丢失、强制最近邻配对或跨域/跨版本混算；保留项见下。
- 原计划第6步的“停在本增量”仍然成立；后续的估算器修正、实板受控采集属于独立增量，不改变本离线工具计划的验收边界。
- 审计发现原计划文档有**过时文字**（“目前仅完成规划”“新入口尚未创建”）和一处证据完整性缺口（审计前独立复核报告未入库）；本次审计已修正过时文字，并把P0独立复核报告固定到`Logs/Detector/TOA_Replay/P0_review/review.json`。

## 本轮复核命令与结果

以下命令在代码基线`74e563e`（字面HEAD `b3f98ff`仅Docs）、当前工作区（仅用户PPS/CubeMX未提交改动）执行，均exit0：

```powershell
python -B -m unittest discover -s Analysis/tests -p test_pcm_event_ledger.py
python -B -m unittest discover -s Analysis/tests -p test_toa_replay.py
python -B Analysis/tests/test_chirp_capture_analysis.py
python -B Tools/Monitor/tests/test_udp_chirp_capture.py
python -B Tests/Detector/run_toa_estimator_tests.py
python -B Tests/SoundDetectorService/run_tests.py --phase GREEN
```

关联的完整重跑命令：

```powershell
python -B Analysis/toa_replay.py --trials toa-20261026-04 toa-20261026-05 toa-20261026-06 toaB-20261026-07 toaB-20261026-08 toaB-20261026-09 toaC-20261026-01 toaC-20261026-02 toaC-20261026-03 --output-root Logs/Detector/TOA_Replay
```

本轮生成run `20261026T104613`（默认两版，HEAD=`74e563e`）和`20261026T105129`（三版，HEAD=`b3f98ff`，代码同`74e563e`）；manifest的Analysis无未提交修改。

## 六步核对

| 步骤 | 实现证据 | 状态 | 边界/缺口 |
| --- | --- | --- | --- |
| 1. 冻结输入和版本 | `Analysis/pcm_event_ledger.py`、`Analysis/toa_replay.py` 的输入枚举/哈希/快照；manifest含HEAD、dirty状态、素材SHA、算法快照；九轮102 SOUND/115 SEARCH/115 PCM | **已实现** | 九轮无BOOT/镜像SHA旁证，`firmware_binding=UNKNOWN`；A/B/C分组不是镜像版本证明；C组不能当作当前包络版实板结果 |
| 2. 全事件账本 | `records.jsonl`/`ledger.json`/`ledger.csv`；8181串口行中437条受跟踪记录→102 SOUND；3条无PCM（B07 SEQ5、B08 SEQ19、C02 SEQ25）保留；0歧义、16份PCM未配SOUND；账本17项测试通过 | **已实现** | 事件身份仍有播放槽缺/候选结构污染；C02缺2槽；无独立播放参考不能证明全10次唯一对齐；账本内部problems尚未并入TOOL_CHECK（P0-F1） |
| 3. 核对窗口并回放 | 256点窗资格、锚`FIRST−64`、不补零/不裁窗；真实C适配器；O0/O2两次重复；99完整窗；默认跑peak_backtrack/current_envelope两版，`--prior-envelope`另跑三版对拍 | **已实现** | 旧实测数据无BOOT/镜像/声道旁证，在线窗口资格UNKNOWN；物理到达参考不存在；三版对拍是后续扩展，不是原两版计划 |
| 4. 已知输入对照 | 2918合成窗：斜坡2880、标准chirp36、静音/尖峰；偏置/覆盖率/跳变统计；标准chirp相关峰参照lag=0/corr=1.0 | **已实现** | 已知输入只证明工具/算法条件敏感性；不能外推为真实房间精度；标准chirp相对幅度跨阈偏置仍属KNOWN_LIMITATION |
| 5. 汇总全部真实事件 | `summary.json`三类结论：DATA_COVERAGE逐轮、A/B/C分组、B组修正版可判槽σ≤3.68μs、重复/O0-O2确定性；STANDARD_ID原样；PHYSICAL_TOA_ACCURACY=NOT_VERIFIED | **已实现（分层不通过）** | 全10槽与物理精度INSUFFICIENT/NOT_VERIFIED；B09背景候选、C02/C03污染、额外/背景事件需保留；不能只报稳定槽 |
| 6. 复核并确定下一修正 | `2d9a605`工具提交、`297ef65`三版对拍、`db441e0`最左能量段、`5471919`尖峰门控、`74e563e`审查修复；计划/里程碑/Project_State更新；六组验收命令exit0；默认`104613`+三版`105129`；独立复核报告已保存`P0_review/review.json` | **已实现，复核PASS_WITH_RESERVATIONS** | 保留P0-F1门控、P0-F2标签/delta、P0-F3 missing_slots、P0-F5 assets门控、P0-F6守恒措辞等，见下；不因此推翻当前两次run的数值结论 |

## 计划/文档缺口与处理

1. **计划文档过时文字**：第3行“本文件目前仅完成规划”、第165行“这些新入口目前尚未创建”与已实现状态矛盾。本次审计已改为“已实施并审查修复”和“新入口已创建；命令保留复现”。
2. **证据与HEAD绑定**：审计前最新的 `20261026T102928` 运行manifest HEAD=`db441e0`且Analysis文件为dirty；不能直接作为`74e563e`的验收证据。本轮已新增 `20261026T104613`，manifest HEAD=`74e563e`，Analysis无工作区修改，作为后续基准。
3. **独立复核报告入库**：计划任务6要求独立只读复核；本轮P0已把复核产物固定到 `Logs/Detector/TOA_Replay/P0_review/review.json`，并在审计/状态文档登记指针；不能只以提交信息代替。
4. **计划范围外后续工作**：`db441e0`/`5471919`修改`App/Detector/toa_estimator.c/.h`，这已超出原计划“不改App”的边界，属于后续独立增量；原计划通过条件不因此改变，不能把后续实板结果倒灌成原离线计划全部通过。
5. **版本口径**：`peak_backtrack=c79bf25`；三版运行中的`original_envelope`实际为`db441e0`（最左能量段修正）快照，`current_envelope=5471919/74e563e`；真正的修正前原始包络快照为`48aec5…`，只存在于旧的`20261026T091225/092458`运行快照中，未成为git提交。三版证据上original→current的fraction差为0，但C02 SEQ21出现一次valid→invalid（弱候选，可能是正确拒绝）；不能把该表当“原始包络 vs 修正”的before/after。

## 独立复核保留项

复核报告 `Logs/Detector/TOA_Replay/P0_review/review.json` 结论为 **PASS_WITH_RESERVATIONS**：

- **P0-F1（P1）**：`ledger.problems`未并入`TOOL_CHECK/format_errors`，账本级格式问题仍可能exit0/PASS；当前两次run的3条“outside every capture”为预期覆盖缺口，不推翻本次PASS，但长期基准前应修复并补ledger级回归。
- **P0-F2（P2）**：三版标签/证据说明需按实际提交与哈希钉住；`original_envelope`实为`db441e0`；真正原始包络`48aec5…`仅存在于旧run快照；C02 SEQ21 valid→invalid应写入证据。
- **P0-F3（P2）**：`summary.cross_round_by_group.missing_slots`因str/int键不匹配恒为[0..9]；`full_10_slot_consistency_possible`逻辑不受影响，但该诊断字段不可用，应修。
- **P0-F5/P3、F6/P3、F7/P3、F8/P3**：assets未纳入门控、“8181相关行”措辞、额外事件出处、测试ResourceWarning/window计数口径，均登记为后续修复/说明项。
- 未覆盖边界：无BOOT/UID/镜像SHA/声道/采样率旁证，在线窗口资格UNKNOWN；无独立物理到达参考，物理精度NOT_VERIFIED；三版在真实集上的区别仅C02 SEQ21 valid→invalid；未烧录/未连接硬件。

## 后续规划（按证据顺序）

### P0 证据收口（本轮完成）
- 保留新运行目录、manifest、summary和测试日志；计划/状态文档同步到`74e563e`。
- 默认两版 `20261026T104613` 与三版 `20261026T105129` 均已固定；三版输入快照 `20261026T094022`。
- 独立复核报告保存于 `Logs/Detector/TOA_Replay/P0_review/review.json`（本轮P0生成；报告为只读复核结论，不修改代码）。

### P1 受控采集（下一独立增量，需硬件/用户配合）
- 按计划§6补：BOOT/UID、镜像SHA256、声道、实际采样率、10次播放参考、播放前后静默；文件命名和分片缺口显式记录。
- 用同一账本/回放工具复算，目标得到全10槽唯一对齐；不得用补造数据或删除背景事件。
- 事件身份与标准声识别需与M4.2.5的`LOW_CORR`结果联动；TOA回放正确不等于标准音匹配通过。

### P2 只选一个最小修正
- 若P1仍把误差归因于多径/前沿：在现有最左能量段修正基础上继续验证，不在同一次改多个参数。
- 若P1显示标准声身份/相关度门限是主因：用已标注正样本和语音/单音/击掌负样本评估，禁止无负样本直接降低0.80。
- 任何算法修改仍先在`20261026T104613`基准上对拍，再做一轮受控实板复验。

### P3 后续接口
- 事件身份与前沿可信后，才推进子帧TOA→本地PTP、双板配对/距离/方向与两屏结果；本轮不实施。

## 验收边界

- 本审计不修改识别门限、估算器、PCM捕获、PTP或硬件配置；工具/测试通过不等于标准音识别、物理TOA或双板测距通过。
- 所有数字为1ms/帧级诊断，不把名义微秒当PTP或物理真值。
- 既有用户PPS/CubeMX改动保持未提交、未混入；新运行产物在Logs，不提交生成物。
