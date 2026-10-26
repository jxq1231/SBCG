# P1 受控声学采集：初步音量扫描与全10槽复核

<!-- doc-status: p1-preliminary-volume-scan-full10-not-stable; reviewed: 2026-10-26 -->

## 结论

P1 受控采集工具和首批真实试跑完成。工具能记录 BOOT/UID、镜像SHA、声道、实测RATE、WAV 10个预期起点和前后12秒静默；用现有账本/回放工具能做全事件与全10槽复核。

初步音量扫描（同一位置、同一20秒WAV）：

- `p1-20261026-01`（vol=100）：12 SOUND/12窗，current_envelope覆盖slot0..9并有背景slot13，**10/10槽**。
- `p1-20261026-03`（vol=80）：10 SOUND/10窗，current_envelope覆盖slot0..9，**10/10槽**。
- `p1-20261026-04`（vol=60）：11 SOUND/10窗、1个无PCM候选；按最小TOA归槽只出现 slot0,6..14，**未得到与播放槽一致的10/10**。
- `p1-20261026-05`（vol=100重复）：13 SOUND/13窗，slot0,2..11,15（slot11有重复），**未得到10/10**。
- `p1-20261026-02`（vol=100）因BOOT/UID元数据缺失被工具判FAIL，**不计入受控证据**；原始采集保留。

结论：**全10槽在本批4个有效trial中仅2个成立，尚不稳定**；不能把单次10/10当作P1通过，也不能据此说标准音识别或物理TOA通过。下一步应继续重复并检查门控/候选结构，而不是先改识别算法。

## 工具与流程

| 文件 | 变更 |
| --- | --- |
| `Tools/Monitor/pcm_trial_metadata.py` | 新增：WAV参考、BOOT/UID/CH/RATE解析、必需元数据校验与manifest |
| `Tools/Monitor/run_pcm_trial.py` | 增加 `--controlled/--reset/--image/--expected-fw/--reference-manifest`；记录镜像SHA、前后静默、播放参考；写`trial_manifest.json`与`Logs/Chirp/<trial>/run.jsonl`供账本/回放使用 |
| `Tools/Monitor/tests/test_pcm_trial_metadata.py` | 3项主机测试（WAV参考、串口解析、缺失/错配元数据） |

受控流程：串口/UDP先开 → openocd `reset run`（只复位，不烧录）→ 捕获BOOT/UID → 12秒静默 → 播放标准WAV → 12秒尾部静默 → 写manifest/串口/PCM。

## 共同条件

- 镜像：`SBCG-M4.2-PCMCAP`；本地Debug ELF SHA256 `286940ae…`；`openocd flash verify_image` exit0。
- UID：`001A002D:30365105:36363631`；CH=0；RATE观测47976/48000。
- WAV：`standard_chirp_2-6khz_10x_20s.wav`，SHA `56dedc45…`；48kHz/单声道/20s；预期起点1,3,…,19s。
- 播放：`winsound`异步；`play_span_api_s`约12.0–12.18s只是API启动跨度，不是20s声学时长；前/后静默各12s。
- 距离/音量：dist=20–50cm、vol由Windows wave音量设置；距离和音量是操作者标注，不是独立测量。

## 全10槽与回放结果

共同回放run：`Logs/Detector/TOA_Replay/20261026T113112/`（HEAD=`51924f7`，代码等价当前；默认两版）。

| 项目 | 结果 |
| --- | --- |
| TOOL_CHECK | PASS；`format_errors=[]`、`hard_failures=[]` |
| 守恒 | 4527串口行→204事件→46 SOUND→45可回放窗；54 PCM、9未用 |
| 重复性 | 11852行重复检查0失配；O0/O2零失配 |
| 逐trial问题 | 01/03/05 problems空；04有1条“sound outside every capture”覆盖缺口，未使TOOL_CHECK失败 |
| slot0..9 | 只在trial 01、03完全成立；04/05因候选缺失/额外候选不能成立 |

名义采样轴间隔（current_envelope O0）：

- trial01：相邻约1,999,641.7–1,999,653.4µs，相对2s偏差约−346.6～−358.3µs。
- trial03：约1,999,423.1–1,999,871.7µs，偏差约−128～−577µs。
- 这些是48k名义轴/源时钟/播放时钟的比例差与候选结构混合，**不是物理传播时间，不是±100µs验收**；未做PTP换算。

## 边界

- 只有一个位置/音量标签，单批trial不足以估计识别率或物理精度；vol60/100重复未覆盖全10槽，说明候选/门控结构仍需更多重复。
- 镜像SHA通过本地ELF+`flash verify_image` exit0绑定；未写入/读取独立Flash哈希。
- 播放前后“静默窗口”内仍有背景候选（如before=1、after=10–12），不是纯静默；未删除额外候选。
- 标准音识别仍0/10；PTP、双板、测距未接入未验收。

## 下一步

1. 在同一位置补足 vol=100/80/60 各至少5次有效trial，逐次保留manifest/PCM/串口；重点统计全10槽率、缺失槽、额外候选和间隔分布。
2. 距离/方向组需要用户移动扬声器/板后再做，不能由当前软件单独改变。
3. 只有重复数据稳定后，才选择一个最小修正（门控提示/候选结构/前沿），并用现有账本/回放对比；不先改0.80或删除背景。
4. 若BOOT/UID偶发缺失（如trial02），工具必须FAIL并重跑，不把该trial计入证据。

证据：`Logs/Chirp/PCM/p1-20261026-*/`、`Logs/Chirp/p1-20261026-*.jsonl`、`Logs/Detector/TOA_Replay/20261026T113112/`（另：单trial全10槽run `20261026T112232/`）。
