# 软件、双板实机与正式指标验收手册

<!-- doc-status: reviewed-acceptance-procedure-not-results; reviewed: 2026-10-28 -->

## 1. 角色和准备

集成负责人只执行当前一个增量；软件复核者检查边界与失败用例；实验记录者保留全部尝试；操作者负责手机、摆位、尺量和实屏视频。一个人可以兼任，但日志里标明实际操作。

- A：UID `001A002D:30365105:36363631`，历史COM3，IP192.168.10.1；B：`00380050:35385104:37313532`，历史COM5，IP192.168.10.2。端口每轮以BOOT核对。
- A/B接同一交换机。PPS历史示波器10次和30分钟记录按原镜像/范围保留，不复测，不外推成任意新固件长期精度。
- 固定麦克风开孔中心、声源实际出声孔和高度。A侧`source—A—B`应有delta>0；B侧`A—B—source`应有delta<0。
- 0.320m只是开发夹具真值；任何滤波/拒绝算法不得读取该值作为运行目标。离线评价器可以使用真值。
- 板子与声源使用支架，避免手持和身体移动反射；记录距桌面/墙面、音量、朝向、手机双出声孔。遮听筒只作单变量诊断，不默认为所有播放设备的使用前提。
- G0确认计时合同前，分别报告播放器启动→首声、首声→临时数字、首声→首个可信结果、首声→稳定确认。不能将2秒结果TTL当成2秒首显要求，也不能用临时数字代替正式识别。
- 若现场仅有单板、无网线或无第二路屏幕，只做相应软件/单板诊断；双板配对、方向和两屏一致一律NOT_TESTED，不能用主机桩冒充实机。无示波器不阻塞本路线的软件/声学实验，已有外部证据仍按原范围保留。

## 2. 新增主机采集工具资格（O1的独立子增量）

**唯一目标：**双板日志/PCM不混流，完整播放窗和负样本均可追溯。

**拟新增：**`Tools/Monitor/dual_acoustic_capture.py`、`Tools/Monitor/tests/test_dual_acoustic_capture.py`；必要时扩展`udp_chirp_capture.py`公共解码函数，保持单板CLI兼容。

现有`run_pcm_trial.py`会PC播放且只开一串口，不能直接作为双板正式工具。现有UDP接收仅按session/id组装，A/B同号会混流；不得直接改个COM5参数就认为隔离完成。

**新工具接口（实现并自测后才执行）：**

```powershell
python -B Tools/Monitor/dual_acoustic_capture.py --a-port COM3 --b-port COM5 --duration 90 --out Logs/Acceptance/<new-run> --mode passive
python -B Tools/Monitor/dual_acoustic_capture.py --a-port COM3 --b-port COM5 --duration 90 --out Logs/Acceptance/<new-run> --mode diagnostic-pcm --bind 192.168.10.100 --udp-port 6008
```

- 默认passive：只接收串口，不写串口、不reset、不播放、不发UDP。两口就绪后释放共同单调时钟起点；任一打开失败全体关闭、产物写FAILED。
- diagnostic-pcm额外监听PC6008，保存逐包来源IP/端口、接收时刻及原始bytes/base64；按`(board UID, boot context, source IP, session, capture_id)`分组，不覆写。新boot未绑定时保存UNBOUND原包，不能猜板。
- `.pcm`和元数据路径含板卡/启动/会话/ID；相同元数据重复块计数，相同块序号不同payload是冲突错误，禁止混合。
- 只收“成功候选片段”不能证明无漏声；需保存每个预期槽的固定窗口或连续PCM负样本。A4/B1的真实连续数据依赖下述O2；未完成时标`BLOCKED_PCM_COVERAGE`，主机监听器不能创造板端未发送的样本。
- `A/B-serial.bin`逐字节保留；JSONL是投影。首行截断/末尾hex保留并注明边界，不能静默丢掉潜在事件。采集65秒可取共同[2,62)为完整60秒负样本窗。
- 输出只写新目录，写manifest、summary、events、各端口闭合状态、所有错误；不自动标硬件PASS。
- fake I/O测试覆盖同号混板、boot变化、乱序、重复冲突、半包、坏magic/version/length/metadata、端口失败、中断、文件冲突、分母槽缺失、两口就绪屏障。现有PCM1无CRC字段，不擅自改线协议；声音CRC属于B路线。

