# 项目文档入口

<!-- doc-status: current-entry; reviewed: 2026-10-26 -->

更新：2026-10-26。**先读本页，再读当前状态和本次计划；无需按时间顺序重读所有历史文档。**

**当前先读[面向两板距离/方向与提高项的路线](Goal_Aligned_Roadmap_2026_10_26.md)。** 后续修改已推进到PCM实物抓取及单板TOA；不再重复旧诊断。整体到达精度和标准声识别尚未通过，报告中的部分稳定slot不代表达标。单A先验证可信时间输入、类型与质量、PTP映射，再用合成数据验证配对/距离；真实方向、两屏和同步精度留双板实物。

**最新先读[M4.2.5真实播放复验](Milestones/M4_2_5_Active_Search_Diagnostics_2026_10_26.md)。** 用户已开放声音测试；`SBCG-M4.2-SEARCHDIAG` 镜像自动播放标准WAV 20秒/10次，24个完整任务全部`LOW_CORR`、MATCH0；最高raw相关0.773821<0.80、RMS≈2300、N=257/V=257、LR/Z/I=0，RPT24/RLOST0，资源/日志/UI错误0；10个高能量候选、7个高相关任务。数字WAV与模板相关1.0，诊断链已实机工作；失败在门限前相关度，不是搜索预算/范围/低RMS；独立复核PASS_WITH_RESERVATIONS（无阻塞）。下一步保存PCM/受控声学实验，不直接降低门限；识别/PTP/测距仍未验收。

**最新M4.3.9结果周期刷新（2026-10-27）：** A每500ms重发同ID权威结果，B刷新2s有效期；新增`$MEASURE_RESULT_STATE`计数。无ACK/序列号、未烧录/未两板实测。[M4.3.9记录](Milestones/M4_3_9_Result_Refresh.md)。

**最新M4.3.8两屏权威结果交换（2026-10-27）：** A板发布唯一结果并用28字节MRES报文发给B，B校共同域/来源后显示A的同一ID/距离、不重算；无重传/ACK、未烧录、未两板实测。[M4.3.8记录](Milestones/M4_3_8_Authoritative_Result_Exchange.md)。

**最新M4.3.7波形分类接入（2026-10-27）：** 新增`event_classifier`（包络长度/前后过零率趋势/差分能量比，输出CHIRP/CLAP/UNKNOWN+quality），非CHIRP不进标准序列/UDP/配对；事件quality不再固定100。仅合成测试/未真实标准声/未烧录。[M4.3.7记录](Milestones/M4_3_7_Chirp_Clap_Classifier.md)。

**最新M4.3.6共同时间域门控（2026-10-27）：** 用TimeSync状态派生A参考/B内部锁定域，域无效时不发事件UDP并撤销测量结果；替代固定诊断domain。PPS QUALIFIED未直接接入、未烧录/未两板验收；标准声识别/距离仍需硬件。[M4.3.6记录](Milestones/M4_3_6_Time_Domain_Gate.md)。

**最新M4.3.5周期序列监控（2026-10-27）：** 冻结标准声后新增纯C 10次/2s周期检测（抖动/漏步/低质量/长静默O0/O2测试通过）；尚未接入App/UDP/测量门控，周期候选不等于标准声识别；MATCH0仍未解决、未烧录。[M4.3.5记录](Milestones/M4_3_5_Standard_Sequence_Monitor.md)。

**最新M4.3.4测量结果/状态页（2026-10-27）：** 配对结果收口为唯一`MeasurementResult`（ID/距离/方向/质量/温度版本/2s有效期），LCD/TouchGFX从同一快照显示；无温度/超龄/越界不出值。仍为诊断时间域/SYNC=0，未接真实两板/未烧录。[M4.3.4记录](Milestones/M4_3_4_Measurement_UI.md)。

**最新M4.3.3诊断事件UDP（2026-10-27）：** 成功窗口TOA事件已接入5009板间UDP、36字节线协议和纯配对/距离核心，输出`$SOUND_PAIR`；当前time_domain/事件类型/quality为诊断设置、SYNC=0，未接真实两板/未烧录，不能当同步或测距验收。[M4.3.3记录](Milestones/M4_3_3_Diagnostic_Event_UDP.md)。

