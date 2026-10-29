# P1 只读分析：播放时间窗与槽覆盖修正

<!-- doc-status: playback-slot-classification-software-verified; no-firmware-change; reviewed: 2026-10-26 -->

## 结论

对P1音量/距离/方向数据做只读复核后，发现先前依据回放`by_trial_version`得到的“全10槽率”**不是播放对齐指标**：

- 该字段把所有有效回放行（包括播放前环境触发、播放后尾随捕获）都放入槽序列，并常以最小TOA当slot0；
- 一个播放前捕获（例如D50-01在7.6s、播放从12.1s才开始）或一个额外候选，会把整个槽基址移位，造成“缺槽/额外槽”假象；
- 因此先前记录的 volume 12/15、D40 3/5、D50 5/5、D+30 4/5、D−30 4/5 是**严格工具口径**，不能直接当作播放10槽捕获率。

按**播放时间窗**重新只读统计捕获日志的粗时间槽覆盖后：

| 组 | 严格工具口径（既有summary） | 播放窗只读复核 |
| --- | ---: | ---: |
| 同位置音量（100/80/60） | 12/15 全10槽 | **15/15** |
| D40（40cm,0°） | 3/5 | **4/5**（仅`p1d40-20261026-01`真缺slot5/6） |
| D50（50cm,0°） | 5/5（排除1个trial，其中2条格式错误） | **6/6**（含`01`；其格式错误来自播放前搜索） |
| D+30（50cm,+30°） | 4/5 | **6/6**（含`01`；同上） |
| D−30（50cm,−30°） | 4/5 | **5/5** |
| D20（20cm,0°） | 0个有效trial | 17个尝试中仅1个全10槽（`11`）；其余CHIRP任务严重不足 |

**结论：原全事件槽基址受到播放前/后捕获影响，但粗时间槽覆盖不等于声音事件已唯一关联或TOA有效。** D20仍有CHIRP任务不足，D40-01缺少两次对应捕获；这不改变严格格式错误，也不代表物理TOA/PTP/识别通过。

若同时要求窗内恰好10条捕获、每槽唯一且没有额外事件，五组分别仅为11/15、3/5、4/6、4/6、4/5。`p1-20261026-04`中接收时刻靠近slot0的捕获并不包含第一声SOUND候选的PCM，不能据粗覆盖补成已关联。后续输出须分开捕获日志覆盖、唯一SOUND→capture→PCM覆盖及各版本有效TOA覆盖。

## 只读方法

1. 从`trial_summary.json.play_span[0]`取播放API启动时刻，既有复核窗口取 `[start, start+22s]`；其中WAV实际时长为20s，另2s只是接收保护窗。窗口外捕获记为pre/post，不进入播放槽；API返回时刻不是播放结束。
2. WAV预期起点为1,3,…,19s；以`start+1s`为第一chirp锚点，按2s栅格分配slot，容差±0.95s。
3. 仍保留全部串口/PCM/背景候选；不删除、不补造。
4. 该分析是**只读复核**，不修改`Analysis/toa_replay.py`、不改变既有TOOL_CHECK门控。

分析记录：`Logs/Detector/P1/playback-window-analysis.json`。

## 关键证据

- `p1d50-20261026-01`：总捕获11个，播放前1个（7.6s），播放窗内10个，按窗统计slot0..9齐全；但串口在0.758/1.576s有2个播放前`$CHIRP_SEARCH`，无对应capture，严格工具因此报`search without capture`。这不是播放数据丢失。
- `p1d30m-20261026-01`：总捕获19个，播放前5个（3.1/3.8/8.74/9.56/11.64s），播放窗内14个；按窗统计slot0..9齐全；旧summary把播放前捕获当槽基址，才显示缺1/2/3/4/7。
- `p1d40-20261026-01`：总捕获8个、全部在播放窗内；接收日志约23.7/25.7s附近缺2个捕获，仍应记缺slot5/6。该轮有完整10条SOUND候选，缺的是对应capture/可回放窗，不能写成SOUND候选缺失。
- D20：多数trial `SOUND_CAND`有响应但`CHIRP CAND≈1–2`；仅`p1d20-20261026-11`播放窗内全10槽。D20仍是独立的门控/近场问题，不计入距离组有效证据。