**现有工具仍可用于正式旁观：**先建新run目录，两终端分别执行，二者起点不同，分析时不能直接相减各自elapsed_s。

```powershell
python -B Tools/Monitor/serial_monitor.py --port COM3 --duration 120 --log-prefix Logs/Acceptance/<run>/A
python -B Tools/Monitor/serial_monitor.py --port COM5 --duration 120 --log-prefix Logs/Acceptance/<run>/B
```

手机手动播放，PC只用USB记录。正式至少一轮完全断开PC，手机播放并录像双屏；不得由主机TEMP、命令、重启或UDP请求驱动出数。PC接网抓PCM的轮次标DIAGNOSTIC，不冒充离线自动正式验收。

### O2：连续PCM诊断采集子增量（单独领取、单独镜像授权）

**唯一目标：**从真实End通过的CPU副本连续保存每板声学窗口，包括无候选/未检出的时段，为A4/B提供有效正负样本。

**准确文件：**新增`App/Audio/pcm_stream_service.c/.h`、`Tests/Audio/test_pcm_stream_service.c`；修改`App/Audio/audio_capture.c`、`App/Network/udp_chirp_capture.c/.h`、用户`cmake/audio-layout.cmake`与`App/app_main.c`最小模式/镜像标识接线；扩展`Tests/Audio/run_tests.py`、`Tests/ChirpService/run_pcm_capture_tests.py`相关测试，以及O1主机接收测试。布局变化只走用户链接扩展及布局检查。

- 复用现有5008→PC6008 PCM1分片编码和PCB，增加有界`SendChunk`入口；不另占UDP PCB，不变更CubeMX。每个完整8192帧单声道块16个512帧分片；block序号作为capture_id，FIRST连续可核对。
- Stage在唯一借用期只拷贝指定声道；Complete收到真实End_ok后才可把该块提交发送。End失败、缺块、会话/代际变化均报告，不能伪造零块补齐。
- 仅诊断编译模式启用，旧大ChirpService缓冲/诊断sink与其互斥；不能用`SBCG_USE_CHIRP=0`关闭ifdef路径。保留原测量检测链，不能把新PCM流当测量前提。
- 使用两个8192帧CPU银行作有界队列；优先一个银行复用17088字节capture保留区，另一个16384字节由停用旧22528+5312字节普通CPU缓存释放空间承接。实际布局/栈/堆不满足则停，不能写进DMA保留区或超预算盲扩RAM。
- Process每次至多4片且≤2ms；发送半块16片需在下一个可用银行耗尽前完成。单板payload约96kB/s、含PCM1头约101.25kB/s，两板约202.5kB/s；100Mbps理论容量不代表主循环处理已过关，必须测量。
- 队满、pbuf失败、链路断开、send失败均计数/标该块不完整，不覆盖仍待发送银行；日志最多1Hz汇总，不每片printf，不在IRQ发送。
- 主机验证每块16片、源IP/boot/session/ID、FIRST递增、长度、原始payload；丢一整块也必须由预期FIRST发现。原始连续记录不能由候选起点重新编号。

**后续命令（O2实现并取得镜像授权后）：**

```powershell
cmake --preset Release -DSBCG_PCM_STREAM_DIAGNOSTIC=ON
cmake --build --preset Release --parallel
python -B Tests/Audio/run_tests.py
python -B Tools/Monitor/check_audio_layout.py --preset Release
```

诊断构建用既有Release preset的用户选项，不创建第二构建体系；复制ELF及hash到新Logs附件再烧录。实验后以同一preset设置`-DSBCG_PCM_STREAM_DIAGNOSTIC=OFF`重建正常模式，恢复烧录仍须匹配当前授权，不能仅认为关闭cache就是已恢复板卡。

**软件必验：**先写银行占用/真实End拒绝/队满/分片乱序/链路失败/源序号回绕/重启/消费者慢于生产/停止后资源释放用例。证明Stage不发送、End失败0片、发送不修改CPU活动块、同一真实块只提交一次。

**硬件资格：**diag同版本A/B，至少65秒连续静默与3轮标准声；取[2,62)完整负样本，PCM块覆盖100%、无分片或整块缺口、无串流/冲突，音频DROP/ERR/End失败、PPS故障/撤域、UART日志错误均无新增。失败标`PCM_STREAM_NOT_QUALIFIED`，先修采集，不交给检测agent伪造/猜测。