**最新M4.3.2软件核心（2026-10-26）：** 两板事件配对/距离纯C核心与36字节事件线协议已完成O0/O2/Debug/Release验证；支持乱序、重复、歧义、质量/类型/共同时间域拒绝、`d=c|Δt|`和A/B方向。尚未接实际UDP/未烧录，不能把合成数据当两板验收。[M4.3.2记录](Milestones/M4_3_2_Pair_Distance_Core.md)。

**最新M4.3.1软件完成（2026-10-26）：** 检测器固定窗口TOA的整数锚点+Q16子帧已接入本地PTP模型；旧`SOUND_TIME`不变，新增`$SOUND_TOA_TIME`，TOA缺失/非法不回退FIRST+1。主机回归、Q16 Fraction、Debug/Release和audio layout通过；未烧录/未实物验收。下一软件增量：两板事件UDP/配对，之后仍需标准声身份/质量与硬件验证。[M4.3.1记录](Milestones/M4_3_1_Local_PTP_TOA_Time.md)。

**最新P1受控采集（2026-10-26）：** 严格工具口径音量全10槽12/15、D40 3/5、D50 5/5、D+30/D−30各4/5；[只读复核](Milestones/P1_ReadOnly_Analysis_2026_10_26.md)发现播放前/后背景污染槽基址，播放窗口径为音量15/15、D40 4/5、D50 6/6、D+30 6/6、D−30 5/5；D20与D40-01仍有真实候选缺失。下一步修工具事件分类/槽锚定，不改0.80或算法。[P1记录](Milestones/P1_Controlled_Acoustic_Capture_2026_10_26.md)。

此前最近记录为M4.2.4-A4优化镜像（固件标识仍`SBCG-M4.2-BATCH`，Debug de212005…）：STAT_MS 15→8–9ms、AUDIO 27→18ms、LOOP 40→34ms；同轮OTHER/FLUSH≈15ms成为最大单项，UI1–2ms；业务错误0。对照镜像未发现A3诊断抬高最大值。C2评估测得UART占用约21.5%、23.7行/秒，建议集中quiet/full策略。[A4记录](Milestones/M4_2_4_A4_Control_Statistics.md) · [C2评估](Milestones/M4_2_4_C2_Log_Evaluation.md)。此前A/B/A2/A3耗时与批量Stage、M4.2.3-B扫描静默证据按各自范围保留；主动标准音频仍因环境不能发声未验收。历史DIAG窗口定位：扫描59ms、评分5ms、日志208次队满；历史证据保留。

当前 **U3.1 TouchGFX显示范围已实机通过；首窗未知重启待查**：[U3.1记录](Milestones/U3_1_TouchGFX_Status_Page.md) · [当前状态](Project_State.md)。NoOS、软件绘图、约1Hz真实状态，距离/方向/质量仍占位；8根颜色低位暂缓。U2真实RGB565、U0软件页面和U1 SDRAM为历史前置，不重做色条、FMC/MPU或破坏性内存测试。

M2.3.1已完成A联合验收，用户随后烧录B；60秒双板各60包、B30请求通过，网络身份/共存通过，尚不证明双板同步精度。[网络证据](Milestones/M2_3_1_Dual_Board_Identity.md)。M3.2.4单板音频时间模型实测和更早计划均保留历史范围，无需重复实施。

本轮已完成系统架构、答辩资料及App中文注释整理；只读/注释维护不改变M3.2.4业务基线。[系统总览](System_Architecture.md)、[答辩稿](Defense_Guide.md)、[验证记录](Architecture_Comment_Review.md)。

## 按问题选择文档

