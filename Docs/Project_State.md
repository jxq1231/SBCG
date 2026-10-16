# SBCG项目状态

更新时间：2026-10-16。当前里程碑：**M0.1——系统时钟基线，实物启动待验收**。

## 工程与分支

| 项目 | 状态 |
| --- | --- |
| 仓库 | https://github.com/jxq1231/SBCG |
| 主工程 | SBCG，STM32F746NGH6 / TFBGA216，Flash 1024 KiB |
| 工具版本 | CubeMX 6.18.1，CubeF7 V1.17.4，GCC/CMake/Ninja，VS Code |
| master | 共享文档与主机工作流；固件仍是原216 MHz/HSE_ON生成基线，未取得实物验收记录 |
| jxq/m0-clock-review | 本次共享的200 MHz/HSE Bypass生成快照，仅供审阅和上板验收，不代表功能完成 |
| 双板共用工程 | 仍待落实；另一侧SBCJ版本信息来自此前审查，尚未核对其完整工程 |

原有`CubeMX_Step1.md`及`Hardware_Resources.md`中的“当前200 MHz”指待验收生成快照；在master阅读时应同时查看本表。其“未作为已验证功能提交”仍成立；上传审阅快照不替代硬件验收。

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

1. 用户执行或明确授权烧录/调试后，验证200 MHz快照能越过SystemClock_Config()并进入主循环。
2. 记录SystemCoreClock=200000000、uwTick持续增加及无错误/异常；软件时钟值需结合RCC配置核对，不代替独立测频。
3. 分别记录两板真实96位UID及A/B指定；不要使用ST-LINK USB序列号代替MCU UID。
4. 补齐板卡修订、实际工具版本、固件提交/ELF校验值和观察结果，审核通过后再讨论合并时钟快照。
5. 停在当前增量；后续FMC/SDRAM、显示或网络按团队确认的下一项独立目标开展。

当前未实现PTP、声音采集、声音检测、温度UDP或测距算法。尚无烧录/运行通过、PPS精度或最终测距指标的实测记录。

## 构建、烧录与观察入口

- 构建：`python -B Tools/Monitor/autodebug.py build`，复用现有CMake预设。
- 主机测试：`python -B -m unittest discover -s Tools/Monitor/tests -v`。
- 烧录/调试：使用既有STM32: Flash和STM32F7: Debug，按授权与实物连接执行；本次仓库发布没有进行这些动作。
- 原始日志：`Logs/`，由Git忽略；实验结论保留在Docs或关联Issue/PR中。
- 硬件分配：[Hardware_Resources.md](Hardware_Resources.md)；启动验收：[CubeMX_Step1.md](CubeMX_Step1.md)第2.6节。