该增量只证明诊断数据可用，不证明标准声识别。由固件集成者负责，O1只写主机工具的agent不得越权完成它。

## 3. 每轮固定账本

每轮必须先生成10行slot0..9，播放当前WAV时用0/2/…/18s；B候选WAV按其自身清单0.4/2.4/…/18.4s，不复用旧起点。

最小字段：

```text
trial_id, slot, expected_onset, reference_kind, reference_uncertainty_ms,
A/B_uid, boot, session, event_seq, candidate, class, PCM_complete,
TOA_status, TOA_frame_Q16, TOA_common_ns, epoch, rate_revision,
pair_status, delta_ns, domain, sound_profile, source_id, decoded_slot,
temperature_version, sound_speed_mm_s, calibration_version,
raw_pair_mm, temperature_corrected_raw_mm, calibrated_raw_mm,
filter_decision, reason, window_member_ids,
measurement_id, state_revision, state, evidence_kind,
filtered_mm, displayed_A_mm, displayed_B_mm, direction_A/B,
source_common_ns, fresh_deadline_ns, display_deadline_ns,
first_display_A_ms, first_display_B_ms, failure_stage
```

事件归属使用源键/decoded slot/共同时间和参考窗，不能按输出日志先后或最小TOA重排。最后一声延迟输出在尾窗，不算新的静默误报；背景与额外候选另列，不删除。

**四类结果必须分开：**

1. 原始：所有预期发声及其原始delta；`SOUND_PAIR.DIST_MM`固定343000，最终温补值另算。
2. 新确认测量：独立源事件使measurement_id前进，记录滤波前后值及接收/拒绝。
3. HOLD：旧ID/无新源证据的历史展示，只统计时长、次数、最大年龄，不充当新样本。
4. 缺失/拒绝/STALE：留在分母，不用0替代后计算标准差，不因“不是有效样本”从成功率表里消失。

输出至少`N_expected/N_PCM/N_candidate/N_TOA/N_pair/N_new_provisional/N_new_confirmed/N_new_formal/N_hold/N_missing`、均值偏差、最大绝对误差、sample std(ddof=1)、误差P95、方向错误、重获延迟、最长HOLD/STALE。重叠中值窗输出相关，其σ只代表显示描述，不等于独立原始精度。

已有单板`toa_replay.py`要求历史`t/run.jsonl`和冻结窗，不能直接拿双板`elapsed_s`文件运行并声称完整服务/双板回放。若转换，生成有原文件hash/行号的派生输入，原件保持不动；格式错误仍报错。

## 4. 软件验收矩阵

先写失败测试并观察失败，再实现；实现者按改动选相关runner，完成后不无理由重复全仓测试。

| 层 | 必须覆盖 | 断言 |
| --- | --- | --- |
| 基线/槽账 | slot0缺失、背景、额外声、未知播放起点 | 固定分母；证据不足是EVIDENCE_INVALID，非猜测漏检 |
| tracker | 2秒三点；离群1/2/3个；互不一致离群 | 第3个一致新簇才更新，拒绝不清旧窗 |
| tracker | 同A或同B源重用、重复、乱序 | 不增长独立证据、不续期 |
| tracker | 320→500→320、320→330→320 | 自动确认新位置；不是永久钳位旧值 |
| tracker | 符号翻转、温补、标定、域/流/session | 元数据一致；不混窗；硬故障撤销 |
| 时钟 | common_ns倒退/读失败，毫秒回绕 | 不形成负年龄或永久有效值 |
| wire | 全状态、短包、未知flags、非法组合 | 大端/长度一致；错误包不改变有效状态 |
| exchange | ACK丢、错ACK、同revision篡改、旧包重放 | 只确认精确revision；不刷新源截止 |
| App | 音频→TOA→pair→tracker→RST2→B | 实际模块接线，不以桩绕过核心边界 |
| UI | VALID/HOLD/REACQUIRE/STALE/FAULT | 保持有标签，过期无数字，同revision同值 |
| 音频服务 | 跨8192边界、随机分块、End拒绝、缺后继 | 不读归还后DMA指针、不伪造PCM或TOA |
| 同步回归 | phase/rate代际、PPS、DOMAIN | 不跨代插值，不由音频DSP改变伺服 |

**已存在的命令：**

