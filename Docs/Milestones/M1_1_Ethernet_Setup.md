# M1.1 当前板以太网链路：配置与单板验收

更新：2026-10-18。状态：M1.1当前板PHY识别及DOWN→UP→DOWN→UP观察通过，两次UP均为100 Mbps全双工；最后一段98条心跳连续、TX_ERRORS=0。用户确认前面重复BOOT来自手动复位，并允许ETH/I2C3生成快照原样纳入本增量；I2C通信不在验收范围。本步只推进当前板。

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

LwIP和FreeRTOS均保持未启用。新版本ETH界面若没有PHY型号或PHY Address下拉框，属于可接受情况；LAN8742的组件接入、地址探测和标识核查在应用层实现，不为出现旧版菜单而启用Legacy ETH驱动。历史板级参考曾预期地址1，**当前板实测地址为0**，PHY_ID1/ID2=0007/C131；继续使用探测结果，不硬编码参考地址。

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

已完成生成配置审查、链接布局修复、LAN8742管理接口接入、主机测试及Debug/Release构建。用户现已回传新固件的SerialWeb运行日志，链路观察结论见第10节；助手没有执行烧录或访问设备。当前硬件授权仍仅允许连接诊断，不包含写Flash，不为补记录重复烧录。

工作区固件在BOOT/HEARTBEAT之外新增`$ETH`行，字段和验收步骤见第9节。PHY初始化失败区分MDIO访问错误、地址/ID不匹配及链路DOWN；普通未插网线不阻塞串口心跳。仅RJ45灯亮不能代替MDIO和软件状态验收；PHY链路UP也不表示UDP/PTP已通过。

## 7. 本次核查来源

