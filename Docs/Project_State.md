# 当前项目状态

<!-- doc-status: current-state; reviewed: 2026-10-24 -->

更新：2026-10-24。本文仅描述现在；历史实验按需查[文档状态表](Document_Status.md)及`Milestones/`，不重复执行旧计划。

## 当前工作

**U2真实RGB565状态页已通过单A实机验收。** 当前板上`SBCG-U2-STATUS-R2`（Debug14fdabe9bec9…），CubeMX生成480×272/9.6MHz及延后初始化；8根颜色低位按用户决定暂缓。BOOT后约79.34秒，UI末77帧/错误0/丢弃0，音频461有效块、丢块/错误0；手动25/30°C分别显示346.450/349.480m/s并恢复AUTO；用户确认“状态页显示正常”。初版HAL查询误拒绝和R1刷新偏慢记录均保留，R2优化后页面启动间隔约1.023秒。见[U2验收](Milestones/U2_LTDC_Status_Page.md)，不再重复LTDC配置、色条或U1整片自检。当前仍无网线/B/示波器，不播放声音；温度模块未连接，AUTO显示无有效值。距离/方向/质量未实现，保持占位；TouchGFX后续另立目标。

**前一M4.1.3候选帧关联本地PTP传输模型及日志修复已通过静默验证。** 当时A板`SBCG-M4.1-TIME-R1`，Debug前缀789c0c92ef2f。BOOT后67.933秒，395有效块、66条音频有效/零丢块/零错误、心跳连续；环境自然候选10条，9模型与Fraction一致、1条NO_LEFT明确拒绝，22组原模型自检继续运行。原TIME版5次归还失败记录保留，R1日志改为有界排队每轮一行。没有主动发声，未宣称标准音频识别、物理到达或同步精度通过。[本轮结果](Milestones/M4_1_3_Sound_Event_Time.md)。

**U0/U1为已完成的历史前置。** U0纯C绘图准备见[软件报告](Milestones/U0_RGB565_Status_Page.md)；U1-SDRAM-R1已完成8MB四轮自检（50529ms）和静默共存，首版MPU失败、恢复及Region2修复见[内存报告](Milestones/U1_SDRAM_Bringup.md)。当前U2改用SDRAM显示READY，未重做破坏性测试，不将READY写成自检PASS。

## 暂缓验收：M2.3.3 PPS

**M2.3.3 PPS软件已接入；新检测固件中观察到本地排程运行，电气波形仍待验收。** 用户已通过CubeMX生成PA15/PPS_OUT、TIM2_CH1/AF1、Reset/ITR1/ETH PTP remap、PSC99/ARR最大/CCR100及Pull-down。A以本地PTP整秒目标产生参考100us正脉冲；B仅在资格及有界重新资格内输出。新增PPS不新增IRQ/DMA，不改变缓存或现有ETH/SAI内存布局。软件计数不是电气波形证据，详见[本轮交付](Milestones/M2_3_3_PTP_PPS.md)。

2026-10-23当前仅保留A板。此前“暂不烧录”已由本轮“板已连接可以测试”的明确授权更新；已烧录M4.1-CAND-R1，不是原冻结M2.3-PPS文件。PPS末计数EMITTED89/MISSED0/ERRORS0只是寄存器观测。输出为绿板Arduino **D9 / PA15 / CN7第2针**，地为GND；没有示波器，不能宣布脉宽或边沿精度通过。B恢复后仍需交换机冷启动≤30秒及收敛后连续10次±10us。

## 上一已验收基线：M2.3.2（历史证据）

