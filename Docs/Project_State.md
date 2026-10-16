# SBCG项目状态

更新时间：2026-10-16。当前里程碑：**M0.1——系统时钟基线，实物启动待验收**。

## 工程与分支

| 项目 | 状态 |
| --- | --- |
| 仓库 | https://github.com/jxq1231/SBCG |
| 主工程 | SBCG，STM32F746NGH6 / TFBGA216，Flash 1024 KiB |
| 工具版本 | CubeMX 6.18.1，CubeF7 V1.17.4，GCC/CMake/Ninja，VS Code |
| master | 已通过PR #1合并200 MHz/HSE Bypass生成快照（7117489）；合并不代表实物启动验收 |
| jxq/m0-clock-review | 本次共享的200 MHz/HSE Bypass生成快照，仅供审阅和上板验收，不代表功能完成 |
| 双板共用工程 | 仍待落实；另一侧SBCJ版本信息来自此前审查，尚未核对其完整工程 |

原有`CubeMX_Step1.md`及`Hardware_Resources.md`中的“当前200 MHz”指待验收生成快照。其“未作为已验证功能提交”仍成立；快照已合并，用户已提供烧录校验成功日志，启动运行仍待验收。

## 已完成并有证据的工作

- 已核对NG器件、封装和1024 KiB链接设置。
- 已记录板级资源、全RGB565、ETH/PTP唯一时间、UID角色及共享整数温度/测距约定。
- 已实现主机环境检查、配置/构建报告和ELF地址定位，并补齐VS Code任务。
- 10项Python行为测试、本机Debug/Release构建、clangd检查及main地址定位已通过；这些结果见`Development_Workflow.md`。
- 待验收生成快照与其.ioc一致：HSE Bypass 25 MHz、M25/N400/P2、HCLK200 MHz、PCLK1 50 MHz、PCLK2 100 MHz。

## 待验收快照的实际资源

| 资源 | 当前配置/用途 |
| --- | --- |
| SYS | Trace Asynchronous Sw；PA13/SWDIO、PA14/SWCLK、PB3/SWO；SysTick时基 |
| RCC | HSE Bypass 25 MHz，HCLK200 MHz；Scale1、OverDrive、FLASH_LATENCY_6 |
| PLLQ | 实际/2，Q输出200 MHz；USB/RNG/SDMMC未启用，启用前重新规划 |
| PA15 | 尚未分配，后续PPS预留 |
| FMC/LTDC/ETH/TIM2/SAI/USART1/DHT11 | 仍未启用；文档中的相关映射是后续目标 |
| Cache/DMA | 本增量未增加共享缓冲或维护策略 |

## 尚缺的证据与下一步

1. 用户已提供烧录校验成功日志，下一步验证固件能越过SystemClock_Config()并进入主循环；本次匹配器修复不重复烧录。
2. 记录SystemCoreClock=200000000、uwTick持续增加及无错误/异常；软件时钟值需结合RCC配置核对，不代替独立测频。
3. 分别记录两板真实96位UID及A/B指定；不要使用ST-LINK USB序列号代替MCU UID。
4. 补齐板卡修订、实际工具版本、已烧录固件提交/ELF校验值和观察结果，记录M0.1启动验收结论。
5. 停在当前增量；后续FMC/SDRAM、显示或网络按团队确认的下一项独立目标开展。

当前未实现PTP、声音采集、声音检测、温度UDP或测距算法。已有用户提供的烧录校验通过日志；尚无启动运行通过、PPS精度或最终测距指标的实测记录。

## 已确认的调试连接问题

2026-10-16已在用户授权下复现OpenOCD的CPUID=0x1a识别失败。单独降速仍失败，采用硬件复位下连接后恢复正常，读取CPUID=0x410fc271、DBGMCU_ID=0x10016449；1800 kHz下亦通过。Flash、Run和F5现共用`.vscode/openocd-stm32f746.cfg`，详细证据见`Development_Workflow.md`第8节。

此前连接诊断仅复位/暂停、读寄存器并恢复运行，没有写Flash、擦除或改选项字节。用户随后提供`Programming Finished`、`Verified OK`及`Resetting Target`日志，确认该次烧录校验成功；不将其计为M0.1启动运行通过。

## 构建任务匹配器修复

2026-10-16用户报告`$gcc`匹配器引用无效。三个构建任务已改为独立内联GCC匹配器，捕获源码位置和warning/error，不再要求C/C++扩展注册命名匹配器。细节和VS Code界面复验步骤见`Development_Workflow.md`第9节。本次不修改CubeMX配置、固件、IRQ、DMA或缓存，不连接硬件。

## 构建、烧录与观察入口

- 构建：`python -B Tools/Monitor/autodebug.py build`，复用现有CMake预设。
- 主机测试：`python -B -m unittest discover -s Tools/Monitor/tests -v`。
- 烧录/调试：使用既有STM32: Flash和STM32F7: Debug，按授权与实物连接执行；本次仓库发布没有进行这些动作。
- 原始日志：`Logs/`，由Git忽略；实验结论保留在Docs或关联Issue/PR中。
- 硬件分配：[Hardware_Resources.md](Hardware_Resources.md)；启动验收：[CubeMX_Step1.md](CubeMX_Step1.md)第2.6节。
