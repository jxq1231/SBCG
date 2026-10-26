# 当前项目状态

<!-- doc-status: current-state; reviewed: 2026-10-26 -->

更新：2026-10-26。本文仅描述现在；历史实验按需查[文档状态表](Document_Status.md)及`Milestones/`，不重复执行旧计划。

## 当前工作

**当前已完成P1播放阶段分类与固定参考分槽的软件验证。** 基于ad8ef66增加主机分析口径，保留旧全量诊断为`legacy_all_events`，另列播放窗的捕获日志覆盖、唯一SOUND→capture→PCM覆盖和各版本数值TOA覆盖；不以最小TOA重锚。53项相关主机测试通过，五个原cohort旧诊断/TOA数值逐项兼容；两个扩展trial的四条prelude格式错误仍严格FAIL。五个旧run、原始采集、估算器、采集工具及51条用户改动均保留，本轮无硬件操作。验证及新run位置见[P1工具实施记录](Milestones/P1_ReadOnly_Analysis_2026_10_26.md)。

播放窗捕获日志十槽覆盖为音量15/15、D40 4/5、D50 6/6、D+30 6/6、D−30 5/5；但唯一关联覆盖分别为14/15、4/5、6/6、4/6、4/5，不能混为声源识别或精度通过。D40-01有10条SOUND但缺capture槽5/6；p1-04第一声无相容PCM，D−30-01仍缺关联槽2。D20的17次仅1次粗覆盖且有重复，继续不足。下一步只规划依据这些保留缺口定位捕获/任务链，不自动修改算法或0.80。

单A已有[原TOA与回放基线](Milestones/PCM_Event_Replay_2026_10_26.md)、[P1受控采集](Milestones/P1_Controlled_Acoustic_Capture_2026_10_26.md)、温度和显示基础；标准声识别、物理TOA/PTP精度、双板配对/距离仍未通过。本轮只处理主机接收近似时间：20s WAV加2s接收保护，不是实际发声参考。后续目标仍按[任务书路线](Goal_Aligned_Roadmap_2026_10_26.md)分层推进。

### 历史窗口摘要（按需追溯，不作为当前下一步指令）

**最新实测：电脑扬声器标准音识别未通过，下一步先诊断声学匹配。** 用户确认清楚听到10次扫频；A板BOOT/UID已核对，62.04秒安静基线后播放20秒文件，出现10次周期能量响应、10个搜索任务完成2570次评分，但CHIRP/MATCH为0（本轮漏检10次）。音频/搜索/日志/UI错误和丢弃均0，SCAN3ms、SCORE4ms；不是新的积压失败。未修改或烧录固件，末尾41字节残片保留排除。原“不能发声、主动未验”已由此更新；先增加失败搜索的原始最高相关度与峰位置观测，再决定修复，暂缓M4.3.1接口接入。见[真实播放记录](Milestones/M4_2_Active_Chirp_Trial_2026_10_26.md)。

**此前同日软件复核：检测软件已推进至M4.2.4-A4；整机并非仅缺标准音验证。** 当时15组主机回归、Tools125/Analysis152项、Debug/Release构建及四项布局通过，未烧录或读取板卡。CHIRP匹配仅输出帧号，尚未接入本地PTP事件；双板声音事件配对、测距/方向和统一结果发布尚未实现，UI仍为占位。原建议M4.3.1“匹配帧→本地PTP模型时间”保留，但已由上述真实声学失败调整为先诊断匹配，C2仍为条件优化。完整整机缺口和误差预算见[同日复核](Progress_Review_2026_10_26.md)。下列历史镜像和数据不表示本轮全部重新实测。

**历史M4.2-DIAG定位记录，扫描瓶颈已由后续批量化改善。** 此前定位窗口A板为`SBCG-M4.2-DIAG`，Debug前缀d3e084b1d83e。BOOT后完整行61.36秒、29条诊断：扫描最大59ms、评分最大5ms、前台间隔最大172ms；搜索34次丢弃=21次任务超时+13次历史覆盖，DONE0。心跳末TX_ERRORS208全部为LOG_FULL208，LOG_WRITE/BAD0，队列峰值32；音频累计丢块/硬件错误0，无额外BOOT或扫频误报。原始文件末尾111字节残行保留，仅从分析输入明确排除，其余raw/JSON完整行一致，不能声称原文件无残片或日志连续。初版/R1/R2三次失败保留；不重复实施旧扫描优化计划。标准音频识别/到达精度仍未验收。[完整证据](Milestones/M4_2_2_Bounded_Chirp_Integration.md)。