## 对P1报告的影响

- 旧P1总结中的“全10槽不稳定”应理解为**严格工具统计不稳定**；播放窗复核后，已完成音量组15/15、D40 4/5、D50 6/6、D+30 6/6、D−30 5/5。
- 不要据此降低0.80或改算法；D20任务不足和D40-01捕获缺失仍需保留，播放前事件应显式分类而不是混入槽统计。
- 此前只读复核建议把pre-play/post-play标记为独立类别，用`play_span`与WAV参考对齐槽，并保留严格格式错误报告。该工具增量现已按用户确认范围实施，软件验证见下节；原始只读结果和旧run均保留。

## 边界

- 播放窗方法依赖主机`play_span`和WAV起点，不是板端PTP时间；槽分配是名义2s栅格，不能当物理到达精度。
- 方向/距离结论仍只基于单板、单一位置和有限样本；不能宣称方向/距离规律。
- 标准音识别仍0/10；PTP/双板/测距未验收；本轮无固件改动、无烧录。

证据：`Logs/Detector/P1/playback-window-analysis.json`、`Logs/Chirp/PCM/p1-*/`、`Logs/Chirp/p1-*.jsonl`；既有严格回放run见P1报告。

## 工具实施与验收（2026-10-26）

本次只有一个目标：将播放窗槽统计与全量严格检查并列，避免窗外事件改变播放槽编号。未修改固件、TOA估算器、0.80门限、采集工具或硬件配置，未烧录、复位或播放声音。

### 修改文件与调用关系

- `Analysis/toa_replay.py`：读取/冻结独立trial manifest，交叉核对播放上下文；将各事件自身的接收时间和分类写入ledger及有/无PCM的回放行，新增固定槽与三层覆盖。
- `Analysis/tests/test_toa_replay.py`：补上下文、固定槽、缺首槽/中间槽、重复/额外/歧义、D40及P1-04/D50式端到端反例。
- `Docs/Milestones/P1_ReadOnly_Analysis_2026_10_26.md`：本记录、覆盖含义和原描述修正。
- `Docs/Project_State.md`、`Docs/Document_Status.md`：更新当前状态和阅读入口。

主机调用链：`main → playback_context → classify_playback_time → aggregate_diagnostics → playback_diagnostics / fixed_slot_coverage`。没有新的STM32调用、IRQ、DMA或缓存关系，No CubeMX change required。

### 输出约定

1. 旧`diagnostics.by_trial_version`、全量版本差异和`version_validity_delta`原值保留，标记`population=legacy_all_events`。
2. 新`diagnostics.playback_window.by_trial[trial]`保存上下文、各类型`phase_counts`及三层覆盖：`capture_log`、`linked_sound_pcm`、`by_version`。播放内有效性变化另存`playback_window.version_validity_delta`，不提前过滤估算无效的一方。
3. 接收分析窗为`[S,S+D+2s)`，S为精确API启动秒、D来自WAV，2s明确为接收保护；同时输出WAV结束时刻。`play_span[1]`不作播放结束。每条事件增加`host_sec`、`time_phase`、`playback_relative_s`、`expected_slot_id`、`slot_residual_s`等；缺失/冲突/非法参考为UNKNOWN。
4. 固定槽来自WAV声明起点，P1为`S+1,3,…,19s`，容差±0.95s；不使用最小TOA重锚。槽数量及参考来源明确输出，`coverage_complete`只表示所声明参考被覆盖；本次P1验收另外核对确为十槽。
5. `coverage_complete`、`unique_complete`分开；重复、额外、orphan、ambiguous及unknown均保留来源。捕获日志粗覆盖不能替代唯一SOUND→capture→PCM或数值TOA覆盖，版本有效性对照也不等于唯一关联覆盖。

### 已有数据复算结果

五个原cohort的原诊断字段、`conservation/per_trial`及每条回放已有字段/TOA数值逐项一致；旧严格全10槽仍为12/15、3/5、5/5、4/5、4/5。新增播放窗字段与独立原始日志参考逐trial一致。

