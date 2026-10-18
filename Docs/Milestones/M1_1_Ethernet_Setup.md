# M1.1 当前板以太网链路：CubeMX操作单

更新：2026-10-18。状态：用户已生成ETH/RMII，GCC链接布局修复及Debug/Release检查通过；PHY观察应用尚未接入，新固件尚未实测。本步只推进当前板。

## 1. 本步目标与边界

在现有SBCG工程中启用ETH/RMII，之后通过串口确认板载LAN8742A的实际PHY地址、标识及网线插拔状态。验收目标是PHY可访问，链路DOWN→UP→DOWN→UP可以观察，协商速度/双工可读，现有串口心跳仍正常。

本步选用HAL管理接口加LAN8742组件轮询。另一方案是同时引入LwIP、IP地址及帧收发，它增加DMA缓冲、协议栈和调度变量，放在后续网络增量。当前阶段不配置IP、不以ping或UDP作为验收，不启用FreeRTOS、PTP/PPS、音频或LCD。

硬件配置已由用户经CubeMX生成，HAL_ETH_MODULE_ENABLED已启用。本次代码侧内存修复为**No CubeMX change required**。下文配置步骤供复核使用。不得手改`.ioc`或在应用代码中补写引脚/时钟初始化。硬件配置必须先由用户生成，再审查实际结果。

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
| First Rx Descriptor Address | `0x2004C0A0`，按用户最终生成值 |
| First Tx Descriptor Address | `0x2004C000`，按用户最终生成值 |
| Rx Buffers Length（如显示） | 1524字节，保留本机默认；本阶段不启动帧接收，后续另审缓冲与对齐 |

地址依据本机STM32F746数据库及CubeF7示例，不能填写H7的0x30000000，也不能套用F767的0x2007C000。当前芯片RAM顶端是0x20050000。

本阶段仅轮询PHY管理寄存器，不调用HAL_ETH_Start/Start_IT，不提交以太网帧：NVIC中的Ethernet global interrupt保持关闭，不添加DMA1/DMA2流。ETH拥有自己的内部DMA，后续帧收发需要另行说明描述符、缓冲区、回调与中断路径。

LwIP和FreeRTOS均保持未启用。新版本ETH界面若没有PHY型号或PHY Address下拉框，属于可接受情况；LAN8742的组件接入、地址探测和标识核查在应用层实现，不为出现旧版菜单而启用Legacy ETH驱动。板上预期PHY地址为1，但必须读取确认，不能当作实测值。

## 5. 生成后先审查内存，不立即烧录

点击Generate Code并保存工程。此阶段可以执行已有Debug Build发现生成问题，但**先不要烧录ETH版本**，等待内存布局和应用接入检查。

已核查的具体原因：当前`STM32F746XX_FLASH.ld`将0x20000000起的320 KiB合成RAM区，栈顶为0x20050000，没有为ETH预留描述符区域。本方案采用官方F746示例的SRAM2描述符布局；具体DMA可达性须按F746总线架构核实，不能把H7的DTCM限制直接套到本工程。只在CubeMX中填写地址，不代表GCC链接后的符号实际位于该处。HAL_ETH_Init本身就会初始化描述符，不能把“尚未Start DMA”视为无需核查内存的理由。

生成后需要检查：

- Rx/Tx描述符的section名称及ELF/map实际地址是否匹配，是否与.bss、堆、栈重叠；后续内存保留策略依据实际生成文件通过明确的扩展实现，不手改build目录。
- 当前F7 HAL的ETH_DMADescTypeDef为40字节，4项数组各160字节，Tx最终范围为0x2004C000～0x2004C09F，Rx为0x2004C0A0～0x2004C13F。两块相邻且起点均32字节对齐；这不表示每个40字节描述符都单独按缓存行隔离。
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


## 8. GCC内存布局修复记录

本次只完成一个可静态验证的目标：让ETH描述符实际位于.ioc指定地址，并保护普通数据、newlib堆及MSP栈。未实现PHY识别、插拔日志或帧收发，不标记M1.1链路完成。

### 文件与边界

