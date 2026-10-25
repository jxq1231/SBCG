# 项目文档入口

<!-- doc-status: current-entry; reviewed: 2026-10-25 -->

更新：2026-10-25。**先读本页，再读当前状态和本次计划；无需按时间顺序重读所有历史文档。**

当前A板为`SBCG-M4.2-CHIRP-R2`（Debug d089855a3ea9），BOOT后61.17秒静默仍失败：音频累计丢块/错误0，但搜索DROP56、串口TX_ERRORS503，日志有缺口。下一步降低相关计算负担并修正日志调度，没有新待烧录镜像。[最新验收](Milestones/M4_2_2_Bounded_Chirp_Integration.md)。历史额外启动原因仍未知，下方显示验收按原U3.1范围理解。

当前 **U3.1 TouchGFX显示范围已实机通过；首窗未知重启待查**：[U3.1记录](Milestones/U3_1_TouchGFX_Status_Page.md) · [当前状态](Project_State.md)。NoOS、软件绘图、约1Hz真实状态，距离/方向/质量仍占位；8根颜色低位暂缓。U2真实RGB565、U0软件页面和U1 SDRAM为历史前置，不重做色条、FMC/MPU或破坏性内存测试。

M2.3.1已完成A联合验收，用户随后烧录B；60秒双板各60包、B30请求通过，网络身份/共存通过，尚不证明双板同步精度。[网络证据](Milestones/M2_3_1_Dual_Board_Identity.md)。M3.2.4单板音频时间模型实测和更早计划均保留历史范围，无需重复实施。

本轮已完成系统架构、答辩资料及App中文注释整理；只读/注释维护不改变M3.2.4业务基线。[系统总览](System_Architecture.md)、[答辩稿](Defense_Guide.md)、[验证记录](Architecture_Comment_Review.md)。

## 按问题选择文档

| 用途 | 现行文档 | 阅读规则 |
| --- | --- | --- |
| 扫频识别现在做到哪一步 | [M4.2.2有界接入](Milestones/M4_2_2_Bounded_Chirp_Integration.md) | 初版/R1/R2静默均失败；当前需继续修复搜索负载与日志拥堵，手机识别未验收 |
| 整个系统如何构建与运行 | [System_Architecture](System_Architecture.md) | 先看目录职责，再沿真实启动/主循环/异步路径阅读 |
| 答辩怎样解释系统 | [Defense_Guide](Defense_Guide.md) | 正文为历史口头稿，先看页首U3.1更新；当前TouchGFX无RTOS |
| 本轮注释改了哪些、是否影响代码 | [Architecture_Comment_Review](Architecture_Comment_Review.md) | 58文件清单、函数覆盖与Flash字节等价证明 |
| 现在完成什么、板上是什么版本 | [Project_State](Project_State.md) | 每次开始先读；只维护当前状态，不堆叠历史快照 |
| 全部文档哪些还有效 | [Document_Status](Document_Status.md) | 按文件给出状态、报告与跳读规则 |
| 本次任务执行到哪里 | [M4.2.2验收记录](Milestones/M4_2_2_Bounded_Chirp_Integration.md) | 三版静默失败证据及下一软件工作；U3.1/U2显示报告按历史范围阅读 |
| 课程目标和未实现部分 | [Project_Roadmap](Project_Roadmap.md) | 基础/提高指标和整机路线仍有效，不是一次实现全部功能的授权 |
| 构建、烧录、调试 | [Development_Workflow](Development_Workflow.md) | 日常操作指南；硬件访问遵守根规则和当前授权 |
| 实际连线、资源与未来方案 | [Hardware_Resources](Hardware_Resources.md) | 先看当前资源摘要；TouchGFX已实屏验收，PPS软件待波形验收；双板内部闭环按短时验收边界理解 |
| 串口和UDP字段 | [Protocol](Protocol.md) | 协议为现行接口，历史固件标识仅说明首次引入版本 |
| 自选标准音频和下载素材 | [Audio_Selection](Audio_Selection.md) | 老师允许自行选择；素材和纯C核心已准备，板端识别率未验收 |
| 网页串口怎么使用 | [SerialWeb](SerialWeb.md) | 115200/8N1；当前已支持手动温度命令；后半为早期发送实验 |
| 仅核对SYS/RCC | [CubeMX_Step1](CubeMX_Step1.md) | 已完成的系统基线，不重复启用外设 |
| 最近一次板端实验详情 | [M4.2.2三次静默](Milestones/M4_2_2_Bounded_Chirp_Integration.md) | R2音频累计零丢块，但搜索丢弃/串口错误仍在，验收失败 |
| 团队规则 | [AGENTS](../AGENTS.md)、[CONTRIBUTING](../CONTRIBUTING.md) | 持续有效 |

## 已完成文档的使用规则

- 审计时`Plans/`顶层20份旧方案及本轮新增M3.2.3方案均已完成其批准范围，页首标记“历史方案”。旧“下一步”、烧录批准或CubeMX修正不再作为当前操作指令；按需追溯，不重复实施。
- 审计时`Milestones/`的23份报告及本轮新增M3.2.3报告均为已归档证据。记录中的失败、延期和未测项保持原日期/固件边界；后续完成由独立报告补充。
- T1.1 R1正常测温通过，拔插恢复后来在T1.2完成连续补验；不能把T1.2结果反写成R1测试过。M3.1安静/温度联动实验当时未发声，后来sound-02已补齐受控幅度响应。
- 旧固件哈希、地址、日志计数与主机测试数量不是当前新版本的自动证明。主机回放通过不等于烧录、实物同步或测距通过。
- 第三方`Drivers/`、`Middlewares/`的许可和说明按原样保留；它们不是项目工作进度文档。本次全量审计范围是仓库自有57份Markdown，新增文档同时登记状态。

当前仍 **LOCAL/SYNC=0**。两板内部闭环已通过短时观测，外部同步精度/PPS波形、声学延迟校准、标准音频识别和测距尚未完成，LCD状态页已实测；历史M2.3.2软件及1分钟运行观测通过；交换机原分配A=2、B=1、PC=3口，当前仅A在场。

最新固件与静默运行见[M4.1.3记录](Milestones/M4_1_3_Sound_Event_Time.md)，当前软件页面和下一步见[U0报告](Milestones/U0_RGB565_Status_Page.md)。M2.3.2短时双板观测按历史范围保留，外部精度未测。

2026-10-23短时验收：B60秒236组四硬件时刻，协议估算−65～+340ns，控制错误/拒绝0；稳定网络A/B各60包零缺口。此为内部估计，各窗口部分重叠，未证明外部同步精度。详见本次交付记录。
