# 当前项目状态

当前状态（2026-10-20）：**M2.1.1单板MAC本地PTP时钟已通过实物验收**，固件SBCG-M2.1-CLOCK。82项主机检查、Debug/Release构建/布局及实际Generate Code保留性回归通过；经用户明确授权完成烧录、校验和复位。新BOOT后88条PTP及心跳连续，70个UDP包零缺口、30次请求全部匹配应答，错误计数均0。仅本地coarse计时，SYNC=0；不表示两板同步或绝对精度达标，见[M2.1.1证据与复验](Milestones/M2_1_PTP_Clock.md)。下述M1.3/M1.4为各自旧固件的历史实测。

更新：2026-10-20。**M1.3当前板与电脑UDP请求/应答已通过实物验收**：电脑6006→板端5006完成30次匹配往返，超时及错误计数均0；串口最终VALID=30、ACK_OK=30，34条心跳连续。Debug/Release构建、布局及71项主机检查通过。实现使用SBCG-M1.3-UDP标识，详情及本次未附BOOT/烧录记录的边界见[M1.3验收文档](Milestones/M1_3_UDP_Request_Reply.md#用户实物验收2026-10-20)。以下M1.2实测仍对应旧版本，不自动延伸为新版本通过。

**最新M1.4实物复验**：在同一`671be76`基线上自动运行两路UDP和COM3采集。观察到两次PHY DOWN/UP，60个请求中41个匹配应答、19次超时集中在两段断线窗口；5005窗口90秒收到71包、19个缺口与串口LINK_DOWN及LINK_SKIPS净增19完全对应；90条心跳连续且TX_ERRORS=0。两路均自动恢复，工具原summary保持REVIEW。见[M1.4联合验收](Milestones/M1_4_UDP_Reconnect.md)。

既有实测（2026-10-19）：SBCG-M1.2-UDP已通过31包正常窗口及后续短时拔插恢复观察。replug-02为另外两次拔插，23个实收包、14个缺口与串口LINK_DOWN完全对应，PHY两次DOWN/UP；心跳662～694连续，TX_ERRORS=0，恢复后序号/tick延续。该窗口在37.737秒手动Ctrl+C结束。该版本的Debug/Release、布局及60项主机检查已有通过记录。当时尚无请求/应答服务；I2C通信未验收，MCU串口接收未实现；不检查第二块板。

**M1.4历史验收增量**：电脑与本板的请求/应答和5005发送在两次链路恢复后均继续，断线不阻止这90条串口心跳；这是单板与电脑的短时恢复测试，不是两板通信。M1.3正常窗口30发30收与本次故障注入窗口分别统计，此前M1.2及INT版ping同样作为历史证据保留。

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
| M1.2 LwIP集成 | SBCG-M1.2-INT，200 MHz、UID匹配；DOWN→UP至100 Mbps全双工；SEQ=1～12，相邻tick差1000 ms，TX_ERRORS=0；ping 4发4收、0%丢失 | [2026-10-19用户实物验收](Milestones/M1_2_UDP_Setup.md#8-用户实物验收2026-10-19)；ARP/ICMP基本通信通过，UDP未实现 |
| M1.2 单向UDP | SBCG-M1.2-UDP，version=1、28字节；来源192.168.10.1:5005，31包SEQ=2～32，缺口/重复/乱序/非法包均0；30个tick间隔1000 ms | [用户串口及电脑日志](Milestones/M1_2_UDP_Send.md#用户实物验收2026-10-19) |
| 两次拔插后的UDP恢复 | 用户确认两次拔插；414～420→427～443→448～452，实收29包；缺口421～426和444～447共10个，重复/乱序均0；恢复后序号/tick延续 | [恢复观察](Milestones/M1_2_UDP_Send.md#两次拔插恢复观察2026-10-19)；38.809秒手动结束，非完整60秒，未附本窗口串口心跳/PHY日志 |
| 第二组拔插联合验收 | 另两次拔插，电脑657～667→676～681→688～693共23包；缺口668～675和682～687共14个，与LINK_SKIPS净增14一致；PHY DOWN/UP、心跳662～694连续、TX_ERRORS=0 | [串口与电脑联合记录](Milestones/M1_2_UDP_Send.md#第二组拔插与串口联合验收2026-10-19)；37.737秒手动结束，正常窗口和故障注入窗口分别统计 |
| M1.3 请求/应答 | 电脑30发30收，SEQ=1～30；来源、UID、请求标记匹配，超时及错误项均0；RTT最小/平均/最大0.395/0.611/2.115 ms | [用户电脑与串口联合验收](Milestones/M1_3_UDP_Request_Reply.md#用户实物验收2026-10-20)；板端VALID/ACK_OK最终30，心跳70～103连续；RTT仅为电脑软件往返时间 |
| M1.4 双向UDP恢复 | 两次PHY DOWN/UP后两路均恢复；请求应答41/60，19次超时对应两段断线；5005实收71包，缺口19与LINK_SKIPS净增19一致；90条心跳连续、TX_ERRORS=0 | [自动采集与联合核对](Milestones/M1_4_UDP_Reconnect.md)；故障窗口summary保留REVIEW，不当作正常丢包率 |
| M2.1.1 本地PTP时钟 | 88条LOCAL/SYNC=0单调时间及心跳；70个UDP包零缺口，30/30请求应答，错误计数0 | [本版本自动验收](Milestones/M2_1_PTP_Clock.md)；已烧录校验，仅本地未同步时钟 |

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
| Cache/DMA | 原基线实测I/D Cache使能位为0，当前代码保持未启用；LwIP生成接口在链路UP时启动ETH内部帧DMA，已分别通过基本ping、逐秒单向UDP和请求/应答；未新增IRQ或DMA1/2流，未做吞吐/压力测试 |

**当前生成状态**：ETH/RMII、I2C3（PH7/PH8）及无RTOS LwIP 2.1.2已生成，静态IP为192.168.10.1/24；MEM_SIZE=4096、Rx池4×1536。代码侧已修复重复编译、段名、固定堆隔离、主循环处理及PHY观察状态。当前MSP顶端0x20048000，LwIP堆保留窗0x20048000～0x2004BFFF，描述符和接收池位于SRAM2；Debug/Release布局检查、本板基本ARP/ICMP、旧版逐秒单向UDP及M1.3请求/应答分别有通过记录。PTP本地coarse时钟已由用户BSP启动并实测，专用ETHMACPTP门控开启；FMC/SDRAM、LTDC、TIM2、SAI采集DMA及DHT11仍未启用；MCU串口接收/回显未实现。

M0.2已验收启动/心跳应用的旧ELF SHA256为`f77768c038b406b54530ac0dd2a45392ae0595249a44020351911c043bcf0352`；助手本轮未烧录；用户回传本版本BOOT及21条心跳，SEQ连续、tick相邻差均为1000 ms，发送验收通过。此前网页打开失败不再是当前阻塞，其原因未追溯确定。M0.1旧ELF的Flash比对结论仍只覆盖旧版本，本次没有对新ELF再次读取Flash比对。所需用户生成UART配置、驱动及应用作为同一已验证增量提交。实施与构建证据见[M0.2记录](Milestones/M0_2_Serial.md)。

## 3. 当前范围与后续

- 下一增量提案为[M2.1.2接收硬件时间戳](Plans/2026-10-20_PTP_RX_Design.md)：先关联电脑请求与DMA接收时间，再单独处理TX。该方案待用户复核PTP接收配置范围；尚未实施或烧录，当前实物通过范围仍为上方M2.1.1。

- M1.3正常请求/应答及M1.4两次链路恢复已验收；用户随后要求按步骤进入M2，统一PTP宏已提交。第四步本地PTP时钟现已实物验收，PTP专用HAL配置集中在用户BSP；CubeMX再生成只刷新.ioc函数清单元数据，生成C源码不变。下一独立目标是评估网络收发硬件事件时间戳，需另行限定范围。长时间压力、非法包实物注入及fatal DMA恢复未执行。

- 本机当前只推进这块板，不把检查第二板作为下一步的前置任务。最终课程双板联调由后续协作阶段落实，不能写成已经通过。
- 用户Build界面复验已完成；不再保留“等待确认匹配器提示消除”的旧待办。
- M1.1历史记录：当前板PHY及链路观察已有[实物日志](Milestones/M1_1_Ethernet_Setup.md#10-用户实物日志验收结果)。当时Tx/Rx布局为0x2004C000/0x2004C0A0，栈顶0x2004C000，独立链接修复提交为`23db576`；后续LwIP集成已将当前栈顶降至0x20048000。LAN8742识别及250 ms轮询通过当时8项PHY/监视器、5项应用测试和Debug/Release构建/布局检查；助手未烧录，用户日志对应SBCG-M1.1。用户确认开头为手动复位，并允许ETH/I2C3共同生成快照原样随应用提交；I2C通信和UART接收命令仍未验证/实现。
- M1.1功能提交为`3807edd`，M1.2.0配置单提交为`e830100`，LwIP集成修复提交为`63aef9b`。单板逐秒UDP根据[31包记录](Milestones/M1_2_UDP_Send.md#用户实物验收2026-10-19)验收，功能提交为`577abbe`。replug-01电脑端恢复记录提交为`d7b6adc`，replug-02[串口/电脑联合验收](Milestones/M1_2_UDP_Send.md#第二组拔插与串口联合验收2026-10-19)提交为`d09b3c3`；这些记录对应M1.2。M1.3本次另行记录实测结果，助手未烧录或修改电脑网络。
- 第二板UID、A/B指定、两板共同固件和PCB修订留待相应联调阶段；冷启动与独立测频未执行。
- 本地PTP时钟已有本版本88条连续实测；PTP收发硬件时间戳、同步协议/伺服、声音采集/检测、温度服务、测距、LCD及同步波形仍无实现或性能验收记录。

## 4. 分支与文档

截至本次整理，`master`已合并PR #1的200 MHz生成快照；`jxq/m0-clock-review`继续保存连接/构建修复与单板实测记录。分支名和PR状态不代替实验结论；协作前查看实际Git状态。

[文档入口](README.md) · [任务书路线](Project_Roadmap.md) · [开发工作流](Development_Workflow.md) · [CubeMX系统基线](CubeMX_Step1.md) · [硬件资源参考](Hardware_Resources.md)