**M4.2.3-B扫频批量接入已完成静默实机验收，主动标准音频待验。** 当时A板烧录`SBCG-M4.2-BATCH`（Debug前缀26a6496eb5ed，功能提交`743e027`）；约70秒静默采集SCAN最大3ms（原59ms）、DONE_MAX_MS=831ms、CAND32/DONE32、丢弃/日志/音频错误全0，原候选256/128与扫频128/64门限及Begin/End事务未迁移。主机合成压力已由`c6af223`完成，不再列为待做；它不是真实时序回放。MATCH 10/10、双板与声学精度未验收。[静默和压力证据](Milestones/M4_2_3_B_Batch_Scan.md)。

**M4.3.17结果重发状态模块（2026-10-27）。** A结果重发/Ack/对端ACK序号从app_main抽为`result_exchange_state`；新增B重启序号回退、域恢复恢复重发测试。主机测试PASS，未烧录/未双板。见[M4.3.17记录](Milestones/M4_3_17_Result_Exchange_State.md)。

**M4.3.16 c24b5ba review修复（2026-10-27）。** A跟踪B ACK序号/域恢复，B重启后恢复当前结果重发；域超时1500→3000ms；MRES标定偏移±1000校验；配对窗口100→250ms。P2 boot_nonce/state/Apply未修；未烧录。见[M4.3.16记录](Milestones/M4_3_16_Review_Fixes.md)。

**M4.3.15 0.5m标定/温补结果版本（软件完成，2026-10-27）。** `calibration`模块提供500mm参考偏移/版本；A发布前应用，MRES扩为32字节携带标定版本/偏移；温度声速继续参与距离。标定触发未接板端按键/界面，0.5m/±2mm未实测；未烧录。见[M4.3.15记录](Milestones/M4_3_15_Calibration_Temperature.md)。

**M4.3.14静默/瞬态测量门控（软件完成，2026-10-27）。** `measurement_gate`状态机：1s预热、标准序列才开测量、3s无新序列撤销、失域/音频会话变化FAULT；单CHIRP/击掌不打开门控。主机测试PASS，未烧录/未60s实测。见[M4.3.14记录](Milestones/M4_3_14_Quiet_Transient_Gate.md)。

**M4.3.13权威结果MACK确认（软件完成，2026-10-27）。** B接受MRES后回16字节MACK；A收到同令牌成功ACK后停止该ID刷新，ACK丢失仍500ms重发；结果状态增加ACK_TX/RX/BAD。未烧录/未两板实测。见[M4.3.13记录](Milestones/M4_3_13_Result_Ack.md)。

**M4.3.12共同域双向握手（软件完成，2026-10-27）。** 新增`DOMN` 24字节REQ/ACK：A每500ms发REQ，B仅本地严格合格才回ACK，A收到同令牌有效ACK才放行；B收到REQ且本地合格才可发事件；超时/FAULT撤域。未烧录/未示波器验收。见[M4.3.12记录](Milestones/M4_3_12_Domain_Handshake.md)。

**M4.3.11配对P0/结果边界修复（2026-10-27）。** `UDPSoundEvent_Send`现在把本端事件提交配对器，修复真实双板永不成对的结构P0；新增本端发送+对端接收配对集成测试。同时域失效立即清A/B结果、拒绝乱序旧ID、MRES拒绝UNKNOWN方向。P1域令牌已部分修复：A用UID+启动时刻派生令牌，B收到A事件后采用，不再固定常量1；但无双向握手/PTP epoch证据，仍不能当外部同步验收。未烧录。见[M4.3.11记录](Milestones/M4_3_11_Pairing_Review_Fixes.md)。

