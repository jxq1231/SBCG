# 文档状态与阅读索引

<!-- doc-status: current-document-index; reviewed: 2026-10-27 -->

审计日期：2026-10-23。已检查原有 **57份仓库自有Markdown**：顶层历史方案20份、历史验收23份，其余入口/资源/规则/模板14份。第三方Drivers/Middlewares许可与说明、构建输出、Logs内实验快照不作为项目进度文档，不改写。新文档随本轮登记。

## 防止重复实施

**当前入口（2026-10-27）：[M4.3.25双板PPS共同1800秒长稳](Milestones/M4_3_25_PPS_30min_Stability.md)已通过串口状态/计数观察。** 两板故障/漏计/错误/恢复均0，建域后无VALID=0记录。已有示波器10次max235ns按原范围保留，不复测；本轮后续只做标准声音频闭合，STANDARD_SEQUENCE/配对/两屏距离尚未通过。下面P1、单A及短时记录均按各自版本保留，不作为当前重做指令；不改测量时间契约，CLAP不纳入正式验收。

最新目标复核入口：[两板距离/方向与提高项路线](Goal_Aligned_Roadmap_2026_10_26.md)。M4.2.6已有实物PCM、M4.2.7有单板TOA；不能继续按旧诊断计划重复烧录。整体精度未过，部分稳定槽不是验收；B2估算器修正已由后续提交`db441e0`/`5471919`入库，按软件/实际窗口分层解释。

**最新工具增量已完成：[P1播放阶段分类与固定槽](Milestones/P1_ReadOnly_Analysis_2026_10_26.md)。** 旧全量诊断保持兼容，新增捕获粗覆盖、唯一关联、各版本数值TOA三层播放口径；53项相关主机测试及原数据复算通过，prelude格式错误不豁免。工具验证不等于标准声身份或物理精度通过，下一步依据捕获/关联缺口规划定位，不重复实施本统计修复。

最新增量：[单A标准音真实播放](Milestones/M4_2_Active_Chirp_Trial_2026_10_26.md)已执行，用户确认听清10次；板端10次周期能量响应、10任务完整搜索，CHIRP/MATCH0，资源错误0。此前“不能发声、主动未测试”均指旧窗口；现在是识别未通过，先做失败相关度/峰位置诊断，M4.3.1暂缓。未更换固件。

1. 每次先读[现行入口](README.md)和[当前状态](Project_State.md)，再读当前增量的计划。旧`Plans/`不构成新授权；历史烧录许可和CubeMX修正不可复用为当前操作。
2. `archived-plan`表示原批准范围已交付；不表示所有未来设想都实现。`completed-evidence`表示实验记录已归档；不把其未测/失败变成成功。
3. 历史M2.3.2时两板由用户手动更新到M2.3.2/SBCG-M2.3-SYNC，并通过1分钟运行内部闭环观测；此前M2.3.1的A123秒联合验收及双板网络证据按历史范围保留。此前M3.2.4的120秒/40组估算证据按原范围保留。M3.2.3纯软件记录保留其当时未接入/未烧录边界。
4. R1温度正常采样、T1.2后续连续拔插、sound-01延后和sound-02通过分别保留。旧状态已标注，禁止跨版本合并成一次全绿实验。
5. U3.1 TouchGFX显示范围已通过单A实屏及82.55秒静默，首窗重启原因未知；U2 RGB565为历史已验收后端；FMC/Region1/2已生成且U1-SDRAM-R1四轮8MB自检及静默共存通过；PPS已由用户生成硬件配置并接入软件，实物未验收；双板闭环源码按M2.3.2记录，短时运行通过，外部精度待验收；现已生成的SAI2/DMA2、USART1 RX、温度和网络不再作为待配置步骤。

**最新进度见[2026-10-26复核](Progress_Review_2026_10_26.md)：检测优化已至A4，整机并非只差标准音验证。** M4.2.2初版/R1/R2和DIAG的失败记录按历史保存，扫描瓶颈随后由M4.2.3-B改善；不重复实施旧修复计划。M4.2.3-B当时SCAN≤3ms、DONE_MAX831ms；≤494ms属于后续M4.2.4-B，不跨版本混用。主机合成压力已完成；主动标准音、外部同步精度、双板事件/测距/结果显示仍待实现或验收。M4.2.4-A/B/A2/A3/A4静默范围完成，C2评估完成但未实施，C1 UI暂缓。M4.2.5搜索诊断已软件验证并实机复验：24个完整任务全部LOW_CORR、MATCH0，最高raw相关0.773821<0.80、RMS≈2300、N=257/V=257、RPT24/RLOST0、资源错误0；独立只读复核PASS_WITH_RESERVATIONS（无阻塞，2 low流程/测试缺口）；诊断链工作但识别仍未通过，下一步保存PCM/受控声学实验，不直接降低门限。旧方案和历史烧录许可不能作为新操作指令。