```powershell
python -B Analysis/prepare_standard_audio.py --check
python -B Analysis/verify_standard_selection.py
python -B Tests/Measure/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/App/run_app_udp_integration.py
python -B Tests/Udp/run_udp_sound_event_tests.py
python -B Tests/SoundDetectorService/run_tests.py
python -B Tests/SoundEventTime/run_tests.py
python -B Tests/Detector/run_event_classifier_tests.py
python -B Tests/Detector/run_standard_sequence_tests.py
python -B Tests/Detector/run_toa_estimator_tests.py
python -B Tests/Audio/run_tests.py
python -B Tests/ClockEpoch/run_tests.py
python -B Tests/BoardSync/run_tests.py
python -B Tests/PpsService/run_tests.py
cmake --build --preset Debug --parallel
cmake --build --preset Release --parallel
python -B Tools/Monitor/check_audio_layout.py --preset Debug
python -B Tools/Monitor/check_audio_layout.py --preset Release
```

当前用户未提交代码若使基线测试失败，先保留失败输出并定位其归属，不能把预期测试改成绿色或把修改清理掉。资产check只证明样本/哈希合同，不证明识别。

## 5. 硬件分级闸门

烧录前交付代码差异、软件结果、构建/布局、精确SHA、A/B协议版本和回退产物；只有当前镜像获授权才烧录。两板必须同版本。没有示波器也可做本节串口与实屏，但不能由此增加外部时钟精度结论。

| 编号 | 执行条件与次数 | 通过门槛 |
| --- | --- | --- |
| H0 启动共存 | 新镜像后旁观90秒，不播放 | BOOT/UID/角色正确，DOMAIN有效同token；PPS FAULT/MISSED/ERRORS无新增；音频/日志错误0；UI可读 |
| H1 固定几何 | 0.320m，A侧/B侧各3轮，每轮10声 | 60槽全账；每轮A/B候选、有效TOA、唯一pair均须10/10才算声学闭合；无串槽/错方向；新确认/正式覆盖另判，不以HOLD补数 |
| H2 真实移动 | A和A侧声源固定，B 320→500→320及320→330→320mm，各3次 | 停稳后第3个一致新证据自动换簇，允许旧值明确保持；新簇不形成则到6.5秒硬撤数字，不无限锁旧值 |
| H2b 微扰 | 仅声源在外侧同轴±10mm，板距不变，各3轮 | 理论声程差不变；比较raw及输出跳变/拒绝率，不要求raw随人为显示平滑而通过 |
| H3 新鲜度 | 每轮播放/停止/静默≥7秒/重播，共3轮 | 共同deadline不被重传续租；两屏标签/数值符合revision；恢复不依赖reset/PC命令 |
| H4 静默 | 上电后、成功播放后旧结果过期、换设备后各65秒，取[2,62) | 每窗60秒任何新数值测量含临时值均0，标准包身份0；背景候选保留；前后正样本确实可检测 |
| H5 瞬态 | 播放器实际启动点击/淡入等5轮 | 可拒绝受扰包；下一干净合法包自动恢复；不依赖人为清状态；不算CLAP验收 |
| H6 换设备 | 不改程序/WAV/标定，手机→另一单扬声器设备；0.320m两侧各3轮 | 初筛最大绝对误差≤15mm，无错方向/串槽；记录音量和出声点，不用每设备重调门限 |
| H7 基础量程 | 0.20/0.32/0.50/1.00/1.50/2.00m，两侧逐点 | 先每条件1轮10声筛选；失败立即停在该条件诊断，不盲跑后续 |

H1硬件合同包括“确认值不被孤立离群拉走”和“两屏状态一致”；若原始误差/拒绝仍严重，标`DISPLAY_BEHAVIOR_PASS_ACOUSTICS_FAIL`，不能作为完整测距通过。

H1的A1初次3声确认窗口必须单列：例如10对都有，但只生成8个新确认结果，不能写10/10新结果。若G0要求每声都有可信新结果，该项为FAIL并指向A4/B。精度按温补后的正式新结果判±10mm、独立重复σ≤2mm；raw全部报告，不额外把“raw σ必须≤2mm”冒充任务要求。若raw仍出现>15mm离群/稳定错簇或靠拒绝/保持才消失，标原始链风险未闭合，转A4而不直接进入H7；只有小幅raw噪声而独立正式结果确实合格时，不强制换音频。