- **M2.3.2已实现，并通过用户指定的1分钟运行窗口验证。** 同一固件`SBCG-M2.3-SYNC`按UID固定A参考/B跟随，319/320固定PTPv2子集、四硬件时刻及B有界fine调相/调频。用户报告已手动烧录两板；助手本次未重复烧录/复位。详见[实现与短时证据](Milestones/M2_3_2_Dual_Board_Clock_Software.md)。
- A=COM3、B=COM5，交换机仍为A2/B1/PC3口。B60秒236组原始四时刻与真实C/Fraction完全一致，协议估算时差−65～+340ns，漂移估计−61～+35ppb，8次成功调频，控制拒绝/错误0。CTRL35条内部锁、25条TRACKING，变频后重新建立锁证据；不是全程连续锁定。
- 后续60秒稳定网络A/B各60包、零缺口/重复/乱序；probe和旧time exchange各板各15次全通过，音频位置各60条连续、codec各30次成功；A温度正常，B本地温度按UID禁用。初始准备窗口B序号回退FAIL原样保留，原因未证实，不能合并成全绿实验。
- 本次用户明确将10分钟缩短为1分钟。各串口/网络窗口部分重叠，未捕获BOOT及冷启动相位步进，未读回Flash哈希；未测正向频率/正负大相位、断网恢复或外部同步精度，所有`SYNC=0`保持。内部估计不表示±10us课程独立验收或测距精度通过。
- 已有软件证据：15组原生运行器、9412断言伺服、12组协调层、Tools113/Analysis98、两种构建/布局及独立复核通过。冻结Debug前缀`8fea8c54cc27`，Release前缀`ca5b89440a5b`；用户手动烧录报告与观测行为相符，实物精确哈希未读回。
- 本次CubeMX仅用户再生成UDP PCB6，BSP fine扩展已明确批准。无引脚/RCC/DMA/NVIC/Cache改动，HAL原文件未改；旧配对/音频模型按时间代际失效，PCM DMA持续运行。
- 该版本之后实施了PPS软件及M4.1.2实验检测管线；均不表示UI或测距已完成。

## 最近一次文档与可读性维护

2026-10-23完成[系统架构](System_Architecture.md)、[答辩稿](Defense_Guide.md)及58个App C/H中文注释；156个函数定义、72个声明已覆盖，非注释token及两配置Flash镜像均与修改前一致，构建/布局通过。Debug调试信息因源码行号改变而有新ELF哈希，该注释维护当时板上仍是旧ELF、未进行新实物验收；之后用户已手动更新两板SBCG-M2.3-SYNC，并按当前工作所述完成短时运行观测。详见[维护记录](Architecture_Comment_Review.md)。

## 当前已生成并使用的资源

| 资源 | 当前配置及边界 |
| --- | --- |
| 板/MCU | STM32F746G-DISCO，STM32F746NGH6/TFBGA216；已登记A/B两块板；双板网络及短时内部闭环已通过 |
| UID | A `001A002D:30365105:36363631`；B `00380050:35385104:37313532`，A/B网络身份均已实测 |
| 工具 | CubeMX6.18.1、CubeF7 V1.17.4、ARM GCC/CMake/Ninja、VS Code |
| SYS/RCC | Trace Asynchronous Sw；HSE Bypass25MHz，M25/N400/P2，HCLK200MHz，PCLK1 50MHz/PCLK2 100MHz；PLLQ/2，不提供48MHz域 |
| USART1 | PA9 TX/PB7 RX，115200 8N1无流控；RX IRQ5/0，无UART DMA；本机ST-LINK VCP COM3 |
| 网络 | ETH/RMII、LAN8742地址0/ID0007:C131，100Mbps全双工；NO_SYS LwIP2.1.2轮询，无ETH IRQ；ETH有专用内部DMA |
| 地址/端口 | 旧实测板192.168.10.1/24；A=.1、B=.2/24身份已实测；PC物理以太网192.168.10.100/24；板5005→PC5005，PC6006↔板5006，PC6007↔板5007 |
| PTP | 原BSP本地时钟与RX/TX硬件戳、板端四时刻估算观测已实测；M2.3.2 B校时/校频短时观测通过；仍SYNC=0，PPS本地排程已观测但电气波形未验收 |
| 温度 | DHT11模块3.3V，DATA为PG7/D4、开漏释放；5秒采集、预读/超龄机制；AUTO/手动及串口命令已实测，无温度UDP共享 |
| I2C3 | PH7 SCL/PH8 SDA；WM8994地址0x1A，ID8994已实测；触控业务未实现 |
| SAI2 | A主接收提供时钟，B内部同步从接收；16位双通道，4时隙中1/3有效；标称48kHz，推导约47991.071Hz，PLLI2S344/Q7/DivQ1 |
| 音频DMA/IRQ | DMA2 Stream7 Channel0，P→M halfword circular，32768元素/64KiB；DMA2_Stream7和SAI2 IRQ均4/0；半满/满每8192帧 |
| 内存/Cache | 音频SRAM1 0x20010000..0x20020000、32字节对齐；主栈0x20047000..0x20048000，LwIP堆窗0x20048000..0x2004C000；ETH描述符/RX池在SRAM2；I/D Cache关闭 |
| FMC/SDRAM | 用户生成38脚/PC3 SDCKE0、CAS3/HCLK3，Region1=C0000000/8MB普通非缓存XN，Region2=A0000000/4KB共享Device；U1历史8MB四轮实测通过；当前U2为显示READY，不重做破坏性自检 |
| LTDC显示 | RGB565/480×272/9.6MHz，C0000000/C0040000双缓冲；延后生成初始化、前台八行分片/VBR轮询，用户确认实屏正常 |
| 当前未完成 | PPS波形/双板同步外部精度、TouchGFX/DMA2D、温度共享、标准声音识别/到达校准与测距；实验能量候选已接入 |