**M4.3.10 PPS资格共同域门控（未验证，2026-10-27）。** 用户PPS工作与`PpsService_GetSnapshot`只读快照已提交；`TimeDomain_UpdateWithPps`要求A/B TimeSync条件+PPS QUALIFIED/RATE_REQUALIFY，PPS FAULT/UNQUALIFIED撤域。软件/主机测试PASS，但**未烧录、未示波器验证±10µs/冷启动≤30s**；不得标记为同步验收。见[M4.3.10未验证记录](Milestones/M4_3_10_PPS_Domain_Unverified.md)。

**M4.3.9权威结果周期刷新（2026-10-27）。** A板每500ms重发当前有效结果（ID变化立即发），B按同ID刷新2s有效期；新增`$MEASURE_RESULT_STATE`TX/RX/BAD计数。无ACK/可靠传输、无结果序列号；未烧录/未两板实测。见[M4.3.9记录](Milestones/M4_3_9_Result_Refresh.md)。

**M4.3.8两屏权威结果交换软件完成（2026-10-27）。** A板发布唯一MeasurementResult并通过5009端口28字节MRES报文发给B；B校验角色/共同域后按A的原ID/距离显示，不重算。无重传/ACK、未烧录/未两板实测；B单包丢失会显示旧值至2s超龄。见[M4.3.8记录](Milestones/M4_3_8_Authoritative_Result_Exchange.md)。

**M4.3.7标准chirp/击掌波形分类接入（2026-10-27）。** `event_classifier`从候选窗口提取包络长度/前后过零率趋势/差分能量比，输出CHIRP/CLAP/UNKNOWN和quality；SoundDetectorService→SoundEventTimeEvent→App，非CHIRP不进标准序列/UDP/配对，事件quality不再固定100。仅合成测试/未真实标准声验证，未烧录。见[M4.3.7记录](Milestones/M4_3_7_Chirp_Clap_Classifier.md)。

**M4.3.6共同时间域门控软件完成（2026-10-27）。** `time_domain`由`TimeSync`派生：A配置且未FAULT、B还要求offset_valid/internal_locked；域无效时不发事件UDP并撤销MeasurementResult，替代固定诊断domain。尚未直接读取PPS QUALIFIED/未烧录，内部锁定不等于外部±10µs验收。见[M4.3.6记录](Milestones/M4_3_6_Time_Domain_Gate.md)。

**M4.3.5标准声周期序列监控软件原型完成（2026-10-27）。** `standard_sequence`用10次/2s契约检测周期事件串，输出事件数/周期误差/质量；O0/O2主机测试通过。App已接入事件流并输出`$STANDARD_SEQUENCE`诊断；测量发布要求序列形成且quality≥50，不足则撤销结果；UDP事件仍为诊断发送；周期候选不等于标准声身份；模板MATCH0仍未解决，未烧录/未两板验证。见[M4.3.5记录](Milestones/M4_3_5_Standard_Sequence_Monitor.md)。

**标准声已正式选定（2026-10-27）。** 按老师允许自选的说明，当前`Data/Audio/standard_chirp_2-6khz_10x_20s.wav`（SHA256 `56dedc45…`，10次50ms 2–6kHz chirp，1/3/…/19s）被冻结为标准声；选择契约`Data/Audio/standard_selection.json`和只读校验`Analysis/verify_standard_selection.py`通过。其识别证据仍为MATCH0/最高raw相关0.773821，标准识别未通过；不能把能量/TOA诊断当标准识别验收。见[标准音频说明](Audio_Selection.md)。

**M4.3.4权威测量结果与状态页软件完成（2026-10-27）。** 配对结果经结果回调进入唯一`MeasurementResult`（测量ID/距离/A-B方向/质量/温度版本/2s有效期），UI/TouchGFX通过同一快照显示；无温度/声速/超龄/越界不出数值。仍为诊断时间域/CHIRP/quality=100，未烧录、未接真实两板；不能当任务书测距验收。见[M4.3.4记录](Milestones/M4_3_4_Measurement_UI.md)。

