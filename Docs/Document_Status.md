# 文档状态与阅读索引

<!-- doc-status: current-document-index; reviewed: 2026-10-26 -->

审计日期：2026-10-23。已检查原有 **57份仓库自有Markdown**：顶层历史方案20份、历史验收23份，其余入口/资源/规则/模板14份。第三方Drivers/Middlewares许可与说明、构建输出、Logs内实验快照不作为项目进度文档，不改写。新文档随本轮登记。

## 防止重复实施

1. 每次先读[现行入口](README.md)和[当前状态](Project_State.md)，再读当前增量的计划。旧`Plans/`不构成新授权；历史烧录许可和CubeMX修正不可复用为当前操作。
2. `archived-plan`表示原批准范围已交付；不表示所有未来设想都实现。`completed-evidence`表示实验记录已归档；不把其未测/失败变成成功。
3. 历史M2.3.2时两板由用户手动更新到M2.3.2/SBCG-M2.3-SYNC，并通过1分钟运行内部闭环观测；此前M2.3.1的A123秒联合验收及双板网络证据按历史范围保留。此前M3.2.4的120秒/40组估算证据按原范围保留。M3.2.3纯软件记录保留其当时未接入/未烧录边界。
4. R1温度正常采样、T1.2后续连续拔插、sound-01延后和sound-02通过分别保留。旧状态已标注，禁止跨版本合并成一次全绿实验。
5. U3.1 TouchGFX显示范围已通过单A实屏及82.55秒静默，首窗重启原因未知；U2 RGB565为历史已验收后端；FMC/Region1/2已生成且U1-SDRAM-R1四轮8MB自检及静默共存通过；PPS已由用户生成硬件配置并接入软件，实物未验收；双板闭环源码按M2.3.2记录，短时运行通过，外部精度待验收；现已生成的SAI2/DMA2、USART1 RX、温度和网络不再作为待配置步骤。

当前 **M4.2.2初版、R1、R2均已烧录且静默验收失败**，R2搜索DROP56、串口TX_ERRORS503，下一步需软件修复；DIAG定位已实测：扫描59ms/评分5ms，21次超时/13次覆盖、208次日志队满；性能仍开放。历史M3.2.4板端接入与实物验收计划和报告均已归档。当前M2.3.1身份选择软件与A板联合实测通过，双板网络共存随后通过；M2.3.2软件及1分钟运行内部闭环通过，外部精度未测。M4.2.3-B扫频批量接入已烧录A板并静默通过（SCAN≤3ms、DONE_MAX≤494ms、日志/音频零错误），主动标准音频与MATCH未验收。M4.2.4-A前台耗时分解、M4.2.4-B原候选批量迁移均已静默通过（STAGE21→7ms、GAP60→49ms）；C UI/日志未开始。旧方案和历史烧录授权不可作为新版本的操作指令。

## 逐文件状态

此前登记91份自有Markdown，本轮补登记U2验收报告、M4.2.3-A批量门控报告、M4.2.3-B静默实测报告、M4.2.4项目书、M4.2.4-A测量记录及M4.2.4-B批量Stage报告。最新M4.1.3本地模型与R1日志修复静默范围通过；U0 RGB565页面完成软件验证，U1首版失败保留，Region2修复后R1已通过全8MB四轮及约79秒静默共存；U2 RGB565真实状态页已通过单A实物和用户目视，跳过独立色条；8根低位暂缓。旧“未接入/未烧录”和主动声音失败仅指各自日期/版本，不重复执行。

| 文件 | 状态 | 何时阅读 |
| --- | --- | --- |
| [M4.2.3性能方案](Plans/2026-10-25_Audio_Scan_Optimization/task_plan.md) | A/B软件完成·B静默通过·主动声学待验 | 批量门控、语义对照、资源预算及分层验收；依据/规划进度在同目录按需读取 |
| [M4.2.3-A批量门控核心](Milestones/M4_2_3_A_Batch_Detector_Core.md) | 软件等价验证通过·已接入 | 64点批量入口、逐字段对照与复跑证据；B已接入并有静默实测 |
| [M4.2.3-B静默实测](Milestones/M4_2_3_B_Batch_Scan.md) | 静默范围通过·主动声学待验 | 批量扫描、时延诊断、A板烧录与零错误证据；不表示MATCH/声学精度 |
| [M4.2.4项目书](Plans/2026-10-26_M4_2_4_Foreground_Optimization.md) | A/B完成·C未开始 | A前台分解、B原候选批量均已完成；C UI/日志待证据，烧录另授权 |
| [M4.2.4-A测量记录](Milestones/M4_2_4_A_Foreground_Profile.md) | 分模块耗时分解通过·未做性能修复 | 静默STAGE21/UI19/GAP60ms及零错误证据；不表示声学/实时最坏时间 |
| [M4.2.4-B批量Stage](Milestones/M4_2_4_B_Batch_Stage.md) | 批量迁移静默对比通过·C未开始 | STAGE21→7ms、GAP60→49ms、业务零错误；不表示声学精度 |
| [M4.2.2有界接入](Milestones/M4_2_2_Bounded_Chirp_Integration.md) | 初版/R1/R2静默均失败 | 失败证据、缓存/调度修复、冻结镜像及验收边界 |
| [M4.2.2实施计划](Plans/2026-10-25_Bounded_Chirp_Integration.md) | 当前计划·DIAG观测已完成 | 下一步优化能量扫描，保留各自阈值，三次失败保留 |
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
