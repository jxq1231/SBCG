# 项目文档入口

<!-- doc-status: current-entry; reviewed: 2026-10-23 -->

更新：2026-10-23。**先读本页，再读当前状态和本次计划；无需按时间顺序重读所有历史文档。**

当前已验收板端版本为 **SBCG-M3.2-MAP（M3.2.4）**。120秒采集取得120个有效锚点、40组板端估算与真实C/Fraction一致，最大绝对留出残差14.884us，音频/温度/三路UDP复验通过。[本轮完整结果](Milestones/M3_2_4_Audio_Time_Map_Board.md)。

M3.2.4[实施计划](Plans/2026-10-23_Audio_Time_Map_Board_Implementation.md)已完成并归档，不重复实施。前置M3.2.3为当时纯软件验证，历史“未接入”不描述当前固件。下一独立增量尚未启动。

本轮已完成系统架构、答辩资料及App中文注释整理；只读/注释维护不改变M3.2.4业务基线。[系统总览](System_Architecture.md)、[答辩稿](Defense_Guide.md)、[验证记录](Architecture_Comment_Review.md)。

## 按问题选择文档

| 用途 | 现行文档 | 阅读规则 |
| --- | --- | --- |
| 整个系统如何构建与运行 | [System_Architecture](System_Architecture.md) | 先看目录职责，再沿真实启动/主循环/异步路径阅读 |
| 答辩怎样解释系统 | [Defense_Guide](Defense_Guide.md) | 30秒、两分钟和追问，当前无TouchGFX/RTOS |
| 本轮注释改了哪些、是否影响代码 | [Architecture_Comment_Review](Architecture_Comment_Review.md) | 58文件清单、函数覆盖与Flash字节等价证明 |
| 现在完成什么、板上是什么版本 | [Project_State](Project_State.md) | 每次开始先读；只维护当前状态，不堆叠历史快照 |
| 全部文档哪些还有效 | [Document_Status](Document_Status.md) | 按文件给出状态、报告与跳读规则 |
| 本次任务执行到哪里 | [M3.2.4实施计划](Plans/2026-10-23_Audio_Time_Map_Board_Implementation.md) | 已完成的最近增量，按需追溯，不重复烧录 |
| 课程目标和未实现部分 | [Project_Roadmap](Project_Roadmap.md) | 基础/提高指标和整机路线仍有效，不是一次实现全部功能的授权 |
| 构建、烧录、调试 | [Development_Workflow](Development_Workflow.md) | 日常操作指南；硬件访问遵守根规则和当前授权 |
| 实际连线、资源与未来方案 | [Hardware_Resources](Hardware_Resources.md) | 先看当前资源摘要；显示/PPS/双板等明确为未实现目标 |
| 串口和UDP字段 | [Protocol](Protocol.md) | 协议为现行接口，历史固件标识仅说明首次引入版本 |
| 网页串口怎么使用 | [SerialWeb](SerialWeb.md) | 115200/8N1；当前已支持手动温度命令；后半为早期发送实验 |
| 仅核对SYS/RCC | [CubeMX_Step1](CubeMX_Step1.md) | 已完成的系统基线，不重复启用外设 |
| 最近一次板端实验详情 | [M3.2.4验收](Milestones/M3_2_4_Audio_Time_Map_Board.md) | 需要复核原始证据/限制时读 |
| 团队规则 | [AGENTS](../AGENTS.md)、[CONTRIBUTING](../CONTRIBUTING.md) | 持续有效 |

## 已完成文档的使用规则

- 审计时`Plans/`顶层20份旧方案及本轮新增M3.2.3方案均已完成其批准范围，页首标记“历史方案”。旧“下一步”、烧录批准或CubeMX修正不再作为当前操作指令；按需追溯，不重复实施。
- 审计时`Milestones/`的23份报告及本轮新增M3.2.3报告均为已归档证据。记录中的失败、延期和未测项保持原日期/固件边界；后续完成由独立报告补充。
- T1.1 R1正常测温通过，拔插恢复后来在T1.2完成连续补验；不能把T1.2结果反写成R1测试过。M3.1安静/温度联动实验当时未发声，后来sound-02已补齐受控幅度响应。
- 旧固件哈希、地址、日志计数与主机测试数量不是当前新版本的自动证明。主机回放通过不等于烧录、实物同步或测距通过。
- 第三方`Drivers/`、`Middlewares/`的许可和说明按原样保留；它们不是项目工作进度文档。本次全量审计范围是仓库自有57份Markdown，新增文档同时登记状态。

当前仍 **LOCAL/SYNC=0**。第二板同步/PPS、声学延迟校准、事件检测、测距与LCD尚未完成；不检查第二板的现有边界保持。