**M4.3.3诊断型两板事件UDP路径软件完成（2026-10-27）。** SoundEventTime成功TOA事件队列→App构造事件→UDP 5009发送/接收→`sound_pair`，输出`$SOUND_PAIR`/`$SOUND_EVENT_STATE`。当前时间域是诊断常量、事件类型固定CHIRP/quality=100、SYNC=0，未接真实两板/未烧录；不能当任务书同步或测距验收。见[M4.3.3记录](Milestones/M4_3_3_Diagnostic_Event_UDP.md)。

**M4.3.2两板事件配对/距离纯核心软件完成（2026-10-26）。** `sound_pair`支持同一`time_domain`下A/B事件乱序/重复/漏检/歧义拒绝、质量/类型窗口、`d=c|tB−tA|`与A/B方向；`sound_event_wire`提供36字节显式大端序编解码。O0/O2主机测试、Debug/Release构建通过；尚未接UDP、未烧录、未产生真实两板距离。见[M4.3.2记录](Milestones/M4_3_2_Pair_Distance_Core.md)。

**M4.3.1固定窗口TOA→本地PTP模型软件完成（2026-10-26）。** 检测器`anchor+window_position`拆成整数帧+Q16后接入`AudioTimeMap_EstimateQ16`，旧`SOUND_TIME`（FIRST+1）保持，新增`$SOUND_TOA_TIME`/`$SOUND_TOA_STATE`；缺失/非法TOA明确失败，不回退FIRST+1。已通过Audio/SoundEventTime/SoundDetectorService/App/ChirpService主机回归、Q16=0旧结果逐项一致、Q16 Fraction和大帧测试、Debug/Release构建及audio layout。未烧录/未实物验收；下一软件增量为两板事件时间差UDP/配对基础，正式测距仍需标准声身份/质量。见[M4.3.1记录](Milestones/M4_3_1_Local_PTP_TOA_Time.md)。

**P1受控采集的历史结果（2026-10-26）。** 原严格全10槽为音量12/15、D40 3/5、D50 5/5、D±30各4/5；随后复核发现全事件槽基址受背景捕获影响。播放窗粗覆盖及关联覆盖现在分别由工具输出，不能跨口径计算成功率。D20仍不足，D40-01缺capture而非SOUND候选；本轮工具修正已完成，无需重复旧采集或烧录。详见[P1记录](Milestones/P1_Controlled_Acoustic_Capture_2026_10_26.md)及[新工具记录](Milestones/P1_ReadOnly_Analysis_2026_10_26.md)。

**M4.2.5失败搜索诊断已硬件复验；标准音仍0/10。** A板运行`SBCG-M4.2-SEARCHDIAG`（Debug `c219af98…`）：24个完整任务全部LOW_CORR，最高raw相关0.773821<0.80，RMS2265–2302、N=257/V=257、LR/Z/I=0，MATCH0；10个高能量候选、7个高相关任务；RPT24/RLOST0，资源/日志/UI错误全0。数字WAV与模板相关1.0。诊断链已实机工作，失败在门限前相关度，不是搜索预算/范围/低RMS；独立只读复核PASS_WITH_RESERVATIONS（无阻塞，低风险流程/测试项）。下一步保存PCM/受控声学实验，不盲目降门限，识别/PTP/测距仍未验收。[M4.2.5复验](Milestones/M4_2_5_Active_Search_Diagnostics_2026_10_26.md)。

**M4.2.1扫频匹配核心的软件范围已完成。** 固定2–6kHz/50ms模板及流式正确性基线经O0/O2各3001个C断言、38组独立参考验证。后续M4.2.2已注册固定模板和有界单窗评分，原逐采样流式直接相关仍仅作基线，不走板端实时路径。[历史软件证据](Milestones/M4_2_1_Chirp_Matcher_Core.md)。

**此前A为启动诊断版`SBCG-U3.1-RESET`，历史额外启动仍未定位。** BOOT后记录原始复位标志及清除结果；原固件受控再烧录未复现，新诊断版90.77秒静默通过，不复位附加、外部复位和软件复位三组对照验证了记录能力。Debug前缀eed5faf23c7f；显示业务沿用U3.1，本轮未另做目视、联网或主动声音验收。见[诊断证据与未解边界](Milestones/U3_Reset_Diagnosis.md)。不是“异常重启已修复”的结论。

