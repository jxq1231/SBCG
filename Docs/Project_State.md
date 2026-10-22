# 当前项目状态

<!-- doc-status: current-state; reviewed: 2026-10-23 -->

更新：2026-10-23。本文仅描述现在；历史实验按需查[文档状态表](Document_Status.md)及`Milestones/`，不重复执行旧计划。

## 当前工作

- **M3.2.4板端时间估算接入已完成并通过本板实物验收。** [结果与限制](Milestones/M3_2_4_Audio_Time_Map_Board.md)：120.190秒，120个有效锚点/位置、40组MAP/CHECK全部与真实C及Fraction一致，最大绝对留出残差14.884us。原音频、温度和三路UDP同时通过。
- 当前已烧录并验收版本 **SBCG-M3.2-MAP**；Debug SHA256 `9921f102cc605a63d162655474bf98732f132a7be901f3372787155f619f20ad`。Release已构建但未烧录，SHA256 `99566cbc92a16c935e2ece7890c0ec5b696330bf95ea4b55d3c3045bce726253`。
- 本轮只做三锚点前台自检，仍LOCAL/SYNC=0，尚无声音事件查询API或声学延迟校准。最近纯算法前置增量M3.2.3见[归档记录](Milestones/M3_2_3_Audio_Time_Map.md)，其“未接入/未烧录”仅是当时边界。
- 用户已批准该SHA单次program/verify/reset；串口和UDP采集现已关闭，未做发声、拔插或第二板实验。下一增量尚未启动。
- 继续使用现有分支`jxq/m0-clock-review`；用户改动受保护，生成初始化和.ioc不手改。

## 当前已生成并使用的资源

| 资源 | 当前配置及边界 |
| --- | --- |
| 板/MCU | STM32F746G-DISCO，STM32F746NGH6/TFBGA216；仅当前板，未分配正式A/B角色 |
| UID | `001A002D:30365105:36363631`；第二板UID/角色待联调 |
| 工具 | CubeMX6.18.1、CubeF7 V1.17.4、ARM GCC/CMake/Ninja、VS Code |
| SYS/RCC | Trace Asynchronous Sw；HSE Bypass25MHz，M25/N400/P2，HCLK200MHz，PCLK1 50MHz/PCLK2 100MHz；PLLQ/2，不提供48MHz域 |
| USART1 | PA9 TX/PB7 RX，115200 8N1无流控；RX IRQ5/0，无UART DMA；本机ST-LINK VCP COM3 |
| 网络 | ETH/RMII、LAN8742地址0/ID0007:C131，100Mbps全双工；NO_SYS LwIP2.1.2轮询，无ETH IRQ；ETH有专用内部DMA |
| 地址/端口 | 板192.168.10.1/24，PC物理以太网192.168.10.100/24；板5005→PC5005，PC6006↔板5006，PC6007↔板5007 |
| PTP | 原BSP本地时钟与RX/TX硬件戳、板端四时刻估算观测已实测；仍LOCAL/SYNC=0，无校时/校频/PPS |
| 温度 | DHT11模块3.3V，DATA为PG7/D4、开漏释放；5秒采集、预读/超龄机制；AUTO/手动及串口命令已实测，无温度UDP共享 |
| I2C3 | PH7 SCL/PH8 SDA；WM8994地址0x1A，ID8994已实测；触控业务未实现 |
| SAI2 | A主接收提供时钟，B内部同步从接收；16位双通道，4时隙中1/3有效；标称48kHz，推导约47991.071Hz，PLLI2S344/Q7/DivQ1 |
| 音频DMA/IRQ | DMA2 Stream7 Channel0，P→M halfword circular，32768元素/64KiB；DMA2_Stream7和SAI2 IRQ均4/0；半满/满每8192帧 |
| 内存/Cache | 音频SRAM1 0x20010000..0x20020000、32字节对齐；主栈0x20047000..0x20048000，LwIP堆窗0x20048000..0x2004C000；ETH描述符/RX池在SRAM2；I/D Cache关闭 |
| 尚未启用 | TIM2/PPS、FMC/外部SDRAM、LTDC/DMA2D显示、双板角色/同步/温度共享；声音检测与测距尚未实现 |

生成与实测的完整资源边界见[Hardware_Resources](Hardware_Resources.md)。任何引脚、时钟、DMA、IRQ变更仍由CubeMX负责；当前板端软件接入任务 **No CubeMX change required**。

## 最近一次实物证据与已知限制

[M3.2.4完整记录](Milestones/M3_2_4_Audio_Time_Map_Board.md)：单次授权烧录后120.190秒，120个有效锚点、拒绝0、WIDTH0/1；40组板端估算与真实C/Fraction逐值相同，残差范围−14.884～+13.200us。120对原音频位置连续；110包UDP零缺口、两组30请求及原板端估算对照通过；23个自动温度版本、60次codec ID读取无错。

codec最大轮询间隔2068ms、心跳最大1015ms保留，不宣称精密前台周期。复位前14行及新BOOT前63字节残片仅作边界排除，残片含0xff；raw二进制与JSON投影逐行一致，新BOOT后严格ASCII且无重启。原始证据未改写。

NDTR进度不是SRAM读取授权；WIDTH=0仍有一帧量化；codec/SAI/FIFO/DMA到物理声音到达的延迟未校准。留出残差及模型区间不能证明声学精度、绝对频率或双板同步。

此前温度[连续拔插恢复](Milestones/T1_1_Temperature_Replug.md)、[手动设温](Milestones/T1_2_Manual_Temperature.md)、音频[受控发声](Milestones/M3_1_Audio_DMA.md)、[5分钟稳定性](Milestones/M3_1_Quiet_Stability.md)和[温度联动](Milestones/M3_1_Temperature_Coexistence.md)各有分版证据；不升级成当前版本已重复执行。

## 后续与验收分层

M3.2.4通过10组原生模块（Audio10程序）、Tools83/Analysis87项及两配置构建/布局，并完成上述Debug实物验收。声学延迟校准、事件查询/检测、双板同步/PPS及LCD仍需独立规划；完整课程指标见[路线图](Project_Roadmap.md)，不因暂不检查第二板而删除最终双板要求。