H2的“第3个”来自停稳后的三次独立声音，2秒周期约需4秒；若旧证据在移动前一次，此过程常在6秒内。不是承诺4秒内任意运动实时定位，也不是改动正式首声时限。

**双屏证据：**同一视频同时录到声音和两屏；对应`UI_RANGE`的revision/ID核对数值、方向、保持/失效。串口快照不等于实际面板呈现；没有用户实屏观察，不提交显示完成结论。

## 6. 正式基础指标门槛

只有H1～H6通过、身份检测资格与计时合同明确后，开展完整基础验收：

- 范围0.20～2.00m；单次绝对误差≤10mm；同一声音两屏差≤1mm，方向正确。
- 重复性至少10次独立发声；若使用跨事件滤波结果主张独立σ≤2mm，则至少10个独立播放轮次，轮次间静默≥7秒，按事先指定的同一槽取样。不能事后挑第一个好值或12个值中挑10个。
- 工程筛选用一轮10声的全账即可；正式独立轮次较长，先在0.32/0.50/2.00m验证，再补完整矩阵。未执行的点明确NOT_TESTED。
- 所有正式误差与σ按“距离×方向×播放设备”分别统计，不混池。最终主张通过的每个条件至少10个独立轮次；事先指定槽缺失记该轮失败，不换槽或跳过该轮。
- 计时分别给播放器启动、实际首个声包、临时、正式、稳定、面板显示。若G0确认严格2秒正式首显而A路线仍依赖3次周期，则该项FAIL，进入A4/B，不以临时值代替。
- 当前未实现板端标定入口，全部基础数据明确calibration_version；不能用离线拟合/手动删偏差宣称单点标定通过。
- 温补单独在固定几何下改变手动温度，检查当前声速和距离按公式变化、偏差≤2mm；自动温度环境试验按后续独立温补验收，不把实时温度字段存在等同通过。

5m/10m、±5mm标定、击掌类型隔离、同步波形均作为基础路线完成后的独立计划；本文件不自动启用。

## 7. 失败分流和停止条件

| 最早断点 | 必须先核对的证据 | 唯一下一方向 |
| --- | --- | --- |
| 起点/哈希不明 | manifest、视频/播放边界 | EVIDENCE_INVALID，补采新run，不调算法 |
| PC PCM缺片/混板 | 板端提交与逐包journal | 主机工具，不动识别门限 |
| 无候选 | 输入通道/幅度/完整时窗 | 声源/采集/低成本门控 |
| 有候选非标准 | 完整分类窗与身份输出 | 身份算法，不先延长HOLD |
| TOA缺失 | WINDOW_WAIT/LOST、End/Session、NO_LEFT | 窗口或映射代际；必要时B的短周期锚点子增量 |
| 两侧有TOA无pair | 同域、身份、源序号、超时/歧义 | 网络/配对 |
| pair有但raw错 | PCM、峰候选、几何、温度、映射 | A4；条件满足才B |
| raw正确但结果错 | tracker决策、gate、温补版本 | A1/A2 |
| A正确B错/晚 | RST2/RAK2、revision/deadline | A2协议，不重做PTP |
| 模型正确屏幕错 | UI快照与实屏视频 | UI呈现/刷新 |
| 显示很稳但大量HOLD或稳定错误簇 | 全槽覆盖、偏差、保持比例 | 未通过；不扩大TTL，不删失败槽 |

每个根因最多两轮有证据的窄修复；仍不理想时负责人审查假设/几何/方法并决定B，不开展无边界调参。丢失/失败轮仍进入“全部尝试”表；补采必须新run，不能覆盖旧证据。

## 8. 每个增量的交付模板

```text
Increment / 唯一目标:
Baseline HEAD / working patch SHA / WAV SHA / A,B ELF SHA:
Changed files / 用户原有修改如何保留:
CubeMX / IRQ / DMA / Cache: 无变化，或指向明确批准记录
Software: 具体命令、退出码、失败前证据、通过后输出
Build/Layout: Debug/Release和散列
Hardware: 授权镜像、run目录、条件、全部尝试
Counts: expected/raw/TOA/pair/new/hold/missing
Metrics: bias/max error/sample std/方向/年龄/重获延迟
Screen: 观察者、视频或照片、同ID两屏结果
Unclosed: NOT_TESTED / FAIL / EVIDENCE_INVALID分别列明
Git: 定向文件、commit、author/committer=jxq
Next: 只建议下一编号，不跨闸门自动实施
```