**U3.1 TouchGFX显示范围已通过单A实机验收；首窗未知重启单独待查。** 当时A为`SBCG-U3.1-TOUCHGFX`，Debug前缀7d9d1e22fe09；NoOS/软件绘图、LTDC VSYNC IRQ9/0、CRC及原RGB565双缓冲，业务文本约1Hz。两种构建/布局及主机回归通过，首帧通知竞态已修复。quiet-02明确复位后82.55秒、81条UI/80条音频、8候选精确计算通过；34秒25/30°C命令联动通过并恢复AUTO。quiet-01两次BOOT间隔6.42秒且用户无操作，本窗不通过、原因未知，未排除重启风险；原始数据保留。无网线/B/示波器、不主动发声、DHT未接。见[U3.1记录](Milestones/U3_1_TouchGFX_Status_Page.md)。不重复U2色条或SDRAM全片自检，8根颜色低位继续暂缓；没有新测距/方向/外部同步精度结论。

**U2是此前已验收的历史RGB565后端。** 当时板上`SBCG-U2-STATUS-R2`（Debug14fdabe9bec9…），BOOT后约79.34秒，UI末77帧/错误0/丢弃0，音频461有效块、丢块/错误0；手动25/30°C分别显示346.450/349.480m/s并恢复AUTO，用户确认实屏正常。初版失败及R1/R2修复保留在[U2验收](Milestones/U2_LTDC_Status_Page.md)。当前TouchGFX不运行旧八行绘制/VBR轮询栈，两后端独占显示资源。

**前一M4.1.3候选帧关联本地PTP传输模型及日志修复已通过静默验证。** 当时A板`SBCG-M4.1-TIME-R1`，Debug前缀789c0c92ef2f。BOOT后67.933秒，395有效块、66条音频有效/零丢块/零错误、心跳连续；环境自然候选10条，9模型与Fraction一致、1条NO_LEFT明确拒绝，22组原模型自检继续运行。原TIME版5次归还失败记录保留，R1日志改为有界排队每轮一行。没有主动发声，未宣称标准音频识别、物理到达或同步精度通过。[本轮结果](Milestones/M4_1_3_Sound_Event_Time.md)。

**U0/U1为已完成的历史前置。** U0纯C绘图准备见[软件报告](Milestones/U0_RGB565_Status_Page.md)；U1-SDRAM-R1已完成8MB四轮自检（50529ms）和静默共存，首版MPU失败、恢复及Region2修复见[内存报告](Milestones/U1_SDRAM_Bringup.md)。当前U2改用SDRAM显示READY，未重做破坏性测试，不将READY写成自检PASS。

## 暂缓验收：M2.3.3 PPS

**M2.3.3 PPS软件已接入；新检测固件中观察到本地排程运行，电气波形仍待验收。** 用户已通过CubeMX生成PA15/PPS_OUT、TIM2_CH1/AF1、Reset/ITR1/ETH PTP remap、PSC99/ARR最大/CCR100及Pull-down。A以本地PTP整秒目标产生参考100us正脉冲；B仅在资格及有界重新资格内输出。新增PPS不新增IRQ/DMA，不改变缓存或现有ETH/SAI内存布局。软件计数不是电气波形证据，详见[本轮交付](Milestones/M2_3_3_PTP_PPS.md)。

2026-10-23当前仅保留A板。此前“暂不烧录”已由本轮“板已连接可以测试”的明确授权更新；已烧录M4.1-CAND-R1，不是原冻结M2.3-PPS文件。PPS末计数EMITTED89/MISSED0/ERRORS0只是寄存器观测。输出为绿板Arduino **D9 / PA15 / CN7第2针**，地为GND；没有示波器，不能宣布脉宽或边沿精度通过。B恢复后仍需交换机冷启动≤30秒及收敛后连续10次±10us。

## 上一已验收基线：M2.3.2（历史证据）

