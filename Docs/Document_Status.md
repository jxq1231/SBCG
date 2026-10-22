# 文档状态与阅读索引

<!-- doc-status: current-document-index; reviewed: 2026-10-23 -->

审计日期：2026-10-23。已检查原有 **57份仓库自有Markdown**：顶层历史方案20份、历史验收23份，其余入口/资源/规则/模板14份。第三方Drivers/Middlewares许可与说明、构建输出、Logs内实验快照不作为项目进度文档，不改写。新文档随本轮登记。

## 防止重复实施

1. 每次先读[现行入口](README.md)和[当前状态](Project_State.md)，再读当前增量的计划。旧`Plans/`不构成新授权；历史烧录许可和CubeMX修正不可复用为当前操作。
2. `archived-plan`表示原批准范围已交付；不表示所有未来设想都实现。`completed-evidence`表示实验记录已归档；不把其未测/失败变成成功。
3. 当前最新实物基线是M3.2.2/SBCG-M3.2-ANCHOR；M3.2.3纯算法与已有日志回放已批准，本表初次整理时尚未实施，不要求新硬件测试。
4. R1温度正常采样、T1.2后续连续拔插、sound-01延后和sound-02通过分别保留。旧状态已标注，禁止跨版本合并成一次全绿实验。
5. 资源表内LCD/FMC/PPS/双板架构属于未来设计；现已生成的SAI2/DMA2、USART1 RX、温度和网络不再作为待配置步骤。

## 逐文件状态

