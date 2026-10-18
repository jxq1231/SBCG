# M1.1 当前板以太网链路：CubeMX操作单

更新：2026-10-18。状态：配置方案已核对，等待用户通过CubeMX生成；ETH尚未启用，PHY及链路尚未实测。本步只推进当前板。

## 1. 本步目标与边界

在现有SBCG工程中启用ETH/RMII，之后通过串口确认板载LAN8742A的实际PHY地址、标识及网线插拔状态。验收目标是PHY可访问，链路DOWN→UP→DOWN→UP可以观察，协商速度/双工可读，现有串口心跳仍正常。

本步选用HAL管理接口加LAN8742组件轮询。另一方案是同时引入LwIP、IP地址及帧收发，它增加DMA缓冲、协议栈和调度变量，放在后续网络增量。当前阶段不配置IP、不以ping或UDP作为验收，不启用FreeRTOS、PTP/PPS、音频或LCD。

**CubeMX change required。** 当前`.ioc`只有SYS/RCC/GPIO/USART1，HAL_ETH_MODULE_ENABLED尚未启用。不得手改`.ioc`或在应用代码中补写引脚/时钟初始化。硬件配置必须先由用户生成，再审查实际结果。

## 2. 先打开原工程

1. 用STM32CubeMX打开工程根目录的`SBCG.ioc`，不要创建新的板卡工程或套用整套Discovery演示。
2. 使用现有约定CubeMX 6.18.1 / CubeF7 V1.17.4，Toolchain为CMake，保留现有工程路径和名称。
3. 保留SYS的Trace Asynchronous Sw、现有HSE Bypass和200 MHz时钟树，以及USART1 PA9/PB7、115200 8N1。
4. 在Project Manager → Code Generator确认Keep User Code when re-generating开启；本工程App入口必须保留在main的USER CODE区。

## 3. 启用ETH与引脚映射

在Pinout & Configuration → Connectivity → ETH中，将Mode设为**RMII**。选择普通RMII，不选择MII或带PTP同步输出的组合模式。

逐项核对以下9根必需信号。界面可能显示ETH_REF_CLK或ETH_RMII_REF_CLK等不同前缀，以信号含义和物理引脚为准。

| MCU引脚 | ETH信号 | 配置 |
| --- | --- | --- |
| PA1 | REF_CLK | AF11，输入来自PHY的50 MHz参考时钟 |
| PA2 | MDIO | AF11，管理数据 |
| PC1 | MDC | AF11，管理时钟 |
| PA7 | CRS_DV | AF11 |
| PC4 | RXD0 | AF11 |
| PC5 | RXD1 | AF11 |
| PG11 | TX_EN | AF11 |
| PG13 | TXD0 | AF11，不使用PB12替代板上线路 |
| PG14 | TXD1 | AF11，不使用PB13替代板上线路 |

在ETH → GPIO Settings核对复用推挽、No Pull、High或Very High速度。不要把REF_CLK改成MCU输出，也不需要改变当前系统PLL来产生50 MHz。

PG2另有板级RMII_RXER/PHYAD0连线。官方板卡示例设PG2为AF11，但本机CubeMX的普通RMII模式只列出上述9个信号，MCU数据库也未给PG2列出ETH选项。若界面没有RX_ER，不切到MII强行获取，不把PG2设为普通输出，也不手改生成MSP；保持PG2未分配并记录差异，生成后再结合HAL及实物链路结果处理。

现有占用的PA9/PB7串口、PA13/PA14/PB3调试、PH0/PH1时钟与这9根信号不重叠。PC1/PC4/PC5及PG11/PG13/PG14原先未在本工程分配，本步由ETH占用，不替换已验收功能。

PHY复位共用板上NRST，不添加ETH_RESET_GPIO；网线接CN9 RJ45，另一端接已通电交换机、路由器LAN口或PC有线网卡。当前不连接第二块开发板。板级线路仍按仓库现有MB1191参考核对，实物改板或不同修订须另行确认。

## 4. ETH参数与当前阶段的中断选择

在ETH → Parameter Settings中核对：

| 参数 | 本阶段设置 |
| --- | --- |
| Ethernet MAC Address | `02:00:00:00:00:01`，当前板开发用；不是A/B角色认定，后续两板MAC必须不同 |
| Rx Mode（如显示） | Polling Mode |
| Rx Descriptor Length | 4 |
| Tx Descriptor Length | 4 |
| First Rx Descriptor Address | `0x2004C000` |
| First Tx Descriptor Address | `0x2004C0A0` |
| Rx Buffers Length（如显示） | 1536字节；本阶段不启动帧接收，生成后核对实际值 |

地址依据本机STM32F746数据库及CubeF7示例，不能填写H7的0x30000000，也不能套用F767的0x2007C000。当前芯片RAM顶端是0x20050000。