## 逐文件状态

此前登记91份自有Markdown，本轮补登记U2验收报告、M4.2.3-A批量门控报告、M4.2.3-B静默实测报告、M4.2.4项目书、M4.2.4-A测量记录、M4.2.4-B批量Stage报告、M4.2.4-A2同循环归因报告、M4.2.4-A3 AUDIO/OTHER分解报告、M4.2.4-A4对照与统计优化报告、M4.2.4-C2日志策略评估报告、M4.2.5搜索诊断报告、M4.2.5真实播放复验报告、P1受控声学采集报告及P1只读分析报告。最新M4.1.3本地模型与R1日志修复静默范围通过；U0 RGB565页面完成软件验证，U1首版失败保留，Region2修复后R1已通过全8MB四轮及约79秒静默共存；U2 RGB565真实状态页已通过单A实物和用户目视，跳过独立色条；8根低位暂缓。旧“未接入/未烧录”和主动声音失败仅指各自日期/版本，不重复执行。

| 文件 | 状态 | 何时阅读 |
| --- | --- | --- |
| [M4.3.25 PPS共同30分钟长稳](Milestones/M4_3_25_PPS_30min_Stability.md) | 串口观察PASS·1800秒共同窗口 | 严格BOOT边界、A/B状态计数、DOMAIN、证据哈希与外部精度边界；现行同步状态入口 |
| [面向提高项的目标路线](Goal_Aligned_Roadmap_2026_10_26.md) | 当前任务书复核与优先级 | 最终几何、已完成/未完成、单A能做什么、每步帮助哪个指标 |
| [全事件对账与TOA离线回放计划](Plans/2026-10-26_PCM_Event_Replay_Plan.md) | 已完成+审查修复+审计 | 九轮输入、版本未知、关联歧义、完整窗、真实C回放及分层通过条件；审计与后续规划见[链接](Plans/2026-10-26_PCM_Event_Replay_Audit.md)；P0独立复核`Logs/Detector/TOA_Replay/P0_review/review.json`（PASS_WITH_RESERVATIONS，F1/F2/F3/F5已修，`c27e5e7`；修复后run `20261026T110943/111047`）；[里程碑](Milestones/PCM_Event_Replay_2026_10_26.md) |
| [P1受控声学采集](Milestones/P1_Controlled_Acoustic_Capture_2026_10_26.md) | 原始采集与旧口径归档·物理精度未验 | 旧严格音量12/15、D40 3/5、D50 5/5、D±30各4/5按原run保留；新分层口径见工具记录，不混算 |
| [P1分析与工具实施](Milestones/P1_ReadOnly_Analysis_2026_10_26.md) | 固定参考分槽已软件验证·无固件改动 | 捕获粗覆盖15/15、4/5、6/6、6/6、5/5与唯一关联14/15、4/5、6/6、4/6、4/5分列；严格错误、D20不足及D40捕获缺5/6均保留 |
| [TOA可信边界修复](Milestones/TOA_Trusted_Boundaries_2026_10_26.md) | 软件验证通过·未烧录 | 完整窗/NA、21字节输出与数值转换安全，不代表前沿精度改善 |
| [单板可信验证计划](Plans/2026-10-26_Goal_Aligned_Review/task_plan.md) | 已完成·复核与软件修复记录 | TOA窗口护栏、已有实验纠偏及验证范围，不重复实施 |
| [M4.2.7单板TOA](Milestones/M4_2_7_A_Single_Board_TOA.md) | 实物链路成立·整体精度未过 | 实验版本、全事件/背景候选及名义采样轴限制 |
| [标准声选择契约](../Data/Audio/standard_selection.json) | 已选定·识别未通过 | 当前chirp WAV/模板哈希、10次时刻冻结；MATCH0仍未解决；asset/selection only |
| [M4.3.9结果周期刷新/计数](Milestones/M4_3_9_Result_Refresh.md) | 软件完成·无ACK/未烧录 | A每500ms重发同ID；B刷新2s；`$MEASURE_RESULT_STATE`计数 |
| [M4.3.25 A/B温度共享](Milestones/M4_3_25_Temperature_Share.md) | 实物验证PASS | A一只DHT11经TEMP UDP共享；B UI显示同温度/声速，无需第二传感器 |
| [M4.3.24 PPS 10分钟长稳](Milestones/M4_3_24_PPS_Long_Stability.md) | 长稳PASS(600s) | A 599/599 QUALIFIED；B无FAULT/MISSED；DOMAIN无掉域 |
| [M4.3.23 PPS FAULT恢复](Milestones/M4_3_23_PPS_Fault_Recovery.md) | 软件完成·短时运行通过 | FAULT后1s重试；A恢复QUALIFIED/B需重新锁定；FAULTS/LAST_FAULT日志 |
| [M4.3.22 PPS示波器相位](Milestones/M4_3_22_PPS_Scope.md) | 操作者实测·满足10µs | 10次max235ns/mean93.9ns；鳄鱼夹未校偏斜，不能证明10ns |
| [M4.3.21 A事件UDP PCB耗尽](Milestones/M4_3_21_A_Event_UDP_PCB.md) | 已修复·硬件验证 | CubeMX池6→10；run05 A INIT_ERR=0，A/B DOMAIN 60s稳定 |
| [M4.3.20 B域连续性](Milestones/M4_3_20_B_Domain_Continuity.md) | 软件完成·A绑定失败/待复验 | B TRACKING+有界RATE不再掉域；run04 A事件UDP bind失败未验证 |
| [事件阻断修复计划](Plans/2026-10-27_Event_Blockers_Fix.md) | 软件完成·未烧录 | 配对3000ms、跨块2400帧分类、App回归；首次2秒/标定/CLAP另列 |
| [M4.3.19 app_main结果接线测试](Milestones/M4_3_19_App_UDP_Integration.md) | 主机完成·UDP边界桩 | App发送/ACK/序号回退/退避/域撤销；非真实UDP/未烧录 |
| [M4.3.18结果P2修复](Milestones/M4_3_18_Result_P2_Fixes.md) | 软件完成·集成测试仍缺 | 失败500ms退避；重复ACK不误判；MRES标定一致性 |
| [M4.3.17结果重发状态模块](Milestones/M4_3_17_Result_Exchange_State.md) | 软件完成·硬件待验 | ACK序号回退/域恢复恢复重发；O0/O2测试 |
| [M4.3.16 c24b5ba review修复](Milestones/M4_3_16_Review_Fixes.md) | 软件完成·未烧录 | B重启恢复重发；域超时3s；标定偏移/配对窗口校验 |
| [M4.3.15 0.5m标定/温补](Milestones/M4_3_15_Calibration_Temperature.md) | 软件完成·触发/实测待验 | 500mm偏移/版本、MRES32携标定；A应用/B显示；未接按键/未烧录 |
| [M4.3.14静默/瞬态门控](Milestones/M4_3_14_Quiet_Transient_Gate.md) | 软件完成·60s实测待验 | 预热/序列/静默3s/失域FAULT；单CHIRP不开放；未烧录 |
| [M4.3.13权威结果MACK](Milestones/M4_3_13_Result_Ack.md) | 软件完成·硬件待验 | B回16B MACK；A同令牌ACK后停发；ACK计数；未烧录 |
| [M4.3.12共同域双向握手](Milestones/M4_3_12_Domain_Handshake.md) | 软件完成·硬件待验 | DOMN REQ/ACK；A确认B严格合格后才放行；B未收到REQ不发事件；超时撤域 |
| [M4.3.11配对P0/结果边界修复](Milestones/M4_3_11_Pairing_Review_Fixes.md) | P0已修·P1域证据未解 | 本端事件提交配对器；域失效清结果；旧ID/UNKNOWN方向拒绝；未烧录 |
| [M4.3.10 PPS共同域门控](Milestones/M4_3_10_PPS_Domain_Unverified.md) | 未验证·PPS/两板待验 | PPS只读getter+QUALIFIED/RATE_REQUALIFY门控；不得当同步验收 |
| [M4.3.8两屏权威结果交换](Milestones/M4_3_8_Authoritative_Result_Exchange.md) | 软件完成·无重传/未烧录 | A→B 28字节MRES，同一ID/距离；B不重算；单包丢失旧值至2s |
| [M4.3.7标准chirp/击掌分类](Milestones/M4_3_7_Chirp_Clap_Classifier.md) | 软件接入·真实声样本待验 | 包络/ZCR趋势/差分能量，CHIRP才进序列/UDP；合成测试，未真实标准声/未烧录 |
| [M4.3.6共同时间域门控](Milestones/M4_3_6_Time_Domain_Gate.md) | 软件门控完成·PPS/两板待验 | A参考/B内部锁定+offset；域无效撤UDP/测量；未直接读PPS/未烧录 |
| [M4.3.5标准声周期序列监控](Milestones/M4_3_5_Standard_Sequence_Monitor.md) | 纯软件原型·未接线/未烧录 | 10次/2s周期、抖动/漏步/低质量/长静默测试；周期≠识别，模板MATCH0未解 |
| [M4.3.4权威测量结果/状态页](Milestones/M4_3_4_Measurement_UI.md) | 软件路径完成·硬件/同步待验 | 配对→单一MeasurementResult→UI快照；距离/方向/质量/温度版本/2s有效期；诊断域/SYNC=0 |
| [M4.3.3诊断型事件UDP](Milestones/M4_3_3_Diagnostic_Event_UDP.md) | 软件路径完成·两板/同步/烧录待验 | 5009事件线协议、成功TOA队列、A/B配对与$SOUND_PAIR；time_domain为诊断常量/SYNC=0 |
| [M4.3.2事件配对/距离纯核心](Milestones/M4_3_2_Pair_Distance_Core.md) | 纯软件核心完成·网络/硬件待接 | 乱序/重复/歧义/质量/时间域门控，d=c|Δt|与A/B方向，36字节线协议；未接UDP/未烧录 |
| [M4.3.1固定窗口TOA本地PTP模型](Milestones/M4_3_1_Local_PTP_TOA_Time.md) | 软件完成·硬件待验 | Q16=0旧兼容、TOA缺失/非法不冒充成功、旧SOUND_TIME保留；新增SOUND_TOA_TIME；未烧录 |
| [M4.2.6 PCM捕获](Milestones/M4_2_6_Candidate_PCM_Capture.md) | 已有实物数据·识别待改进 | 保留在线/离线一致性与旧同步采集缺口，勿按捕获文件数算识别率 |
| [标准音真实播放](Milestones/M4_2_Active_Chirp_Trial_2026_10_26.md) | 识别未通过·资源范围通过 | 10次实际刺激、0匹配、完整性及下一诊断目标 |
| [本次声学实测计划](Plans/2026-10-26_Active_Chirp_Trial/task_plan.md) | 执行完成·未修复识别 | 01主机播放脚本失败保留，02有效播放及听感确认 |
| [进度复核与下一步](Progress_Review_2026_10_26.md) | 此前核对结论·软件复验通过 | 整机目标分层、M4.3.1建议、实物补验和精度边界 |
| [本轮审计计划](Plans/2026-10-26_Progress_Audit/task_plan.md) | 审计已完成·未实施下一功能 | task_plan/findings/progress仅追溯本轮检查 |
| [M4.2.3性能方案](Plans/2026-10-25_Audio_Scan_Optimization/task_plan.md) | A/B软件完成·B静默通过·主动声学待验 | 批量门控、语义对照、资源预算及分层验收；依据/规划进度在同目录按需读取 |
| [M4.2.3-A批量门控核心](Milestones/M4_2_3_A_Batch_Detector_Core.md) | 软件等价验证通过·已接入 | 64点批量入口、逐字段对照与复跑证据；B已接入并有静默实测 |
| [M4.2.3-B静默实测](Milestones/M4_2_3_B_Batch_Scan.md) | 静默范围通过·主动声学待验 | 批量扫描、时延诊断、A板烧录与零错误证据；不表示MATCH/声学精度 |
| [M4.2.4项目书](Plans/2026-10-26_M4_2_4_Foreground_Optimization.md) | A/B/A2/A3/A4完成·C2评估完成 | 前台分解、批量Stage、归因、AUDIO/OTHER拆分、统计优化、C2日志量化均完成；C2实施待选，C1 UI暂缓 |
| [M4.2.4-A测量记录](Milestones/M4_2_4_A_Foreground_Profile.md) | 分模块耗时分解通过·未做性能修复 | 静默STAGE21/UI19/GAP60ms及零错误证据；不表示声学/实时最坏时间 |
| [M4.2.4-B批量Stage](Milestones/M4_2_4_B_Batch_Stage.md) | 批量迁移静默对比通过·C未开始 | STAGE21→7ms、GAP60→49ms、业务零错误；不表示声学精度 |
| [M4.2.4-A2同循环归因](Milestones/M4_2_4_A2_Loop_Attribution.md) | 同循环归因通过·C1未触发 | 最大循环AUDIO21–25/OTHER16/UI1ms；口径限制见报告 |
| [M4.2.4-A3 AUDIO/OTHER分解](Milestones/M4_2_4_A3_Audio_Other_Profile.md) | 内部分解通过·A4已完成 | 最大轮STAT15/SD_STAGE6/CH_PROC6，同轮FLUSH12；不表示纯CPU/最坏时间 |
| [M4.2.4-A4对照与优化](Milestones/M4_2_4_A4_Control_Statistics.md) | 对照+统计优化通过·C2评估完成 | STAT15→8–9ms、AUDIO27→18ms、LOOP40→34ms；FLUSH≈15ms保留为条件优化 |
| [M4.2.4-C2日志评估](Milestones/M4_2_4_C2_Log_Evaluation.md) | 评估完成·实施未开始 | UART占用21.5%、23.7行/秒；quiet/full方案与风险边界 |
| [M4.2.5搜索诊断](Milestones/M4_2_5_Search_Diagnostics.md) | 软件+硬件诊断通过·独立复核PASS_WITH_RESERVATIONS·识别未通过 | 失败搜索raw峰/分类；不改门限/预算；实机24任务LOW_CORR |
| [M4.2.5真实播放复验](Milestones/M4_2_5_Active_Search_Diagnostics_2026_10_26.md) | 0/10失败·LOW_CORR已定位 | 最高raw相关0.773821、RMS≈2300、N=257/V=257；下一步PCM/受控声学 |
| [M4.2.2有界接入](Milestones/M4_2_2_Bounded_Chirp_Integration.md) | 初版/R1/R2静默均失败 | 失败证据、缓存/调度修复、冻结镜像及验收边界 |
| [M4.2.2实施计划](Plans/2026-10-25_Bounded_Chirp_Integration.md) | 历史实施/定位计划·后续批量化已完成 | 按需追溯三次失败和DIAG，不重复旧下一步 |
| [M4.2.1匹配核心](Milestones/M4_2_1_Chirp_Matcher_Core.md) | 软件范围通过·基线归档 | 模板及配置已用于M4.2.2，逐采样流式算法仍只作基线，不当作实物识别率 |
| [M4.2.1实施计划](Plans/2026-10-25_Chirp_Matcher_Core.md) | 已完成·历史计划 | 不重复核心实施；下一增量另做实时预算与板端接入 |
| [标准音频选型](Audio_Selection.md) | 候选素材·待声学实测 | 原始下载/合成文件、参数和许可；纯C核心已验证，板端识别尚未完成 |
| [启动原因诊断](Milestones/U3_Reset_Diagnosis.md) | 记录能力已实测·历史根因未明 | 当前诊断固件、三组对照及尚未关闭的问题 |
| [诊断计划](Plans/U3_Reset_Diagnosis/task_plan.md) | 本轮实施记录·根因仍开放 | 不重复已完成对照，不将记录能力当根因修复 |
| [U3.1状态页记录](Milestones/U3_1_TouchGFX_Status_Page.md) | 显示范围通过·首窗重启未解 | 当前固件、首帧竞态修复、SDK复现及待补范围 |
| [U3.1状态页计划](Plans/2026-10-25_TouchGFX_Status_Page.md) | 已完成·历史计划 | 已生成NoOS/CRC/LTDC IRQ，不重复硬件设置 |
| [U2状态页验收](Milestones/U2_LTDC_Status_Page.md) | 历史后端·单A实屏通过 | 初版失败、R1/R2修复、温度页面联动及证据边界 |
| [U2状态页计划](Plans/2026-10-24_LTDC_Status_Page.md) | 已完成·历史计划 | 不重复配置/色条；8根低位按用户决定暂缓 |
| [U1 SDRAM启动记录](Milestones/U1_SDRAM_Bringup.md) | 当前实物交付·内存与静默共存通过 | R1校验/音频证据，首版失败和恢复保留 |
| [U0 RGB565状态页](Milestones/U0_RGB565_Status_Page.md) | 当前软件交付·未点屏 | 软件预览/测试，不能当实板照片 |
| [U0/U1配置参考](Plans/2026-10-24_LTDC_RGB565_Bringup.md) | U1已完成·仅追溯 | 不重复FMC/MPU；显示操作转到U2现行计划 |
| [M4.1.3模型与日志修复](Milestones/M4_1_3_Sound_Event_Time.md) | 当前交付·静默范围通过 | 原失败、R1条件及后续显示 |
| [M4.1.3实施方案](Plans/2026-10-23_Sound_Event_Time.md) | 已实施·受限实测 | 不重复烧录，主动声音/精度仍待验 |
| [M4.1.2接入与实测](Milestones/M4_1_2_Sound_Candidate_Integration.md) | 当前管线交付·严格声音实验未通过 | 本轮先读；失败、补验和已测边界 |
| [M4.1.2实施方案](Plans/2026-10-23_Sound_Candidate_Integration.md) | 已实施·受限证据 | 不重复烧录；以报告为准 |
| [M4.1.1纯C候选内核](Milestones/M4_1_1_Sound_Candidate_Core.md) | 历史纯算法交付 | 后续已由M4.1.2接入，不重复实现 |
| [M4.1.1设计与执行](Plans/2026-10-23_Sound_Candidate_Core.md) | 已完成·纯算法计划 | 按需追溯，不重复实现 |
| [M2.3.3 PPS交付与单机后续](Milestones/M2_3_3_PTP_PPS.md) | 波形待验·软件通过 | 原冻结文件未单独烧录；新检测固件已观察本地排程，仍无波形证据 |
| [M2.3.3 PA15 PPS方案](Plans/2026-10-23_PTP_PPS_PA15.md) | 已实施软件·待实物 | Pull-down已补齐，不重复配置；只在追溯机制时阅读 |
| [M2.3.2软件交付](Milestones/M2_3_2_Dual_Board_Clock_Software.md) | 软件与短时运行通过 | 冻结固件及60秒内部观测，初始异常/未测边界保留 |
| [M2.3.2批准方案](Plans/2026-10-23_Dual_Board_Clock_Closed_Loop.md) | 已实施·历史闭环方案 | 见M2.3.2短时运行证据，不重复实现 |
| [纯C伺服测试](../Tests/ClockServo/README.md) | 当前验证参考 | 控制增益、符号、边界与仿真限制 |
| [M2.3.1软件交付](Milestones/M2_3_1_Dual_Board_Identity.md) | A联合通过·双板网络通过 | 网络共存不表示同步精度 |
| [双板同步当前设计](Plans/2026-10-23_Dual_Board_Time_Sync_Design.md) | 历史起点·身份已完成 | 闭环实现请读M2.3.2批准方案 |
| [M2.3.1工作计划](Plans/Dual_Board_Sync_2026_10_23/task_plan.md) | 当前计划 | M2.3.2短时运行通过，后续目标另选 |
| [M2.3.1证据笔记](Plans/Dual_Board_Sync_2026_10_23/findings.md) | 当前工作记录 | UID、生成钩子与PTP后续限制 |
| [M2.3.1执行记录](Plans/Dual_Board_Sync_2026_10_23/progress.md) | 当前工作记录 | 红绿灯测试、复核、用户范围调整 |
| [系统架构](System_Architecture.md) | 现行架构参考 | 当前真实启动/调度/数据流，排除未启用GUI路径 |
| [答辩指南](Defense_Guide.md) | 现行讲解材料 | 按实际实现组织口头回答，不将设计目标当已实现 |
| [注释与验证](Architecture_Comment_Review.md) | 已完成·维护证据 | 58文件清单、覆盖与代码/镜像等价，非新硬件验收 |
| [架构整理计划](Plans/Architecture_2026_10_23/task_plan.md) | 已完成·维护计划 | 按需追溯，不重复批量修改 |
| [架构核对笔记](Plans/Architecture_2026_10_23/findings.md) | 已完成·工作记录 | 本次代码证据和判断边界 |
| [架构执行记录](Plans/Architecture_2026_10_23/progress.md) | 已完成·工作记录 | 本次验证和工具问题处理 |
| [M3.2.4实施计划](Plans/2026-10-23_Audio_Time_Map_Board_Implementation.md) | 已完成·历史方案 | 按需追溯，不重复实施 |
| [M3.2.4验收记录](Milestones/M3_2_4_Audio_Time_Map_Board.md) | 已完成·归档实物证据 | 只证明该版本120秒正常窗口，不代表声学校准 |
| [.github/ISSUE_TEMPLATE/experiment.md](../.github/ISSUE_TEMPLATE/experiment.md) | 持续有效·协作模板 | 新建Issue/PR时使用，非实验结论 |
| [.github/ISSUE_TEMPLATE/task.md](../.github/ISSUE_TEMPLATE/task.md) | 持续有效·协作模板 | 新建Issue/PR时使用，非实验结论 |
| [.github/PULL_REQUEST_TEMPLATE.md](../.github/PULL_REQUEST_TEMPLATE.md) | 持续有效·协作模板 | 新建Issue/PR时使用，非实验结论 |
| [AGENTS.md](../AGENTS.md) | 持续有效·工程规则 | 修改前必读，不归档 |
| [CONTRIBUTING.md](../CONTRIBUTING.md) | 持续有效·协作指南 | 协作时按需查阅 |
| [Docs/CubeMX_Step1.md](CubeMX_Step1.md) | 已完成·基线参考 | 仅复核SYS/RCC时读，不重复配置 |
| [Docs/Development_Workflow.md](Development_Workflow.md) | 现行入口/状态/接口 | 按入口选择，不按旧阶段顺序重读 |
| [Docs/Document_Status.md](Document_Status.md) | 现行入口/状态/接口 | 按入口选择，不按旧阶段顺序重读 |
| [Docs/Hardware_Resources.md](Hardware_Resources.md) | 现行参考·含未来设计 | 先看当前摘要，未来外设尚未自动启用 |
| [Docs/Milestones/M0_1_Startup.md](Milestones/M0_1_Startup.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M0_2_Serial.md](Milestones/M0_2_Serial.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M1_1_Ethernet_Setup.md](Milestones/M1_1_Ethernet_Setup.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M1_2_UDP_Send.md](Milestones/M1_2_UDP_Send.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M1_2_UDP_Setup.md](Milestones/M1_2_UDP_Setup.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M1_3_UDP_Request_Reply.md](Milestones/M1_3_UDP_Request_Reply.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M1_4_UDP_Reconnect.md](Milestones/M1_4_UDP_Reconnect.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M2_1_PTP_Clock.md](Milestones/M2_1_PTP_Clock.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M2_1_PTP_RX.md](Milestones/M2_1_PTP_RX.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M2_1_PTP_TX.md](Milestones/M2_1_PTP_TX.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M2_2_Board_Observation.md](Milestones/M2_2_Board_Observation.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M2_2_Sync_Estimator.md](Milestones/M2_2_Sync_Estimator.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M2_2_Sync_Observation.md](Milestones/M2_2_Sync_Observation.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M2_2_Time_Exchange.md](Milestones/M2_2_Time_Exchange.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M3_0_Codec_ID.md](Milestones/M3_0_Codec_ID.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M3_1_Audio_DMA.md](Milestones/M3_1_Audio_DMA.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M3_1_Quiet_Stability.md](Milestones/M3_1_Quiet_Stability.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M3_1_Temperature_Coexistence.md](Milestones/M3_1_Temperature_Coexistence.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M3_2_1_Audio_Frame_Index.md](Milestones/M3_2_1_Audio_Frame_Index.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M3_2_2_Audio_PTP_Anchor.md](Milestones/M3_2_2_Audio_PTP_Anchor.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/M3_2_3_Audio_Time_Map.md](Milestones/M3_2_3_Audio_Time_Map.md) | 已完成·纯软件证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/T1_1_Temperature.md](Milestones/T1_1_Temperature.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/T1_1_Temperature_Replug.md](Milestones/T1_1_Temperature_Replug.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Milestones/T1_2_Manual_Temperature.md](Milestones/T1_2_Manual_Temperature.md) | 已完成·归档实物/分层证据 | 只证明对应版本/范围；软件回放不等于新实物验收 |
| [Docs/Plans/2026-10-20_PTP_Clock.md](Plans/2026-10-20_PTP_Clock.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-20_PTP_RX_Design.md](Plans/2026-10-20_PTP_RX_Design.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-20_PTP_RX_Implementation.md](Plans/2026-10-20_PTP_RX_Implementation.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-20_PTP_TX_Design.md](Plans/2026-10-20_PTP_TX_Design.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-20_PTP_TX_Implementation.md](Plans/2026-10-20_PTP_TX_Implementation.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-20_Sync_Estimator_Design.md](Plans/2026-10-20_Sync_Estimator_Design.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-20_Sync_Estimator_Implementation.md](Plans/2026-10-20_Sync_Estimator_Implementation.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-20_Time_Exchange_IO_Design.md](Plans/2026-10-20_Time_Exchange_IO_Design.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-20_Time_Exchange_IO_Implementation.md](Plans/2026-10-20_Time_Exchange_IO_Implementation.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-21_Board_Sync_Observation_Design.md](Plans/2026-10-21_Board_Sync_Observation_Design.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-21_Board_Sync_Observation_Implementation.md](Plans/2026-10-21_Board_Sync_Observation_Implementation.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-21_Manual_Temperature.md](Plans/2026-10-21_Manual_Temperature.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-21_Sync_Observation_Design.md](Plans/2026-10-21_Sync_Observation_Design.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-21_Sync_Observation_Implementation.md](Plans/2026-10-21_Sync_Observation_Implementation.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-21_Temperature_Integration_Design.md](Plans/2026-10-21_Temperature_Integration_Design.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-21_Temperature_Integration_Implementation.md](Plans/2026-10-21_Temperature_Integration_Implementation.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-22_Audio_DMA_Design.md](Plans/2026-10-22_Audio_DMA_Design.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-22_Audio_Frame_Index_Design.md](Plans/2026-10-22_Audio_Frame_Index_Design.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-22_Audio_PTP_Anchor_Design.md](Plans/2026-10-22_Audio_PTP_Anchor_Design.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-22_Codec_ID_Design.md](Plans/2026-10-22_Codec_ID_Design.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/2026-10-23_Audio_Time_Map_Implementation.md](Plans/2026-10-23_Audio_Time_Map_Implementation.md) | 已完成·历史方案 | 跳过常规读取；按需看对应验收，勿重复配置/烧录 |
| [Docs/Plans/M3_2_3/findings.md](Plans/M3_2_3/findings.md) | 已完成·M3.2.3工作记录 | 追溯本轮决策/进度时读，勿重复执行 |
| [Docs/Plans/M3_2_3/progress.md](Plans/M3_2_3/progress.md) | 已完成·M3.2.3工作记录 | 追溯本轮决策/进度时读，勿重复执行 |
| [Docs/Plans/M3_2_3/task_plan.md](Plans/M3_2_3/task_plan.md) | 已完成·M3.2.3工作记录 | 追溯本轮决策/进度时读，勿重复执行 |
| [Docs/Project_Roadmap.md](Project_Roadmap.md) | 现行入口/状态/接口 | 按入口选择，不按旧阶段顺序重读 |
| [Docs/Project_State.md](Project_State.md) | 现行入口/状态/接口 | 按入口选择，不按旧阶段顺序重读 |
| [Docs/Protocol.md](Protocol.md) | 现行入口/状态/接口 | 按入口选择，不按旧阶段顺序重读 |
| [Docs/README.md](README.md) | 现行入口/状态/接口 | 按入口选择，不按旧阶段顺序重读 |
| [Docs/SerialWeb.md](SerialWeb.md) | 现行指南·含历史 | 读页首命令和连接，旧M0.2记录按需追溯 |
| [README.md](../README.md) | 现行入口/状态/接口 | 按入口选择，不按旧阶段顺序重读 |

## 本次修正

- 重写Docs入口和当前状态，移除互相矛盾的多份“当前”摘要；历史证据仍完整留在Milestones。
- 路线图第9节从过时的M2.2.4待实施改为M3.2.3；保留任务书指标和历史日期段落。
- 补齐旧方案/实验的页首状态，标记早期CubeMX时隙问题已解决；不改动真实失败、延期、哈希和各版未测项。
- 修正系统基线、硬件DMA/SAI、串口RX及协议页的过期状态；DHT11当前接线仍为3.3V、PG7/D4。
- 首页只保留最新摘要和分类进度，链接到单一现行状态；不再要求从旧启动阶段文档逐个重读。

本次审计核对本地代码、历史报告和文档内链接；没有重新核验外部网站、PCB修订、第三方手册全部引脚或实物波形，未新增硬件通过结论。

前置M3.2.3当时完成纯C/Fraction核对和30点留出回放，最大绝对残差13.004us；现行入口已更新，无新固件或硬件验证。板端后续已由M3.2.4独立验收。

前一M3.2.4/SBCG-M3.2-MAP已链接真实估算内核并完成授权烧录/实物验收。历史M3.2.3“未接入”仅是该纯软件增量当时边界。