| 文件 | 状态 | 何时阅读 |
| --- | --- | --- |
| [.github/ISSUE_TEMPLATE/experiment.md](../.github/ISSUE_TEMPLATE/experiment.md) | 持续有效·协作模板 | 新建Issue/PR时使用，非实验结论 |
| [.github/ISSUE_TEMPLATE/task.md](../.github/ISSUE_TEMPLATE/task.md) | 持续有效·协作模板 | 新建Issue/PR时使用，非实验结论 |
| [.github/PULL_REQUEST_TEMPLATE.md](../.github/PULL_REQUEST_TEMPLATE.md) | 持续有效·协作模板 | 新建Issue/PR时使用，非实验结论 |
| [AGENTS.md](../AGENTS.md) | 持续有效·工程规则 | 修改前必读，不归档 |
| [CONTRIBUTING.md](../CONTRIBUTING.md) | 持续有效·协作指南 | 协作时按需查阅 |
| [Docs/CubeMX_Step1.md](CubeMX_Step1.md) | 已完成·基线参考 | 仅复核SYS/RCC时读，不重复配置 |
| [Docs/Development_Workflow.md](Development_Workflow.md) | 现行入口/状态/接口 | 按文档入口选择，非旧计划待办 |
| [Docs/Document_Status.md](Document_Status.md) | 现行入口/状态/接口 | 按文档入口选择，非旧计划待办 |
| [Docs/Hardware_Resources.md](Hardware_Resources.md) | 现行参考·含未来设计 | 读当前资源摘要；未来外设未自动启用 |
| [Docs/Milestones/M0_1_Startup.md](Milestones/M0_1_Startup.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M0_2_Serial.md](Milestones/M0_2_Serial.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M1_1_Ethernet_Setup.md](Milestones/M1_1_Ethernet_Setup.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M1_2_UDP_Send.md](Milestones/M1_2_UDP_Send.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M1_2_UDP_Setup.md](Milestones/M1_2_UDP_Setup.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M1_3_UDP_Request_Reply.md](Milestones/M1_3_UDP_Request_Reply.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M1_4_UDP_Reconnect.md](Milestones/M1_4_UDP_Reconnect.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M2_1_PTP_Clock.md](Milestones/M2_1_PTP_Clock.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M2_1_PTP_RX.md](Milestones/M2_1_PTP_RX.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M2_1_PTP_TX.md](Milestones/M2_1_PTP_TX.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M2_2_Board_Observation.md](Milestones/M2_2_Board_Observation.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M2_2_Sync_Estimator.md](Milestones/M2_2_Sync_Estimator.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M2_2_Sync_Observation.md](Milestones/M2_2_Sync_Observation.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M2_2_Time_Exchange.md](Milestones/M2_2_Time_Exchange.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M3_0_Codec_ID.md](Milestones/M3_0_Codec_ID.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M3_1_Audio_DMA.md](Milestones/M3_1_Audio_DMA.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M3_1_Quiet_Stability.md](Milestones/M3_1_Quiet_Stability.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M3_1_Temperature_Coexistence.md](Milestones/M3_1_Temperature_Coexistence.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M3_2_1_Audio_Frame_Index.md](Milestones/M3_2_1_Audio_Frame_Index.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/M3_2_2_Audio_PTP_Anchor.md](Milestones/M3_2_2_Audio_PTP_Anchor.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/T1_1_Temperature.md](Milestones/T1_1_Temperature.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/T1_1_Temperature_Replug.md](Milestones/T1_1_Temperature_Replug.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Milestones/T1_2_Manual_Temperature.md](Milestones/T1_2_Manual_Temperature.md) | 已完成·归档证据 | 只核对对应固件/窗口；保留失败和未测边界 |
| [Docs/Plans/2026-10-20_PTP_Clock.md](Plans/2026-10-20_PTP_Clock.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-20_PTP_RX_Design.md](Plans/2026-10-20_PTP_RX_Design.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-20_PTP_RX_Implementation.md](Plans/2026-10-20_PTP_RX_Implementation.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-20_PTP_TX_Design.md](Plans/2026-10-20_PTP_TX_Design.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-20_PTP_TX_Implementation.md](Plans/2026-10-20_PTP_TX_Implementation.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-20_Sync_Estimator_Design.md](Plans/2026-10-20_Sync_Estimator_Design.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-20_Sync_Estimator_Implementation.md](Plans/2026-10-20_Sync_Estimator_Implementation.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-20_Time_Exchange_IO_Design.md](Plans/2026-10-20_Time_Exchange_IO_Design.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-20_Time_Exchange_IO_Implementation.md](Plans/2026-10-20_Time_Exchange_IO_Implementation.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-21_Board_Sync_Observation_Design.md](Plans/2026-10-21_Board_Sync_Observation_Design.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-21_Board_Sync_Observation_Implementation.md](Plans/2026-10-21_Board_Sync_Observation_Implementation.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-21_Manual_Temperature.md](Plans/2026-10-21_Manual_Temperature.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-21_Sync_Observation_Design.md](Plans/2026-10-21_Sync_Observation_Design.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-21_Sync_Observation_Implementation.md](Plans/2026-10-21_Sync_Observation_Implementation.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-21_Temperature_Integration_Design.md](Plans/2026-10-21_Temperature_Integration_Design.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-21_Temperature_Integration_Implementation.md](Plans/2026-10-21_Temperature_Integration_Implementation.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-22_Audio_DMA_Design.md](Plans/2026-10-22_Audio_DMA_Design.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-22_Audio_Frame_Index_Design.md](Plans/2026-10-22_Audio_Frame_Index_Design.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-22_Audio_PTP_Anchor_Design.md](Plans/2026-10-22_Audio_PTP_Anchor_Design.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/2026-10-22_Codec_ID_Design.md](Plans/2026-10-22_Codec_ID_Design.md) | 已完成·历史方案 | 跳过常规读取；需要追溯时看页首验收链接，勿重复操作 |
| [Docs/Plans/M3_2_3/findings.md](Plans/M3_2_3/findings.md) | 当前执行·M3.2.3 | 本轮任务计划/发现/进度；实施中，不是已完成证据 |
| [Docs/Plans/M3_2_3/progress.md](Plans/M3_2_3/progress.md) | 当前执行·M3.2.3 | 本轮任务计划/发现/进度；实施中，不是已完成证据 |
| [Docs/Plans/M3_2_3/task_plan.md](Plans/M3_2_3/task_plan.md) | 当前执行·M3.2.3 | 本轮任务计划/发现/进度；实施中，不是已完成证据 |
| [Docs/Project_Roadmap.md](Project_Roadmap.md) | 现行入口/状态/接口 | 按文档入口选择，非旧计划待办 |
| [Docs/Project_State.md](Project_State.md) | 现行入口/状态/接口 | 按文档入口选择，非旧计划待办 |
| [Docs/Protocol.md](Protocol.md) | 现行入口/状态/接口 | 按文档入口选择，非旧计划待办 |
| [Docs/README.md](README.md) | 现行入口/状态/接口 | 按文档入口选择，非旧计划待办 |
| [Docs/SerialWeb.md](SerialWeb.md) | 现行指南·含历史 | 读页首命令和连接步骤；M0.2旧实验按需追溯 |
| [README.md](../README.md) | 现行入口/状态/接口 | 按文档入口选择，非旧计划待办 |

## 本次修正

- 重写Docs入口和当前状态，移除互相矛盾的多份“当前”摘要；历史证据仍完整留在Milestones。
- 路线图第9节从过时的M2.2.4待实施改为M3.2.3；保留任务书指标和历史日期段落。
- 补齐旧方案/实验的页首状态，标记早期CubeMX时隙问题已解决；不改动真实失败、延期、哈希和各版未测项。
- 修正系统基线、硬件DMA/SAI、串口RX及协议页的过期状态；DHT11当前接线仍为3.3V、PG7/D4。
- 首页只保留最新摘要和分类进度，链接到单一现行状态；不再要求从旧启动阶段文档逐个重读。

本次审计核对本地代码、历史报告和文档内链接；没有重新核验外部网站、PCB修订、第三方手册全部引脚或实物波形，未新增硬件通过结论。
