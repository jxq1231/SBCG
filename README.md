<p align="center">
  <img src=".github/assets/sbcg-banner.svg" alt="SBCG · 分布式声波测距系统" width="100%">
</p>

<p align="center">
  <strong>双节点协同 · 硬件时间同步 · 声音到达时间差 · 统一温度补偿</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-STM32F746-0e7490?style=flat-square" alt="STM32F746">
  <img src="https://img.shields.io/badge/Toolchain-GCC%20%2B%20CMake-334155?style=flat-square" alt="GCC and CMake">
  <img src="https://img.shields.io/badge/Stage-M2.3.1%20A%20%7C%20Verified-15803d?style=flat-square" alt="M2.3.1 A板实物通过，B待更新">
  <a href="https://github.com/jxq1231/SBCG/actions/workflows/host-checks.yml"><img src="https://github.com/jxq1231/SBCG/actions/workflows/host-checks.yml/badge.svg" alt="Host checks"></a>
</p>

<p align="center">
  <a href="#快速开始">快速开始</a> ·
  <a href="#项目进度">项目进度</a> ·
  <a href="#文档导航">文档导航</a> ·
  <a href="CONTRIBUTING.md">协作指南</a> ·
  <a href="https://github.com/jxq1231/SBCG/issues">任务与讨论</a>
</p>

<!-- doc-status: current-summary; reviewed: 2026-10-23 -->

## 项目介绍

SBCG是基于两块 **STM32F746G-DISCO** 的分布式声波测距课程项目。目标是在板端完成以太网时间同步、声音事件采集、到达时间差计算和温度补偿，并在两块板上显示一致的测量结果。

这个仓库用于共享工程、审核硬件资源、跟踪实验结果和协作开发。**以下架构描述为设计目标，单板LwIP基本ping、逐秒单向UDP及电脑请求/板端应答已验收；两板通信、时间同步及测距业务尚未实现。**

| 设计方向 | 团队约定 |
| --- | --- |
| 时间同步 | ETH IEEE1588硬件时间为唯一测量时间源，A主/B从 |
| PPS观测 | TIM2仅执行PTP整秒脉冲输出，计划使用PA15，不另建测量时钟 |
| 声音测量 | 单板SAI/DMA连续采集已验收；后续建立样本时间映射、声音事件与TDOA |
| 温度补偿 | A板读取一只DHT11，经UDP共享温度快照；两端使用同一整数公式 |
| 显示 | LTDC、帧缓冲、DMA2D和绘图数据统一RGB565 |
| 双板身份 | 同一固件根据真实MCU UID固定分配A/B角色 |

> 测距布置和声源位置决定TDOA对应的物理含义，详见[硬件资源与测量约定](Docs/Hardware_Resources.md)。电脑用于开发、观察和设参，最终测量在板端完成。

## 项目进度

**当前开发M2.3.1：UID角色及唯一IP/MAC已完成软件验证和A板实物验收。** 本轮A已烧录SBCG-M2.3-ROLE并通过123秒联合验收：110包UDP无缺口、两组各30请求通过，音频/温度/PTP保持正常。B板之后由用户自行烧录同一固件，尚未验收。新固件SBCG-M2.3-ROLE自动识别A=192.168.10.1、B=192.168.10.2，仍SYNC=0，尚未实现板间校时。11组原生回归、Tools108/Analysis87及两配置构建/布局通过。[固件与验收步骤](Docs/Milestones/M2_3_1_Dual_Board_Identity.md)。

下方保留前一M3.2.4已验收的硬件基线。

**M3.2.4板端时间估算接入已通过实物验收。** 前一固件SBCG-M3.2-MAP：120秒取得120个有效锚点，40组MAP/CHECK与真实C及Fraction逐值一致，最大绝对留出残差14.884us。原音频、自动温度和三路UDP同时通过。[完整结果与限制](Docs/Milestones/M3_2_4_Audio_Time_Map_Board.md)。

