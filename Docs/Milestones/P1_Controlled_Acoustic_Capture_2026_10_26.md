# P1 受控声学采集：单次试跑与全10槽复算

<!-- doc-status: p1-single-trial-captured-full10-slots-nominal-only; reviewed: 2026-10-26 -->

## 结论

P1 受控采集工具与一次真实试跑完成：

- trial `p1-20261026-01`；A板镜像 `SBCG-M4.2-PCMCAP`，本地 Debug ELF SHA256 `286940aeef473bf79595d095a232ed447e2bf1c94e5f85f0ae3cef0d5ba18937`；`openocd flash verify_image` exit0，说明板上Flash与该ELF一致。
- 记录到：BOOT/UID、镜像SHA、`CHIRP_INIT CH=0`、实测 `AUDIO RATE_HZ=47976`、WAV SHA256/10个预期起点（1,3,…,19s）、播放前后12秒静默窗口、播放API时段、13个PCM、1130条串口行。
- 用现有账本/回放工具复算：**current_envelope 覆盖 slot0..slot9（10/10）**，另有1个背景候选落在slot13；`TOOL_CHECK=PASS`、`format_errors=[]`、`hard_failures=[]`、重复11720行0失配。
- 这证明受控采集和全10槽回放链路可用；**不证明标准音识别、物理TOA、PTP或双板测距通过**。

## 工具与流程

| 文件 | 变更 |
| --- | --- |
| `Tools/Monitor/pcm_trial_metadata.py` | 新增：WAV参考、BOOT/UID/CH/RATE解析、必需元数据校验与manifest |
| `Tools/Monitor/run_pcm_trial.py` | 增加 `--controlled/--reset/--image/--expected-fw/--reference-manifest`；记录镜像SHA、前后静默、播放参考；写 `trial_manifest.json` 和 `Logs/Chirp/<trial>/run.jsonl` 供账本/回放直接使用 |
| `Tools/Monitor/tests/test_pcm_trial_metadata.py` | 3项主机测试（WAV参考、串口解析、缺失/错配元数据） |

受控流程：开机后先起串口/UDP采集 → openocd `reset run`（只复位，不烧录）→ 捕获BOOT/UID → 12秒静默 → 播放同一标准WAV → 12秒尾部静默 → 写manifest/串口/PCM。

## 本次条件与元数据

| 项 | 值 |
| --- | --- |
| trial | `p1-20261026-01` |
| 标签 | `p1;dist=20-50cm;vol=100;standard=10x20s;controlled`（距离/音量为操作者标注，不是独立测量） |
| 镜像 | `SBCG-M4.2-PCMCAP`；本地ELF SHA `286940ae…`；flash verify exit0 |
| UID | `001A002D:30365105:36363631` |
| 声道/采样率 | `CH=0`；`RATE_HZ=47976`（串口实测） |
| WAV | `standard_chirp_2-6khz_10x_20s.wav` SHA `56dedc45…`；48kHz/单声道/20s；预期起点1,3,…,19s |
| 静默/播放 | 前12s、后12s；播放API时段12.137–12.184s（`winsound`异步启动跨度，不是20s声学时长） |
| 采集 | 串口1130行；UDP完成13个PCM；12个SOUND、12个可回放窗口；1个PCM未配SOUND |
| 播放前/后SOUND候选 | 1 / 11（尾部含背景候选，不是纯静默） |

## 全10槽复算

回放run：`Logs/Detector/TOA_Replay/20261026T112232/`（HEAD=`56c28f4`，默认两版；current_envelope）。

| 项 | 结果 |
| --- | --- |
| TOOL_CHECK | PASS；format/hard errors空；重复11720行0失配；O0/O2零失配 |
| 守恒 | raw1130→51 events→12 SOUND→12 WINDOW；PCM13、unused1；0 orphan/歧义 |
| current_envelope slot | **slot0..9全出现**；samples=11（多出的背景候选映射到slot13）；missing=0、collisions=0、`verdict_possible=true` |
| 峰值版本 | 该次数据下peak_backtrack仅slot0可判，不能与current并列做10槽结论 |

slot0..9的名义采样轴TOA偏移与2s间隔（`Logs/Chirp/PCM/p1-20261026-01/replay_slots.json`）：

- 相邻间隔：1,999,641.7～1,999,653.4µs（相对名义2s：−346.6～−358.3µs）。
- 该约−0.35ms/2s的系统差与48kHz名义轴/实际源时钟/播放时钟比例一致，**不是物理传播时间、不是±100µs验收**；未做PTP换算。
- 多出的slot13背景候选保留在账本/回放中，不删除以凑10槽。

## 结论边界

- 全10槽只针对本次单trial、current_envelope、名义48k采样轴；不是全10次跨轮重复性，也不是A/B双板结果。
- 镜像SHA通过本地ELF+`flash verify_image` exit0绑定；若后续更换镜像需重新记录。
- 没有独立物理到达参考；播放API跨度不是声学播放时长；前后“静默窗口”内仍有背景候选。
- 标准音识别仍为0/10（M4.2.5）；PTP/双板/测距未接入未验收。

## 下一步

1. 用同一命令重复受控条件（至少距离/音量/方向各5次），每次写独立trial/manifest，保留slot值/候选计数/错误。
2. 对多次完整10槽数据用现有账本/回放聚合，再选择一个最小修正；不在本轮改0.80或构造匹配。
3. 若更换镜像，先登记镜像SHA并复验；任何烧录仍需单独授权。

证据：`Logs/Chirp/PCM/p1-20261026-01/`（trial_summary/trial_manifest/replay_slots/PCM）、`Logs/Chirp/p1-20261026-01.jsonl`、`Logs/Detector/TOA_Replay/20261026T112232/`。