下表D50和D+30分别额外包含原来因格式错误排除的01号；不能将它们计为严格工具通过的trial：

| 组 | 捕获日志十槽覆盖 | 捕获恰十条且各槽唯一 | 唯一关联十槽覆盖 | current_envelope数值TOA十槽覆盖 |
| --- | ---: | ---: | ---: | ---: |
| 音量 | 15/15 | 11/15 | 14/15 | 14/15 |
| D40 | 4/5 | 3/5 | 4/5 | 4/5 |
| D50（含01） | 6/6 | 4/6 | 6/6 | 6/6 |
| D+30（含01） | 6/6 | 4/6 | 4/6 | 4/6 |
| D−30 | 5/5 | 4/5 | 4/5 | 4/5 |

这仍是主机近似时间分槽及既有代码数值有效性的统计，不是标准声身份、直达波、物理TOA、PTP或测距精度通过。唯一关联列的“十槽覆盖”也不自动等于每槽只有一个事件，具体重复/额外状态必须同时读取。

关键反例全部保留：

- `p1-20261026-04`：捕获粗覆盖齐全，但第一声没有相容PCM；唯一关联仍缺slot0。
- `p1d40-20261026-01`：10条SOUND、8个capture，捕获和关联均缺slot5/6。
- `p1d50-20261026-01`和`p1d30p-20261026-01`：粗覆盖均齐全，四条prelude `search without capture`仍使单独扩展run的`TOOL_CHECK=FAIL`、退出码1。D+30-01关联仍缺slot4。
- `p1d30m-20261026-01`：5条prelude捕获、14条playback捕获，重复保留；关联仍缺slot2。
- D20全部17次（01–15、20、21）仅11号有粗覆盖，且有重复；总计26个capture=3prelude+18playback+5postlude，唯一完整0/17，继续不足。此项通过已有ledger/生产分类函数验证，不补造回放窗。

### 软件验证和证据

- TDD记录在`Logs/Detector/P1_Playback_Slots_Implementation/`：先有上下文/固定槽和非法时间RED，随后GREEN；最终交接46项主机测试通过。
- 主代理独立运行`python -B -m unittest Analysis.tests.test_toa_replay Analysis.tests.test_pcm_event_ledger -v`：46项通过；元数据3项、原相关分析4项通过，共53项不同主机测试。
- 六组真实输入新回放使用原命令入口及`--skip-synthetic`，前五组退出0/严格PASS，扩展格式错误组按预期退出1/严格FAIL；不以此重复验证前沿算法或声学合成精度。
- 独立规格及代码质量审查均PASS，无必须修正项；规格审查另以50组内存样本与ad8ef66差分，旧诊断数值兼容。
- 保护检查16894个文件哈希未变化，涵盖51条用户改动展开后的文件、五个旧run、原P1采集、估算器和采集工具；验证时两Python源码与实际回放版本一致。

完整本次证据根：`Logs/Detector/P1_Playback_Slot_Verification/20261026T131734/`。其中`acceptance.json`、`combined-metrics.json`、`D20-negative.json`、`root-host-tests.txt`、`related-tests.json`和`protection-check.json`记录验证。新run均在该根的`runs/`，未覆盖旧目录：

| 输入cohort | 原run | 本次新run |
| --- | --- | --- |
| 音量 | 20261026T115438 | runs/volume/20261026T135433 |
| D40 | 20261026T123127 | runs/D40/20261026T135433 |
| D50原五轮 | 20261026T124005 | runs/D50/20261026T135433 |
| D+30原五轮 | 20261026T124752 | runs/Dp30/20261026T135444 |
| D−30 | 20261026T125349 | runs/Dm30/20261026T135444 |
| 两个prelude格式错误trial | 保留原采集 | runs/prelude_format_errors/20261026T135456 |

本次主机GCC适配器在原流程下编译/运行O0/O2。没有STM32构建目标变化，无需重新构建或烧录固件。原始采集来自此前实板，本轮验收是离线工具验收。

当前增量到此停止。下一步应依据保留的SOUND/capture/PCM缺口，规划定位D40-01与D20的捕获/任务问题，不能再仅凭背景污染解释全部缺失，也不直接改0.80或前沿算法。