- **M2.3.2已实现，并通过用户指定的1分钟运行窗口验证。** 同一固件`SBCG-M2.3-SYNC`按UID固定A参考/B跟随，319/320固定PTPv2子集、四硬件时刻及B有界fine调相/调频。用户报告已手动烧录两板；助手本次未重复烧录/复位。详见[实现与短时证据](Milestones/M2_3_2_Dual_Board_Clock_Software.md)。
- A=COM3、B=COM5，交换机仍为A2/B1/PC3口。B60秒236组原始四时刻与真实C/Fraction完全一致，协议估算时差−65～+340ns，漂移估计−61～+35ppb，8次成功调频，控制拒绝/错误0。CTRL35条内部锁、25条TRACKING，变频后重新建立锁证据；不是全程连续锁定。
- 后续60秒稳定网络A/B各60包、零缺口/重复/乱序；probe和旧time exchange各板各15次全通过，音频位置各60条连续、codec各30次成功；A温度正常，B本地温度按UID禁用。初始准备窗口B序号回退FAIL原样保留，原因未证实，不能合并成全绿实验。
- 本次用户明确将10分钟缩短为1分钟。各串口/网络窗口部分重叠，未捕获BOOT及冷启动相位步进，未读回Flash哈希；未测正向频率/正负大相位、断网恢复或外部同步精度，所有`SYNC=0`保持。内部估计不表示±10us课程独立验收或测距精度通过。
- 已有软件证据：15组原生运行器、9412断言伺服、12组协调层、Tools113/Analysis98、两种构建/布局及独立复核通过。冻结Debug前缀`8fea8c54cc27`，Release前缀`ca5b89440a5b`；用户手动烧录报告与观测行为相符，实物精确哈希未读回。
- 本次CubeMX仅用户再生成UDP PCB6，BSP fine扩展已明确批准。无引脚/RCC/DMA/NVIC/Cache改动，HAL原文件未改；旧配对/音频模型按时间代际失效，PCM DMA持续运行。
- 该版本之后实施了PPS软件及M4.1.2实验检测管线；均不表示UI或测距已完成。

## 最近一次文档与可读性维护

2026-10-23完成[系统架构](System_Architecture.md)、[答辩稿](Defense_Guide.md)及58个App C/H中文注释；156个函数定义、72个声明已覆盖，非注释token及两配置Flash镜像均与修改前一致，构建/布局通过。Debug调试信息因源码行号改变而有新ELF哈希，该注释维护当时板上仍是旧ELF、未进行新实物验收；之后用户已手动更新两板SBCG-M2.3-SYNC，并按当前工作所述完成短时运行观测。详见[维护记录](Architecture_Comment_Review.md)。

## 当前已生成并使用的资源