- `CMakeLists.txt`：在用户拥有的顶层文件引入内存扩展。
- `cmake/ethernet-layout.cmake`：有ETH配置时检查RMII与地址，选用用户包装脚本，注册原脚本/包装脚本/.ioc为链接依赖；无ETH的历史快照继续用原脚本。
- `cmake/linker/stm32f746_eth.ld`：INCLUDE原版`STM32F746xx_FLASH.ld`，追加两个NOLOAD描述符段、KEEP与尺寸/内存边界ASSERT。
- `Tools/Monitor/check_eth_layout.py`：检查真实ELF的地址、尺寸、32字节起点对齐、NOLOAD、非重叠、其它RAM段边界及向量初始MSP。
- 本页、`Docs/Project_State.md`、`Docs/Development_Workflow.md`：更新状态、验证命令和限制。

本次未修改.ioc、生成main/MSP、HAL、生成链接脚本或工具链，也未改用户新增的I2C3（PH7/SCL、PH8/SDA）。原有ETH/I2C3生成改动留工作区，等待各自实物验证；不纳入本次独立构建修复提交。

### 实际结果

| 项目 | Debug及Release结果 |
| --- | --- |
| DMATxDscrTab | 0x2004C000，160字节 |
| DMARxDscrTab | 0x2004C0A0，160字节 |
| 两个描述符段 | NOBITS/NOLOAD，起点32字节对齐 |
| `_estack`及向量初始MSP | 0x2004C000 |
| `_sstack` | 0x2004BC00，保留1024字节MSP栈 |
| ETH区域 | SRAM2的16 KiB；当前描述符占320字节，未增加其它缓冲 |

现有sysmem.c的_sbrk依据_estack减_Min_Stack_Size限制堆增长，因此堆不会增长进SRAM2。静态数据和最小堆也受链接ASSERT约束。NOLOAD描述符不由启动代码复制/清零；HAL_ETH_Init会初始化本阶段使用的描述符控制字段，后续帧收发/PTP仍需继续审查所有权和其它字段。

运行调用链仍为main → MX_ETH_Init → HAL_ETH_Init，之后是现有I2C3初始化及App_Init/App_Process。无新增IRQ、回调、DMA1/DMA2配置；未启动ETH内部DMA，未改Cache/MPU或添加缓存维护。新布局使用当前MPU允许访问的内部SRAM。

RAM显示95.10%是普通RAM起点到固定高地址段末尾的地址跨度，不是实际分配了约304 KiB数据。当前Debug的.data+.bss共928字节、描述符320字节，另有最小堆512字节和栈1024字节。

### 验证与复验命令

修复前同一地址检查因地址、NOLOAD及栈顶不符而失败；修复后Debug/Release均通过：

```powershell
python -B Tools/Monitor/autodebug.py build
python -B Tools/Monitor/check_eth_layout.py
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

- Debug报告：`Logs/AutoDebug/20261018T102507-2c64038d/report.json`；Flash 18000字节。
- Release报告：`Logs/AutoDebug/20261018T102607-303c3e30/report.json`；Flash 12104字节。
- Debug ELF SHA256：`7fdf9699931073109dd8c149c9dcc15944ee66d00ddaf0dad62cd7be87c35190`。
- Release ELF SHA256：`e3211ff33facf8030824eb0ad624b629226f0e2de0a7b225b26fb67c30b5030e`。
- 在Logs下用同一项目和CMake预设做隔离负例：增加Tx段尺寸、让静态数据侵入栈/ETH区，链接均按预期ASSERT失败；证据为`Logs/EthLayout/guards/results.json`和对应build.txt，未修改项目实际源文件。
- 独立提交兼容性：将当前Git HEAD快照与本次链接集成文件组合，在Logs下用同一Debug预设构建通过；不依赖尚未提交的ETH/I2C3生成变化。证据为`Logs/EthLayout/head-with-layout-build.txt`。
- Flash/Runtime：NOT_RUN。M0.2旧ELF实测不能推广到本次改变了启动MSP的新ELF。

本构建修复按ELF和链接保护静态验收，提交只包含上述用户拥有的文件。之后仍需接入LAN8742/MDIO观察，再按授权烧录并核对BOOT、心跳、PHY ID及插拔状态；不能仅凭地址检查通过宣布链路、UDP或PTP通过。

包装脚本的NOLOAD语义参考[GNU ld官方说明](https://sourceware.org/binutils/docs/ld/Output-Section-Type.html)，并以本机实际链接结果验证。