- 当前仓库的`.ioc`、main/MSP、HAL模块开关和链接脚本；工作区起点为已验收的M0.2提交`e6b839d`，未改固件或访问设备。
- 本机CubeMX数据库：`D:/STM32CubeMX/db/mcu/STM32F746NGHx.xml`，ETH IP版本为ethermac110_v3_0；参数定义位于`D:/STM32CubeMX/db/mcu/IP/ETH-ethermac110_v3_0_Modes.xml`。地址已按F7x6分支核对，界面默认Rx长度可能为1524，不能把它误记为F7全部界面的1536默认值。
- 本机CubeF7 V1.17.4的STM32746G-Discovery Ethernet示例、HAL ETH实现和LAN8742组件；在线对应[ST官方板卡示例](https://github.com/STMicroelectronics/STM32CubeF7/blob/v1.17.4/Projects/STM32746G-Discovery/Applications/LwIP/LwIP_HTTP_Server_Netconn_RTOS/Src/ethernetif.c)。只借其板级资源及驱动接口，不复制RTOS/LwIP初始化到本项目。
- 板级连线与修订限制沿用[硬件资源第4.4、5.5节](../Hardware_Resources.md)；当前板PHY标识和链路状态现有第10节实测证据，PCB修订仍未现场确认。


## 8. GCC内存布局修复记录

本节记录独立链接修复提交`23db576`：让ETH描述符实际位于.ioc指定地址，并保护普通数据、newlib堆及MSP栈。该提交当时未实现PHY识别、插拔日志或帧收发，不单独作为链路验收；后续应用及实物结果见第9、10节。

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
- 当时Flash/Runtime：NOT_RUN。后续用户已运行采用此布局的M1.1固件，证据见第10节；日志不包含独立Flash字节比对或MSP寄存器读取。

本构建修复按ELF和链接保护静态验收，提交只包含上述用户拥有的文件。后续已接入LAN8742/MDIO观察并收到用户实物日志，见第9、10节；不能仅凭地址检查通过宣布链路、UDP或PTP通过。

包装脚本的NOLOAD语义参考[GNU ld官方说明](https://sourceware.org/binutils/docs/ld/Output-Section-Type.html)，并以本机实际链接结果验证。

## 9. PHY观察应用与板上验收

### 代码与调用关系

本增量只观察PHY，不收发以太网帧。新增`App/BSP/bsp_ethernet.c/.h`管理ETH句柄和MDIO接口，`App/Network/ethernet_monitor.c/.h`负责轮询和日志。`App/app_main.c`保留启动/心跳并调度监视器；顶层`CMakeLists.txt`注册源文件和组件包含目录。`Drivers/BSP/Components/lan8742/`的源文件、头文件和LICENSE.txt原样取自本机CubeF7 V1.17.4。

本增量文件清单（不含原有用户生成改动）：

| 用途 | 文件 |
| --- | --- |
| BSP | `App/BSP/bsp_ethernet.c`、`App/BSP/bsp_ethernet.h` |
| 链路观察 | `App/Network/ethernet_monitor.c`、`App/Network/ethernet_monitor.h` |
| 调度/构建 | `App/app_main.c`、`CMakeLists.txt` |
| 原版ST组件 | `Drivers/BSP/Components/lan8742/lan8742.c`、`Drivers/BSP/Components/lan8742/lan8742.h`、`Drivers/BSP/Components/lan8742/LICENSE.txt` |
| 测试 | `Tests/Network/run_tests.py`、`Tests/Network/test_ethernet.c`、`Tests/Network/stubs/main.h`、`Tests/App/test_app_main.c` |
| 文档 | `README.md`、`Docs/README.md`、`Docs/Project_State.md`、`Docs/Project_Roadmap.md`、`Docs/SerialWeb.md`、`Docs/Milestones/M1_1_Ethernet_Setup.md` |

调用链：`main → CubeMX初始化 → App_Init → EthernetMonitor_Init`；`主循环 → App_Process → EthernetMonitor_Process → BSP_Ethernet_Init/ReadLink → LAN8742组件 → HAL_ETH_ReadPHYRegister`。日志复用`SendLine → BSP_Console_Write → HAL_UART_Transmit`，错误计入同一TX_ERRORS。

- **CubeMX**：No CubeMX change required。本增量未修改.ioc或任何生成初始化，用户已有的ETH/I2C3改动保留。
- **IRQ**：无新增中断或ISR回调。MDIO与UART调用均在主循环；使用现有SysTick计时。
- **DMA/Cache**：无新增DMA关系，不调用HAL_ETH_Start/Start_IT；已有描述符由HAL_ETH_Init初始化，内部帧DMA未启动。保持I/D Cache未启用，不添加缓存维护，不改MPU。
- **时钟**：调用HAL_ETH_SetMDIOClockRange按既有HCLK选择管理总线分频，200 MHz时为/102、MDC约1.96 MHz；不改RCC/PLL/引脚。

ST组件扫描地址0～31并读取SMR；应用随后验证PHYI1R=0007、PHYI2R高12位=C13，低4位版本号允许变化。不是把参考地址1硬编码为实测值。全0/全FFFF的ID判为NOT_FOUND。标识依据[Microchip LAN8742数据手册](https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/DataSheets/DS_LAN8742_00001989A.pdf)第61～62页。读取链路使用ST组件的BSR双读处理锁存低位；不复位PHY、不强制自协商、不写PHY寄存器，也不随协商结果配置MAC帧收发速度。

### 日志与已知边界

以下为格式示例，**不是实物结果**，地址1、PHY版本和速度须以真实输出为准：

```text
$BOOT,FW=SBCG-M1.1,HCLK_HZ=200000000,UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631
$ETH,INIT=OK,PHY_ADDR=1,PHY_ID1=0007,PHY_ID2=C130,AUTONEG=1
$ETH,LINK=DOWN,SPEED_MBPS=0,DUPLEX=UNKNOWN
$HEARTBEAT,SEQ=1,TICK_MS=1000,TX_ERRORS=0
$ETH,LINK=UP,SPEED_MBPS=100,DUPLEX=FULL
```

每250 ms轮询，状态变化立即记录，稳定状态每5000 ms刷新；没有积压补发。INIT还可能为NOT_READY、NOT_FOUND、IO_ERROR或ID_MISMATCH。无有效地址时PHY_ADDR=4294967295（UINT32_MAX哨兵），ID默认0000；失败行中的字段仅用于诊断，AUTONEG仅在INIT=OK时有效。链路可能为NEGOTIATING、DOWN、UP或READ_ERROR；未观察到短暂NEGOTIATING不算失败。

初始化失败或链路读取失败后按2000 ms间隔重试，间隔从尝试/轮询开始的tick计算，uint32回绕已测试。**现有生成配置PHY_READ_TO=0xFFFF，HAL单次卡死读取可能等待约65.5秒，期间主循环心跳会延迟**。本增量没有修改该超时；MB忙位未清时立即返回错误，避免扫描剩余31个地址再次长等待，也不主动清忙或复位外设。若总线持续忙，日志会继续报IO_ERROR；这是管理总线故障边界，不能宣称已有硬件恢复能力。普通网线未连接时PHY仍可访问，不应触发该故障。

BOOT位于MX_ETH_Init及MX_I2C3_Init之后。如果新固件完全没有BOOT，先检查烧录/复位与生成初始化中的Error_Handler路径，不能直接判断为串口参数错误。

### 已完成的主机验证

```powershell
python -B Tests/Network/run_tests.py
python -B Tests/App/run_tests.py
python -B Tools/Monitor/autodebug.py build
python -B Tools/Monitor/check_eth_layout.py
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

- PHY/监视器8项通过：实际地址3及版本位、型号不匹配、空总线FFFF、忙总线不重复HAL等待、插拔/协商/速度双工、稳定日志限速、读失败重试恢复、初始化失败及tick回绕。使用真实BSP、ST组件及监视器，HAL由测试替身提供；不证明真实总线时序。
- 应用5项通过：版本/UID、每秒调度及未到心跳时仍调度PHY、tick回绕、无积压补发、发送失败计数。
- ST组件保留原样；主机测试只对该组件禁用已知的ClearIT未用参数诊断，自有代码使用-Wall/-Wextra/-Werror。
- Debug构建报告：`Logs/AutoDebug/20261018T151154-c71d5a6e/report.json`，Flash20512字节；Release报告：`Logs/AutoDebug/20261018T151214-bd60082e/report.json`，Flash13728字节。均无新增编译警告，两个布局检查通过。
- Debug ELF SHA256：`676cbc5043fcabfbb4f835a0fd2d54d82fcd014a922a943de3d0083d3dd24ef7`。
- Release ELF SHA256：`bcab3aac03ece522fc323a47f89ba4095db8ad97d3abd349ac25cd76a57699b4`。
- **助手Flash：NOT_RUN；Runtime：用户日志确认PHY识别与链路插拔观察通过，详见第10节；应用、测试及获用户允许的生成快照作为本增量提交。** M0.2实测不代替本次证据。
- 测试输出保存为`Logs/M1_1/network-tests.txt`和`Logs/M1_1/app-tests.txt`；生成文件未改动、ST组件逐字节一致及两份ELF哈希核对记录为`Logs/M1_1/verification.json`。

### 当前板验收复现步骤（用户日志已回传）

1. 用户在VS Code手动执行`STM32: Flash`，确认Programming Finished及Verified OK。助手当前不执行Flash写入。
2. 打开[SerialWeb](https://conductance-lab.xyz/SerialWeb/)，选择ST-LINK VCP，115200、8N1、无流控、UTF-8。连接后按板上复位键，记录FW=SBCG-M1.1的BOOT、INIT、链路及至少10条心跳。
3. 先不插网线，确认INIT=OK、实际ID正确、LINK=DOWN、心跳持续且TX_ERRORS=0。
4. 插入接往已通电交换机/路由器LAN口的网线，等待协商为UP；记录速度与双工。拔出确认DOWN，再插回确认UP。当前不需要第二板，也不要求ping。
5. 将完整BOOT、INIT、插拔前后ETH行和心跳回传，原始日志可保存到`Logs/M1_1/`。若失败，保留原始输出定位；不跳到UDP/PTP。

按根目录AGENTS.md第19节，硬件结果确认后才能认定M1.1完成并提交功能增量；第18节要求在这里停止，不自动开始下一里程碑。

## 10. 用户实物日志验收结果

来源：用户于2026-10-18粘贴的SerialWeb接收文本。原样保存为`Logs/M1_1/user-serial-20261018.txt`，解析摘要为`Logs/M1_1/user-serial-20261018-summary.json`；原始日志由Git忽略。原文SHA256为`cebb74fa9b5ad73d9549ebdee0c09df3fe9bebb095867d49e23cbedff4d1707f`。

| 检查项 | 实际观察 | 结论 |
| --- | --- | --- |
| 固件与板标识 | FW=SBCG-M1.1；HCLK_HZ=200000000；UID=001A002D/30365105/36363631 | 当前板日志匹配；非独立测频或Flash字节比对 |
| PHY初始化 | 三个BOOT段均INIT=OK、PHY_ADDR=0、PHY_ID1=0007、PHY_ID2=C131、AUTONEG=1 | 实际地址0，型号匹配；低4位版本1被正确接受 |
| 最后一段心跳 | SEQ=1～98，tick=1015～98015 ms，97个相邻差全部1000 ms，TX_ERRORS全部0 | 该97秒心跳跨度内无缺号或BOOT，日志发送正常 |
| 链路变化 | DOWN→UP→DOWN→UP，最后一段共22条链路报告 | 状态变化及稳定刷新均可观察 |
| 两次UP | SPEED_MBPS=100、DUPLEX=FULL | PHY协商结果可读 |
| MDIO错误 | 全文没有INIT失败或LINK=READ_ERROR | 本次正常链路实验未触发错误路径 |

链路状态行没有自带tick，只能用相邻心跳界定报告出现时段，不能当作物理插拔时刻或链路恢复耗时：

| 状态变化 | 前一心跳tick / ms | 后一心跳tick / ms |
| --- | --- | --- |
| 首次UP | 82015 | 83015 |
| 随后DOWN | 94015 | 95015 |
| 再次UP | 97015 | 98015 |

日志开头另有两段相同BOOT/INIT/DOWN及SEQ=1、tick=1015的短片段。用户最初不确定，随后明确确认前面是手动复位。三个片段均到达INIT=OK、LINK=DOWN及第一条心跳，作为手动复位后启动成功的记录；最后一段有98条连续心跳。未执行冷启动或长期稳定性试验，不能推广该结论。

复验执行8项PHY/监视器及5项应用测试通过；Debug及Release按现有工作流构建、ELF布局检查均通过，报告分别为`Logs/AutoDebug/20261018T152439-0e02e74a/report.json`、`Logs/AutoDebug/20261018T152621-6ea4f3f8/report.json`；两份ELF哈希与第9节一致，源码未改动。本轮助手只处理文件与主机工具，没有连接ST-LINK或串口、复位或写入设备。BOOT说明执行已越过生成的ETH/I2C3初始化；不等于I2C器件读写、MAC帧收发、UDP、PTP或第二板验证通过。

提交前另将暂存区导出至`Logs/M1_1/staged-bd64752b/`，用同一Debug预设从零构建及布局检查通过，确认构建不依赖未纳入提交的源文件；报告为该目录下`Logs/AutoDebug/20261018T152935-32198c64/report.json`。ST组件按原版保留，包括其头文件/许可证原有行尾空格；自有文件通过diff空白检查，不为格式检查改写第三方源码。

提交边界：用户已明确允许将共同生成的ETH/I2C3快照原样随M1.1提交。包含`SBCG.ioc`、`.mxproject`、`Core/Inc/stm32f7xx_hal_conf.h`、`Core/Src/main.c`、`Core/Src/stm32f7xx_hal_msp.c`、`cmake/stm32cubemx/CMakeLists.txt`、`Drivers/STM32F7xx_HAL_Driver/Inc/stm32f7xx_hal_eth.h`、`Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_eth.c`和`Drivers/STM32F7xx_HAL_Driver/Inc/stm32f7xx_ll_i2c.h`。这些用户文件原样保留，没有手改.ioc或生成初始化；I2C器件通信未验收。应用、ST组件、测试及文档一并提交以保持可构建快照。按AGENTS.md第18节在本增量结束，不自动开始UDP。