生成与实测的完整资源边界见[Hardware_Resources](Hardware_Resources.md)。任何引脚、时钟、DMA、IRQ变更仍由CubeMX负责；此前M2.3.2仅调整UDP PCB池。U1用户生成FMC/MPU Region1/2并通过自检；U2用户生成LTDC并已点亮，助手未手改生成初始化。

## 前一M3.2.4实物证据与已知限制

[M3.2.4完整记录](Milestones/M3_2_4_Audio_Time_Map_Board.md)：单次授权烧录后120.190秒，120个有效锚点、拒绝0、WIDTH0/1；40组板端估算与真实C/Fraction逐值相同，残差范围−14.884～+13.200us。120对原音频位置连续；110包UDP零缺口、两组30请求及原板端估算对照通过；23个自动温度版本、60次codec ID读取无错。

codec最大轮询间隔2068ms、心跳最大1015ms保留，不宣称精密前台周期。复位前14行及新BOOT前63字节残片仅作边界排除，残片含0xff；raw二进制与JSON投影逐行一致，新BOOT后严格ASCII且无重启。原始证据未改写。

NDTR进度不是SRAM读取授权；WIDTH=0仍有一帧量化；codec/SAI/FIFO/DMA到物理声音到达的延迟未校准。留出残差及模型区间不能证明声学精度、绝对频率或双板同步。

此前温度[连续拔插恢复](Milestones/T1_1_Temperature_Replug.md)、[手动设温](Milestones/T1_2_Manual_Temperature.md)、音频[受控发声](Milestones/M3_1_Audio_DMA.md)、[5分钟稳定性](Milestones/M3_1_Quiet_Stability.md)和[温度联动](Milestones/M3_1_Temperature_Coexistence.md)各有分版证据；不升级成当前版本已重复执行。

## 后续与验收分层

M3.2.4通过10组原生模块（Audio10程序）、Tools83/Analysis87项及两配置构建/布局，并完成上述Debug实物验收。声学延迟校准、事件查询/检测、双板同步/PPS及LCD仍需独立规划；完整课程指标见[路线图](Project_Roadmap.md)，M2.3.2已通过短时运行观测，最终整机要求保留。

最新固件和日志修复见[M4.1.3报告](Milestones/M4_1_3_Sound_Event_Time.md)，当前显示软件及硬件关口见[U0报告](Milestones/U0_RGB565_Status_Page.md)；M2.3.2仅为此前双板短时观测，不代表外部精度。
