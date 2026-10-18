# 当前项目状态

更新：2026-10-18。当前板M0.1系统基线通过调试复位启动验收。用户已确认VS Code Build正常，并要求当前不检查第二块板。整机测距功能尚未实现。

## 1. 已确认结果

| 项目 | 当前结论 | 证据 |
| --- | --- | --- |
| 工程 | SBCG；STM32F746NGH6 / TFBGA216；Flash 1024 KiB | `.ioc`、链接脚本与生成代码 |
| 工具约定 | CubeMX 6.18.1、CubeF7 V1.17.4、GCC/CMake/Ninja、VS Code | 当前工程元数据与构建配置 |
| 构建 | Debug构建通过，用户确认Build界面正常；`$gcc`引用问题已解决 | 用户反馈、独立内联匹配器及构建日志 |
| 烧录 | 用户提供Programming Finished、Verified OK及Resetting Target日志 | 用户实物操作反馈 |
| 固件匹配 | 本轮只读Flash比对，两个加载段共5500字节与当前ELF一致 | [单板启动报告](Milestones/M0_1_Startup.md) |
| 启动运行 | SystemCoreClock=200000000；进入主循环；uwTick=0→1002→3005→8006；再次复位正常 | 同上；各快照CFSR/HFSR为0 |
| 当前MCU UID | w0/w1/w2=`001A002D / 30365105 / 36363631` | 只读目标UID寄存器，尚未指定A/B |
| 主机工具 | doctor/build/symbolize已实现；10项行为测试有通过记录 | [工作流](Development_Workflow.md) |

本次实物观察未重新写Flash、擦除或改选项字节，结束时已恢复运行。该结论只覆盖当前板的调试复位与短时运行，不覆盖冷启动、第二板或独立测频。

## 2. 当前真实硬件配置

| 资源 | 已生成/实测状态 |
| --- | --- |
| SYS | Trace Asynchronous Sw，PA13/PA14/PB3保留；SysTick时基 |
| RCC | HSE Bypass；PLLM25/N400/P2；AHB /1、APB1 /4、APB2 /2 |
| 频率 | 按工程声明25 MHz输入及实测寄存器计算：HCLK200 MHz、PCLK1 50 MHz、PCLK2 100 MHz |
| 电源/Flash | Scale1、OverDrive、FLASH_LATENCY_6 |
| PLLQ | 实际/2，Q输出200 MHz；USB/RNG/SDMMC未启用 |
| GPIO | 仅开启GPIOA/B/H时钟；PA15尚未分配，保留后续PPS用途 |
| Cache/DMA | 实测I/D Cache使能位为0；无应用DMA缓冲和缓存维护策略 |

**尚未启用**：USART1、FMC/SDRAM、LTDC、I2C3、ETH/LwIP/PTP、TIM2、SAI及其采集DMA、DHT11。当前主循环为空；资源文档的引脚表是后续目标，不是已生效配置。

## 3. 当前范围与后续

- 本机当前只推进这块板，不把检查第二板作为下一步的前置任务。最终课程双板联调由后续协作阶段落实，不能写成已经通过。
- 用户Build界面复验已完成；不再保留“等待确认匹配器提示消除”的旧待办。
- 下一步建议为M0.2 USART1观测入口；具体顺序、基础60分和提高项目见[完整路线](Project_Roadmap.md)。该增量尚未配置或实现。
- 第二板UID、A/B指定、两板共同固件和PCB修订留待相应联调阶段；冷启动与独立测频未执行。
- PTP、声音采集/检测、温度服务、测距、LCD及同步波形均无实现或性能验收记录。

## 4. 分支与文档

截至本次整理，`master`已合并PR #1的200 MHz生成快照；`jxq/m0-clock-review`继续保存连接/构建修复与单板实测记录。分支名和PR状态不代替实验结论；协作前查看实际Git状态。

[文档入口](README.md) · [任务书路线](Project_Roadmap.md) · [开发工作流](Development_Workflow.md) · [CubeMX系统基线](CubeMX_Step1.md) · [硬件资源参考](Hardware_Resources.md)