| 用途 | 现行文档 | 阅读规则 |
| --- | --- | --- |
| 性能问题下一步怎样优化 | [M4.2.3优化方案](Plans/2026-10-25_Audio_Scan_Optimization/task_plan.md) | B批量扫描已静默实测SCAN≤3ms（原59ms）；主机压力已完成，主动声学与真实负载待验 |
| 下一批软件优化做什么 | [M4.2.5诊断记录](Milestones/M4_2_5_Search_Diagnostics.md) | 搜索诊断已实机；下一步失败原因/PCM与受控声学，C2暂缓 |
| 扫频识别现在做到哪一步 | [M4.2.2有界接入](Milestones/M4_2_2_Bounded_Chirp_Integration.md) | 初版/R1/R2静默失败保留；DIAG定位；扫描瓶颈已由M4.2.3-B静默改善，手机/标准音频识别仍待验收 |
| 整个系统如何构建与运行 | [System_Architecture](System_Architecture.md) | 先看目录职责，再沿真实启动/主循环/异步路径阅读 |
| 答辩怎样解释系统 | [Defense_Guide](Defense_Guide.md) | 正文为历史口头稿，先看页首U3.1更新；当前TouchGFX无RTOS |
| 本轮注释改了哪些、是否影响代码 | [Architecture_Comment_Review](Architecture_Comment_Review.md) | 58文件清单、函数覆盖与Flash字节等价证明 |
| 现在完成什么、板上是什么版本 | [Project_State](Project_State.md) | 每次开始先读；只维护当前状态，不堆叠历史快照 |
| 全部文档哪些还有效 | [Document_Status](Document_Status.md) | 按文件给出状态、报告与跳读规则 |
| 本次任务执行到哪里 | [M4.2.5真实播放复验](Milestones/M4_2_5_Active_Search_Diagnostics_2026_10_26.md) | 诊断链实机工作；识别0/10，下一步保存PCM/受控实验 |
| 课程目标和未实现部分 | [Project_Roadmap](Project_Roadmap.md) | 基础/提高指标和整机路线仍有效，不是一次实现全部功能的授权 |
| 构建、烧录、调试 | [Development_Workflow](Development_Workflow.md) | 日常操作指南；硬件访问遵守根规则和当前授权 |
| 实际连线、资源与未来方案 | [Hardware_Resources](Hardware_Resources.md) | 先看当前资源摘要；TouchGFX已实屏验收，PPS软件待波形验收；双板内部闭环按短时验收边界理解 |
| 串口和UDP字段 | [Protocol](Protocol.md) | 协议为现行接口，历史固件标识仅说明首次引入版本 |
| 自选标准音频和下载素材 | [Audio_Selection](Audio_Selection.md) | 老师允许自行选择；素材和纯C核心已准备，板端识别率未验收 |
| 网页串口怎么使用 | [SerialWeb](SerialWeb.md) | 115200/8N1；当前已支持手动温度命令；后半为早期发送实验 |
| 仅核对SYS/RCC | [CubeMX_Step1](CubeMX_Step1.md) | 已完成的系统基线，不重复启用外设 |
| 最近一次板端实验详情 | [M4.2.5真实播放复验](Milestones/M4_2_5_Active_Search_Diagnostics_2026_10_26.md) | 0/10仍失败但已定位到LOW_CORR；不表示声学通过 |
| 团队规则 | [AGENTS](../AGENTS.md)、[CONTRIBUTING](../CONTRIBUTING.md) | 持续有效 |

## 已完成文档的使用规则

- 审计时`Plans/`顶层20份旧方案及本轮新增M3.2.3方案均已完成其批准范围，页首标记“历史方案”。旧“下一步”、烧录批准或CubeMX修正不再作为当前操作指令；按需追溯，不重复实施。
- 审计时`Milestones/`的23份报告及本轮新增M3.2.3报告均为已归档证据。记录中的失败、延期和未测项保持原日期/固件边界；后续完成由独立报告补充。
- T1.1 R1正常测温通过，拔插恢复后来在T1.2完成连续补验；不能把T1.2结果反写成R1测试过。M3.1安静/温度联动实验当时未发声，后来sound-02已补齐受控幅度响应。
- 旧固件哈希、地址、日志计数与主机测试数量不是当前新版本的自动证明。主机回放通过不等于烧录、实物同步或测距通过。
- 第三方`Drivers/`、`Middlewares/`的许可和说明按原样保留；它们不是项目工作进度文档。本次全量审计范围是仓库自有57份Markdown，新增文档同时登记状态。

当前仍 **LOCAL/SYNC=0**。两板内部闭环已通过短时观测，外部同步精度/PPS波形、声学延迟校准、标准音频识别和测距尚未完成，LCD状态页已实测；历史M2.3.2软件及1分钟运行观测通过；交换机原分配A=2、B=1、PC=3口，当前仅A在场。

最新进度见[本轮复核](Progress_Review_2026_10_26.md)，最新记录镜像见[A4报告](Milestones/M4_2_4_A4_Control_Statistics.md)，显示范围见[U3.1报告](Milestones/U3_1_TouchGFX_Status_Page.md)。M4.1.3/U0是历史前置，M2.3.2短时双板观测不代表外部精度。

2026-10-23短时验收：B60秒236组四硬件时刻，协议估算−65～+340ns，控制错误/拒绝0；稳定网络A/B各60包零缺口。此为内部估计，各窗口部分重叠，未证明外部同步精度。详见本次交付记录。