| 资源 | 当前配置及边界 |
| --- | --- |
| 板/MCU | STM32F746G-DISCO，STM32F746NGH6/TFBGA216；已登记A/B两块板；双板网络及短时内部闭环已通过 |
| UID | A `001A002D:30365105:36363631`；B `00380050:35385104:37313532`，A/B网络身份均已实测 |
| 工具 | CubeMX6.18.1、CubeF7 V1.17.4、ARM GCC/CMake/Ninja、VS Code |
| SYS/RCC | Trace Asynchronous Sw；HSE Bypass25MHz，M25/N400/P2，HCLK200MHz，PCLK1 50MHz/PCLK2 100MHz；PLLQ/2，不提供48MHz域 |
| USART1 | PA9 TX/PB7 RX，115200 8N1无流控；RX IRQ5/0，无UART DMA；本机ST-LINK VCP COM3 |
| 网络 | ETH/RMII、LAN8742地址0/ID0007:C131，100Mbps全双工；NO_SYS LwIP2.1.2轮询，无ETH IRQ；ETH有专用内部DMA |
| 地址/端口 | 旧实测板192.168.10.1/24；A=.1、B=.2/24身份已实测；PC物理以太网192.168.10.100/24；板5005→PC5005，PC6006↔板5006，PC6007↔板5007 |
| PTP | 原BSP本地时钟与RX/TX硬件戳、板端四时刻估算观测已实测；M2.3.2 B校时/校频短时观测通过；仍SYNC=0，PPS本地排程已观测但电气波形未验收 |
| 温度 | DHT11模块3.3V，DATA为PG7/D4、开漏释放；5秒采集、预读/超龄机制；AUTO/手动及串口命令已实测，无温度UDP共享 |
| I2C3 | PH7 SCL/PH8 SDA；WM8994地址0x1A，ID8994已实测；触控业务未实现 |
| SAI2 | A主接收提供时钟，B内部同步从接收；16位双通道，4时隙中1/3有效；标称48kHz，推导约47991.071Hz，PLLI2S344/Q7/DivQ1 |
| 音频DMA/IRQ | DMA2 Stream7 Channel0，P→M halfword circular，32768元素/64KiB；DMA2_Stream7和SAI2 IRQ均4/0；半满/满每8192帧 |
| 内存/Cache | 音频SRAM1 0x20010000..0x20020000、32字节对齐；主栈0x20047000..0x20048000，LwIP堆窗0x20048000..0x2004C000；ETH描述符/RX池在SRAM2；I/D Cache关闭 |
| FMC/SDRAM | 用户生成38脚/PC3 SDCKE0、CAS3/HCLK3，Region1=C0000000/8MB普通非缓存XN，Region2=A0000000/4KB共享Device；U1历史8MB四轮实测通过；当前U2为显示READY，不重做破坏性自检 |
| LTDC显示 | TouchGFX4.26.1/NoOS、软件绘图，RGB565/480×272/9.6MHz及C0000000/C0040000双缓冲；CRC、LTDC IRQ9/0；前台更新，用户确认实屏正常 |
| 当前未完成 | 首窗额外启动原因、PPS波形/双板同步外部精度、DMA2D/触控、温度共享、标准声音识别/到达校准与测距；TouchGFX状态页已接入 |

生成与实测的完整资源边界见[Hardware_Resources](Hardware_Resources.md)。任何引脚、时钟、DMA、IRQ变更仍由CubeMX负责；此前M2.3.2仅调整UDP PCB池。U1用户生成FMC/MPU Region1/2并通过自检；U2用户生成LTDC并已点亮，助手未手改生成初始化。

## 前一M3.2.4实物证据与已知限制

[M3.2.4完整记录](Milestones/M3_2_4_Audio_Time_Map_Board.md)：单次授权烧录后120.190秒，120个有效锚点、拒绝0、WIDTH0/1；40组板端估算与真实C/Fraction逐值相同，残差范围−14.884～+13.200us。120对原音频位置连续；110包UDP零缺口、两组30请求及原板端估算对照通过；23个自动温度版本、60次codec ID读取无错。

codec最大轮询间隔2068ms、心跳最大1015ms保留，不宣称精密前台周期。复位前14行及新BOOT前63字节残片仅作边界排除，残片含0xff；raw二进制与JSON投影逐行一致，新BOOT后严格ASCII且无重启。原始证据未改写。

NDTR进度不是SRAM读取授权；WIDTH=0仍有一帧量化；codec/SAI/FIFO/DMA到物理声音到达的延迟未校准。留出残差及模型区间不能证明声学精度、绝对频率或双板同步。

此前温度[连续拔插恢复](Milestones/T1_1_Temperature_Replug.md)、[手动设温](Milestones/T1_2_Manual_Temperature.md)、音频[受控发声](Milestones/M3_1_Audio_DMA.md)、[5分钟稳定性](Milestones/M3_1_Quiet_Stability.md)和[温度联动](Milestones/M3_1_Temperature_Coexistence.md)各有分版证据；不升级成当前版本已重复执行。

## 后续与验收分层

M3.2.4当时通过10组原生模块（Audio10程序）、Tools83/Analysis87项及两配置构建/布局，并完成上述Debug实物验收；这些数量是历史基线。本轮LCD状态页及检测软件已有后续实现，不重新列为待配置；匹配时间接口、声学延迟校准、双板测距和外部同步/PPS精度仍有缺口。完整课程指标见[路线图](Project_Roadmap.md)，最终整机要求保留。

最新进度及下一目标见[本轮复核](Progress_Review_2026_10_26.md)；M4.1.3/U0均为历史前置，不作为当前版本或下一配置步骤。M2.3.2仅为此前双板短时观测，不代表外部精度。