更新：2026-10-23。UDP收到110包零缺口，两组各30请求通过，60次codec ID8994读取及23个温度版本无错。仍 **LOCAL/SYNC=0**，这是DMA位置的线性时间模型自检，尚未校准声音到达延迟，不代表检测、同步或测距精度通过。前置[M3.2.3纯算法/回放](Docs/Milestones/M3_2_3_Audio_Time_Map.md)已归档，其“未接入”仅描述当时版本。

| 范围 | 当前结论 | 记录 |
| --- | --- | --- |
| 系统与串口 | 200MHz启动、USART1日志及RX命令均已分版实测 | [当前状态](Docs/Project_State.md) |
| 单板网络/PTP | UDP、重连、RX/TX戳、时间交换和板端估算观测已实测 | [协议](Docs/Protocol.md) |
| 温度 | DHT11 3.3V/PG7正常采集、手动设温及连续拔插恢复已实测 | [手动温度](Docs/Milestones/T1_2_Manual_Temperature.md)、[恢复](Docs/Milestones/T1_1_Temperature_Replug.md) |
| 音频 | codec ID、PCM连续采集/受控响应、帧编号、本地PTP锚点和时间估算自检已实测 | [M3.1](Docs/Milestones/M3_1_Audio_DMA.md)、[M3.2.2](Docs/Milestones/M3_2_2_Audio_PTP_Anchor.md) |
| 双板同步/PPS、声学时间校准、检测/测距、LCD | 尚未完成；当前先交付A程序、B延后烧录 | [课程路线](Docs/Project_Roadmap.md) |

历史方案和各版实验按需查[文档状态表](Docs/Document_Status.md)，勿从旧计划重复配置/烧录。

完整要求及实施顺序见[任务书路线与验收矩阵](Docs/Project_Roadmap.md)，当前实测范围见[项目状态](Docs/Project_State.md)。路线优先建立观测、网络与统一时间，再完成声音检测、测距和基础LCD；基础60分闭环后推进提高功能。所有文档从[Docs入口](Docs/README.md)阅读。

| 里程碑 | 验收目标 | 当前状态 |
| --- | --- | --- |
| M0 | 资源规划与系统基线 | 本板启动通过；M0.2启动/心跳发送已通过用户实物日志验收；MCU接收及手动设温已由T1.2验收 |
| M1 | 以太网链路与双向UDP通信 | 本板与电脑正常请求/应答、两次链路恢复及5005接收已验证；两板UDP未实现 |
| M2 | 时间同步及PPS精度验证 | 本地时钟、RX/TX、UDP时间戳回报及板端估算观测通过；两板同步及PPS待实现 |
| M3 | 音频DMA采集 | M3.1连续采集/幅度响应已验收；M3.2.1编号/M3.2.2锚点通过；M3.2.3纯算法通过；M3.2.4板端时间估算正常窗口已实测 |
| M4 | 声音事件检测 | 未开始 |
| M5 | TDOA测量及标定 | 未开始 |
| M6 / M7 | 课程基础 / 提高要求验收 | 未开始 |

## 快速开始

### 1. 获取工程

```powershell
git clone https://github.com/jxq1231/SBCG.git
cd SBCG
```

审阅连接/构建诊断修复与最新单板启动记录时，另外执行：

```powershell
git switch --track origin/jxq/m0-clock-review
```

### 2. 准备开发环境

安装Git、Python 3.9及以上、CMake、Ninja和ARM GNU Toolchain。编辑器使用VS Code，并按仓库推荐安装扩展。需要生成硬件配置时使用团队约定的CubeMX 6.18.1 / CubeF7 V1.17.4；烧录调试需要OpenOCD与ST-LINK。

