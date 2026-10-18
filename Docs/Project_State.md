# SBCG项目状态

更新时间：2026-10-18。当前里程碑：**M0.1——当前单板调试复位启动通过，两板共同基线仍待验收**。证据见[单板启动报告](Milestones/M0_1_Startup.md)。

## 工程与分支

| 项目 | 状态 |
| --- | --- |
| 仓库 | https://github.com/jxq1231/SBCG |
| 主工程 | SBCG，STM32F746NGH6 / TFBGA216，Flash 1024 KiB |
| 工具版本 | CubeMX 6.18.1，CubeF7 V1.17.4，GCC/CMake/Ninja，VS Code |
| master | 已通过PR #1合并200 MHz/HSE Bypass生成快照（7117489）；合并不代表实物启动验收 |
| jxq/m0-clock-review | 200 MHz/HSE Bypass基线及连接/构建诊断修复；当前单板启动结果已记录，不代表两板或测距功能通过 |
| 双板共用工程 | 仍待落实；另一侧SBCJ版本信息来自此前审查，尚未核对其完整工程 |

200 MHz生成快照已通过PR #1合并。用户此前提供烧录校验成功日志；2026-10-18又以只读Flash比对确认板上加载段与当前ELF一致，并完成当前单板的调试复位启动检查。该证据不覆盖第二块板、冷启动或独立测频。

## 已完成并有证据的工作

- 已核对NG器件、封装和1024 KiB链接设置。
- 已记录板级资源、全RGB565、ETH/PTP唯一时间、UID角色及共享整数温度/测距约定。
- 已实现主机环境检查、配置/构建报告和ELF地址定位，并补齐VS Code任务。
- 10项Python行为测试、本机Debug/Release构建、clangd检查及main地址定位已通过；这些结果见`Development_Workflow.md`。
- 生成代码、.ioc和当前板卡寄存器一致：HSE Bypass、M25/N400/P2、AHB /1、APB1 /4、APB2 /2；按工程25 MHz输入计算HCLK200 MHz、PCLK1 50 MHz、PCLK2 100 MHz。
- 当前板卡已进入主循环，SystemCoreClock=200000000，分段运行uwTick=0→1002→3005→8006；再次复位后进入主循环，tick=1131。各次快照CFSR/HFSR为0，结束时恢复运行。

## 当前基线的实际资源

| 资源 | 当前配置/用途 |
| --- | --- |
| SYS | Trace Asynchronous Sw；PA13/SWDIO、PA14/SWCLK、PB3/SWO；SysTick时基 |
| RCC | HSE Bypass 25 MHz，HCLK200 MHz；Scale1、OverDrive、FLASH_LATENCY_6 |
| PLLQ | 实际/2，Q输出200 MHz；USB/RNG/SDMMC未启用，启用前重新规划 |
| PA15 | 尚未分配，后续PPS预留 |
| FMC/LTDC/ETH/TIM2/SAI/USART1/DHT11 | 仍未启用；文档中的相关映射是后续目标 |
| Cache/DMA | 本增量未增加共享缓冲或维护策略 |

## 尚缺的证据与下一步

1. 当前单板的时钟返回、主循环、HAL时基和复位复验已通过；第二块板仍需独立做相同检查，不推广本板结果。
2. 断电冷启动和独立测频未执行；软件时钟值已结合RCC寄存器核对，不作为绝对频率精度证据。
3. 已读当前MCU UIDw0/w1/w2=`001A002D / 30365105 / 36363631`；第二块板UID、两板PCB修订及A/B指定仍待补录。
4. 当前ELF SHA256及只读Flash比对、实物观察已归档至单板启动报告；VS Code Build界面的匹配器提示消除仍需用户确认。
5. 停在当前增量；后续FMC/SDRAM、显示或网络按团队确认的下一项独立目标开展。

当前未实现PTP、声音采集、声音检测、温度UDP或测距算法。已有用户烧录校验成功及本轮单板短时启动运行记录；尚无双板启动、PPS精度或最终测距指标的实测记录。

## 已确认的调试连接问题

2026-10-16已在用户授权下复现OpenOCD的CPUID=0x1a识别失败。单独降速仍失败，采用硬件复位下连接后恢复正常，读取CPUID=0x410fc271、DBGMCU_ID=0x10016449；1800 kHz下亦通过。Flash、Run和F5现共用`.vscode/openocd-stm32f746.cfg`，详细证据见`Development_Workflow.md`第8节。

此前连接诊断仅复位/暂停、读寄存器并恢复运行，没有写Flash、擦除或改选项字节。用户随后提供`Programming Finished`、`Verified OK`及`Resetting Target`日志，确认该次烧录校验成功。2026-10-18的单板启动结论来自独立运行观察，不是仅从烧录日志推断。

## 构建任务匹配器修复

2026-10-16用户报告`$gcc`匹配器引用无效。三个构建任务已改为独立内联GCC匹配器，捕获源码位置和warning/error，不再要求C/C++扩展注册命名匹配器。细节和VS Code界面复验步骤见`Development_Workflow.md`第9节。本次不修改CubeMX配置、固件、IRQ、DMA或缓存，不连接硬件。

## 构建、烧录与观察入口

- 构建：`python -B Tools/Monitor/autodebug.py build`，复用现有CMake预设。
- 主机测试：`python -B -m unittest discover -s Tools/Monitor/tests -v`。
- 烧录/调试：使用既有STM32: Flash和STM32F7: Debug，按授权与实物连接执行；本次仓库发布没有进行这些动作。
- 原始日志：`Logs/`，由Git忽略；实验结论保留在Docs或关联Issue/PR中。
- 单板实物证据：[M0_1_Startup.md](Milestones/M0_1_Startup.md)，本轮没有重复烧录。
- 硬件分配：[Hardware_Resources.md](Hardware_Resources.md)；启动验收：[CubeMX_Step1.md](CubeMX_Step1.md)第2.6节。