本阶段仅轮询PHY管理寄存器，不调用HAL_ETH_Start/Start_IT，不提交以太网帧：NVIC中的Ethernet global interrupt保持关闭，不添加DMA1/DMA2流。ETH拥有自己的内部DMA，后续帧收发需要另行说明描述符、缓冲区、回调与中断路径。

LwIP和FreeRTOS均保持未启用。新版本ETH界面若没有PHY型号或PHY Address下拉框，属于可接受情况；LAN8742的组件接入、地址探测和标识核查在应用层实现，不为出现旧版菜单而启用Legacy ETH驱动。板上预期PHY地址为1，但必须读取确认，不能当作实测值。

## 5. 生成后先审查内存，不立即烧录

点击Generate Code并保存工程。此阶段可以执行已有Debug Build发现生成问题，但**先不要烧录ETH版本**，等待内存布局和应用接入检查。

已核查的具体原因：当前`STM32F746XX_FLASH.ld`将0x20000000起的320 KiB合成RAM区，栈顶为0x20050000，没有为ETH预留描述符区域。本方案采用官方F746示例的SRAM2描述符布局；具体DMA可达性须按F746总线架构核实，不能把H7的DTCM限制直接套到本工程。只在CubeMX中填写地址，不代表GCC链接后的符号实际位于该处。HAL_ETH_Init本身就会初始化描述符，不能把“尚未Start DMA”视为无需核查内存的理由。

生成后需要检查：

- Rx/Tx描述符的section名称及ELF/map实际地址是否匹配，是否与.bss、堆、栈重叠；后续内存保留策略依据实际生成文件通过明确的扩展实现，不手改build目录。
- 当前F7 HAL的ETH_DMADescTypeDef为40字节，4项数组各160字节，Rx目标范围为0x2004C000～0x2004C09F，Tx为0x2004C0A0～0x2004C13F。两块相邻且起点均32字节对齐；这不表示每个40字节描述符都单独按缓存行隔离。
- 当前I/D Cache未启用，先沿用此状态；不随机添加Cache清理调用。后续帧收发或启用D-Cache时，重新设计CPU/DMA内存所有权、MPU属性和缓存维护。
- 现有main中的App_Init/App_Process、顶层CMake的App源文件/包含目录仍保留；USART1、系统时钟和调试引脚未被重分配。

## 6. 预计生成变化与接下来做什么

可能变化的文件包括`.ioc`、`.mxproject`、`Core/Inc/stm32f7xx_hal_conf.h`、main、MSP、生成CMake，以及新增HAL ETH源/头文件。若开启每外设独立文件生成，则初始化可能位于`Core/Src/eth.c`和`Core/Inc/eth.h`；以实际生成结构为准。本方案不要求新增LwIP或RTOS文件，不应无故改动RCC、USART或TIM。

完成这次CubeMX生成后，再依次进行：审查生成配置和链接布局 → 在App/BSP中接入LAN8742管理接口与有限频率的链路日志 → 主机测试/Debug构建 → 按授权烧录 → 用SerialWeb记录插拔结果。具体源文件改动计划将在实际生成结果确定后给出，当前不预写依赖不存在ETH句柄的固件。

预期日志字段会包括PHY地址、PHY ID、链路状态、协商速度和双工；字段形式将在应用接入时确定，当前固件仍只有BOOT/HEARTBEAT。PHY初始化失败需区分MDIO访问错误、地址/ID不匹配及链路DOWN，未插网线不应阻塞串口心跳。仅RJ45灯亮不能代替MDIO和软件状态验收；PHY链路UP也不表示UDP/PTP已通过。

## 7. 本次核查来源

- 当前仓库的`.ioc`、main/MSP、HAL模块开关和链接脚本；工作区起点为已验收的M0.2提交`e6b839d`，未改固件或访问设备。
- 本机CubeMX数据库：`D:/STM32CubeMX/db/mcu/STM32F746NGHx.xml`，ETH IP版本为ethermac110_v3_0；参数定义位于`D:/STM32CubeMX/db/mcu/IP/ETH-ethermac110_v3_0_Modes.xml`。地址已按F7x6分支核对，界面默认Rx长度可能为1524，不能把它误记为F7全部界面的1536默认值。
- 本机CubeF7 V1.17.4的STM32746G-Discovery Ethernet示例、HAL ETH实现和LAN8742组件；在线对应[ST官方板卡示例](https://github.com/STMicroelectronics/STM32CubeF7/blob/v1.17.4/Projects/STM32746G-Discovery/Applications/LwIP/LwIP_HTTP_Server_Netconn_RTOS/Src/ethernetif.c)。只借其板级资源及驱动接口，不复制RTOS/LwIP初始化到本项目。
- 板级连线与修订限制沿用[硬件资源第4.4、5.5节](../Hardware_Resources.md)，当前板PHY和网口尚无实测结论。