当前VS Code设置中含有维护者机器上的GDB、OpenOCD、clangd和编译器路径，队友首次使用须按本机安装位置调整；不要直接覆盖其他成员的工具设置。具体见[协作指南](CONTRIBUTING.md#本机工具配置)。

### 3. 检查与构建

在工程根目录执行：

```powershell
python -B Tools/Monitor/autodebug.py doctor
python -B Tools/Monitor/autodebug.py build
python -B -m unittest discover -s Tools/Monitor/tests -v
```

VS Code中，`Ctrl+Shift+B`运行默认Debug构建；“运行任务 → STM32: AI Debug”会构建并保存诊断报告。报告位于`Logs/AutoDebug/`，不提交到Git。

**构建成功只代表编译和链接通过。** 烧录、实物启动、同步精度和最终测距均需单独记录结果。GitHub的Host checks仅运行主机工具测试，不连接开发板。

## 文档导航

| 文档 | 内容 |
| --- | --- |
| [Docs入口](Docs/README.md) | 阅读顺序、资料依据与已验证/目标配置的区别 |
| [系统架构](Docs/System_Architecture.md) | 实际目录职责、启动/主循环/IRQ与模块数据流 |
| [答辩指南](Docs/Defense_Guide.md) | 30秒、两分钟回答和常见追问 |
| [注释与验证](Docs/Architecture_Comment_Review.md) | 58个App文件说明、函数覆盖及代码等价验证 |
| [Project State](Docs/Project_State.md) | 当前分支、已完成事项、待验收项目和下一步 |
| [Project Roadmap](Docs/Project_Roadmap.md) | 任务书基础/提高指标、全部开发步骤、误差预算与验收方法 |
| [CubeMX Step 1](Docs/CubeMX_Step1.md) | SYS/RCC配置、200 MHz基线及启动验收步骤 |
| [Single-board Startup](Docs/Milestones/M0_1_Startup.md) | ELF与Flash比对、时钟寄存器、主循环、HAL时基及单板验收范围 |
| [Hardware Resources](Docs/Hardware_Resources.md) | 引脚、时钟、DMA、显示、同步、温度和测距约定 |
| [Development Workflow](Docs/Development_Workflow.md) | VS Code入口、主机诊断工具、退出码和日志 |
| [USART1 / SerialWeb](Docs/SerialWeb.md) | 串口配置审查、指定网页工具、COM口及收发验收边界 |
| [Contributing](CONTRIBUTING.md) | 分支、提交、PR、CubeMX所有权及实验记录要求 |
| [Project Rules](AGENTS.md) | 工程修改、生成代码保护和验证提交规则 |

## 仓库结构

```text
SBCG/
├── App/                  应用调度、串口与PHY资源抽象
├── Core/                 CubeMX生成的应用入口与中断代码
├── Drivers/              HAL与CMSIS依赖
├── LWIP/                 CubeMX生成的网络接口与协议栈配置
├── Middlewares/          随工程生成的LwIP第三方源码
├── cmake/                现有交叉编译及CubeMX构建集成
├── Tools/Monitor/        主机环境检查、构建诊断与地址定位
├── Docs/                 资源约定、项目状态与操作指南
├── .vscode/              构建、烧录、调试和语言服务配置
├── .github/              首页素材、协作模板与主机测试
└── SBCG.ioc              CubeMX硬件配置源
```

`build/`、`Logs/`和`compile_commands.json`为本机生成内容。`App/`已有串口、网络/PTP观测、温度和音频模块；后续只按当前里程碑需要扩展。

## 一起开发

1. 先在[Issues](https://github.com/jxq1231/SBCG/issues)说明本次目标、资源影响和验收方法。
2. 先确认团队当前集成基线（本轮为`jxq/m0-clock-review`，勿假定master已含最新实验），再创建职责明确的分支，使用`jxq/<topic>`命名；成员可由团队另行约定自己的前缀。
3. 每次推进一个独立可测试增量，提交后发起PR，并写明编译、烧录和实物验证分别进行到哪一步。
4. `.ioc`、主PLL、TIM2、DMA、NVIC等共享资源变更先沟通，再通过CubeMX生成；不手改生成硬件配置。
5. 用Issue记录问题、PR讨论代码、`Docs/`保存可复核结论。尚未验收的硬件快照保持草稿PR状态。

完整约定见[协作指南](CONTRIBUTING.md)。两板真实UID与A/B身份已登记在当前状态和身份模块中；团队成员的模块负责人尚未填写，不预设人员分工。

## 第三方代码

`Drivers/`中的HAL/CMSIS代码及其他ST生成内容遵循各文件和目录中的许可说明。仓库尚未为自有代码指定统一开源许可证；协作共享不等于重新授权第三方代码。
