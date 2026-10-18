# 当前项目状态

更新：2026-10-18。当前板M0.1系统基线、M0.2串口发送已有通过记录。M1.1 PHY识别及DOWN→UP→DOWN→UP观察通过，最后一段98条心跳连续、TX_ERRORS=0；实际PHY地址0，100 Mbps全双工。用户确认前面重复BOOT来自手动复位，并允许ETH/I2C3生成快照原样提交；I2C通信未验收。MCU接收未实现，当前不检查第二块板。

## 1. 已确认结果

| 项目 | 当前结论 | 证据 |
| --- | --- | --- |
| 工程 | SBCG；STM32F746NGH6 / TFBGA216；Flash 1024 KiB | `.ioc`、链接脚本与生成代码 |
| 工具约定 | CubeMX 6.18.1、CubeF7 V1.17.4、GCC/CMake/Ninja、VS Code | 当前工程元数据与构建配置 |
| 构建 | Debug构建通过，用户确认Build界面正常；`$gcc`引用问题已解决 | 用户反馈、独立内联匹配器及构建日志 |
| 烧录 | 用户提供Programming Finished、Verified OK及Resetting Target日志 | 用户实物操作反馈 |
| M0.1固件匹配 | 旧系统基线ELF的两个加载段共5500字节与Flash一致；不是本次新增UART的ELF | [单板启动报告](Milestones/M0_1_Startup.md) |
| M0.1启动运行 | SystemCoreClock=200000000；进入主循环；uwTick=0→1002→3005→8006；再次复位正常 | 同上；各快照CFSR/HFSR为0 |
| M0.2串口配置 | 当前工作区已生成USART1：PA9/TX、PB7/RX、115200 8N1、PCLK2；无UART IRQ/DMA；启动信息/每秒心跳已接入，Debug构建通过 | [M0.2实测报告](Milestones/M0_2_Serial.md)：BOOT及21条心跳接收正常，TX_ERRORS全为0；MCU接收未实现 |
| 当前MCU UID | w0/w1/w2=`001A002D / 30365105 / 36363631` | 只读目标UID寄存器，尚未指定A/B |
| 主机工具 | doctor/build/symbolize已实现；10项行为测试有通过记录 | [工作流](Development_Workflow.md) |
| M1.1 PHY及链路 | PHY_ADDR=0，ID=0007/C131，自协商开启；DOWN→UP→DOWN→UP，两次UP为100 Mbps全双工；最后一段SEQ=1～98，TX_ERRORS=0 | [用户日志验收](Milestones/M1_1_Ethernet_Setup.md#10-用户实物日志验收结果)；不覆盖UDP/PTP |

M0.1实物观察未重新写Flash、擦除或改选项字节，结束时已恢复运行。该结论只覆盖当时基线固件在当前板的调试复位与短时运行，不覆盖新增UART版本、冷启动、第二板或独立测频。

## 2. 当前真实硬件配置

| 资源 | 已生成/实测状态 |
| --- | --- |
| SYS | Trace Asynchronous Sw，PA13/PA14/PB3保留；SysTick时基 |
| RCC | HSE Bypass；PLLM25/N400/P2；AHB /1、APB1 /4、APB2 /2 |
| 频率 | 按工程声明25 MHz输入及实测寄存器计算：HCLK200 MHz、PCLK1 50 MHz、PCLK2 100 MHz |
| 电源/Flash | Scale1、OverDrive、FLASH_LATENCY_6 |
| PLLQ | 实际/2，Q输出200 MHz；USB/RNG/SDMMC未启用 |
| GPIO / USART1 | 现有A/B/H端口时钟；新增PA9/PB7的AF7，USART1初始化已接入main；PA15仍未分配 |
| Cache/DMA | 原基线实测I/D Cache使能位为0，当前保持未启用；已生成ETH描述符但未启动帧DMA或增加缓存维护 |

**当前生成状态**：用户新增ETH/RMII及I2C3（PH7/PH8）初始化，ETH链接布局已通过Debug/Release检查；用户回传新固件PHY及链路日志，启动已越过两项初始化，但I2C器件通信未验证。FMC/SDRAM、LTDC、LwIP/PTP、TIM2、SAI采集DMA及DHT11仍未启用。USART1已接入启动信息和每秒心跳应用；未实现接收、回显或命令解析。

M0.2已验收启动/心跳应用的旧ELF SHA256为`f77768c038b406b54530ac0dd2a45392ae0595249a44020351911c043bcf0352`；助手本轮未烧录；用户回传本版本BOOT及21条心跳，SEQ连续、tick相邻差均为1000 ms，发送验收通过。此前网页打开失败不再是当前阻塞，其原因未追溯确定。M0.1旧ELF的Flash比对结论仍只覆盖旧版本，本次没有对新ELF再次读取Flash比对。所需用户生成UART配置、驱动及应用作为同一已验证增量提交。实施与构建证据见[M0.2记录](Milestones/M0_2_Serial.md)。

## 3. 当前范围与后续

- 本机当前只推进这块板，不把检查第二板作为下一步的前置任务。最终课程双板联调由后续协作阶段落实，不能写成已经通过。
- 用户Build界面复验已完成；不再保留“等待确认匹配器提示消除”的旧待办。
- M1.1当前板PHY及链路观察已有[实物日志](Milestones/M1_1_Ethernet_Setup.md#10-用户实物日志验收结果)。Tx/Rx实际布局为0x2004C000/0x2004C0A0，栈顶0x2004C000，独立链接修复已提交为`23db576`。LAN8742识别及250 ms轮询已通过8项PHY/监视器、5项应用测试和Debug/Release构建/布局检查；助手未烧录，用户日志对应SBCG-M1.1。用户确认开头为手动复位，并允许ETH/I2C3共同生成快照原样随应用提交。本增量在此收尾，不自动开始UDP；I2C通信和UART接收命令仍未验证/实现。
- M1.1功能已提交为`3807edd`并同步远程。用户继续下一步，当前只推进**M1.2.0：无RTOS LwIP的CubeMX准备**，目标为后续当前板→电脑的单向UDP；配置单见[M1.2准备](Milestones/M1_2_UDP_Setup.md)。LwIP尚未生成、UDP未实现，等待用户经CubeMX启用LwIP并将ETH RxBuffLen改为1536；生成后先审查内存保留、重复ETH定义及主循环调用，不直接烧录。电脑有线IPv4检查时为169.254.212.178/16，后续测试目标地址192.168.10.100，本轮未改网络设置。
- 第二板UID、A/B指定、两板共同固件和PCB修订留待相应联调阶段；冷启动与独立测频未执行。
- PTP、声音采集/检测、温度服务、测距、LCD及同步波形均无实现或性能验收记录。

## 4. 分支与文档

截至本次整理，`master`已合并PR #1的200 MHz生成快照；`jxq/m0-clock-review`继续保存连接/构建修复与单板实测记录。分支名和PR状态不代替实验结论；协作前查看实际Git状态。

[文档入口](README.md) · [任务书路线](Project_Roadmap.md) · [开发工作流](Development_Workflow.md) · [CubeMX系统基线](CubeMX_Step1.md) · [硬件资源参考](Hardware_Resources.md)
