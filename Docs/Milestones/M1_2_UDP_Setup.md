# M1.2 LwIP集成验收与单板UDP准备

更新：2026-10-19。M1.1当前板PHY识别和链路观察已通过，提交为`3807edd`。用户生成LwIP后，SBCG-M1.2-INT集成修复已通过Debug/Release构建、主机检查及基本实物验收：12条连续心跳、100 Mbps全双工、ping 4/4回复。UDP发送业务尚未实现。第1～6节保留配置操作及原准备记录，第7节记录集成修复，第8节为最新实测结果；无需重复生成已完成配置。

## 1. 目标、方案与边界

M1.2的独立验收目标：当前板向电脑每秒发送一个有版本、序号和运行tick的UDP数据报，电脑实际收到至少30包；拔插网线后可以恢复发送，串口心跳继续正常。当前只准备这个目标所需的CubeMX生成配置，不涉及第二板、双向应用协议、PTP、声音采集或LCD。

选择**无RTOS的LwIP、Raw API、静态IPv4、轮询**。其它选择是引入FreeRTOS及Socket/Netconn，或手写ARP/IP/UDP；前者增加线程及同步变量，后者增加协议维护负担，当前都不采用。NO_SYS=1时只能使用Raw API，不能使用LwIP Socket/Netconn；网络输入和定时器由主循环持续处理。[LwIP主循环模式说明](https://www.nongnu.org/lwip/2_1_x/group__lwip__nosys.html)、[Raw API说明](https://lwip.nongnu.org/2_1_x/group__callbackstyle__api.html)。

固件接口/协议/主机接收脚本将在生成后按实际文件制定最小改动，不预写依赖尚不存在的LwIP符号的代码。SerialWeb继续观察串口，后续电脑的UDP接收由`Tools/Monitor/`工具承担，二者是不同通道。

## 2. 必须通过CubeMX处理的变化

**CubeMX change required**：启用LwIP并修改其生成选项；将ETH接收缓冲长度从1524改为1536，使长度为32字节整数倍并与新网络接口一致。LwIP接管网络接口初始化和链路驱动，不能继续把M1.1的“只读PHY、不启动帧DMA”条件套到新固件。

不更换现有RMII九根信号，不调整SYS/RCC、USART1或I2C3，不增加TIM、SAI、GPIO输出或FreeRTOS。保留PHY地址探测，当前板实测地址是0、ID=0007/C131；Platform Settings选择LAN8742，不硬编码参考地址1。

生成前保留当前Git提交。只打开根目录`SBCG.ioc`，使用既有CubeMX 6.18.1、CubeF7 V1.17.4、GCC/CMake工程；不要创建替代工程或迁移软件包。本机该软件包附带LwIP 2.1.2，不为使用更高的通用版本而迁移库。

## 3. CubeMX操作顺序

### 3.1 ETH继续轮询

在Connectivity → ETH复核：

| 参数 | 本步设置 |
| --- | --- |
| Mode | RMII，原九根引脚保持 |
| Rx Mode（如显示） | Polling Mode |
| MAC | 02:00:00:00:00:01，当前板测试地址 |
| Tx / Rx Descriptor Length | 各4 |
| First Tx Descriptor Address | 0x2004C000 |
| First Rx Descriptor Address | 0x2004C0A0 |
| Rx Buffers Length | **1536**，本步唯一ETH参数调整 |
| Ethernet global interrupt | 关闭；生成后检查是否确实采用轮询路径 |

不添加DMA1/DMA2 Stream。以太网帧使用ETH内部DMA，不能因此把它写成“没有DMA”。本机F7模板依据无RTOS及ETH中断选项选择HAL_ETH_Start/Stop轮询路径；如果生成结果调用Start_IT或自动启用IRQ，应先回传审查，不手工删中断或改生成函数。

### 3.2 启用LwIP及PHY组件

在Middleware and Software Packs（或当前版本的Middleware分类）→ LWIP中启用。打开LwIP的**Platform Settings**，选择PHY Driver为**LAN8742**，不要留在Choose/Undefined或选择DP83848。CubeMX可能重新复制已有的原版LAN8742组件，这是预期生成行为，生成后检查是否有重复构建源。

FreeRTOS保持关闭。General Settings、Key Options中的基本配置如下；以宏名称为准，部分依赖项会由CubeMX自动设置或隐藏：

| 设置 | 值 | 用途 |
| --- | --- | --- |
| WITH_RTOS | 0 / Disabled | 无RTOS |
| NO_SYS | 1 / OS Not Used | 主循环模式 |
| LWIP_NETCONN / LWIP_SOCKET | 0 / Disabled | 不使用线程API |
| LWIP_IPV4 / LWIP_ARP | 1 / Enabled | 同网段IPv4及ARP |
| LWIP_IPV6 | 0 / Disabled | 当前不使用IPv6 |
| LWIP_UDP | 1 / Enabled | UDP收发基础 |
| LWIP_TCP | 0 / Disabled | 当前不使用TCP |
| LWIP_ICMP | 1 / Enabled | 后续可辅助ping排查，不替代UDP验收 |
| LWIP_DHCP | 0 / Disabled | 静态地址，不等待DHCP |
| LWIP_TIMERS | 1 / Enabled | 协议定时器 |
| LWIP_NETIF_LINK_CALLBACK | 1 / Enabled | 生成链路检查及状态处理 |
| LWIP_SUPPORT_CUSTOM_PBUF（如显示） | 1 / Enabled | 匹配F7模板的零拷贝接收池 |

不要在生成前手改头文件来补上述宏。界面依赖报错时先核对NO_SYS与FreeRTOS状态；不要通过启用RTOS来消除报错。

### 3.3 静态IPv4

关闭DHCP后填写：

| 项目 | 值 |
| --- | --- |
| IP_ADDRESS | 192.168.10.1 |
| NETMASK_ADDRESS | 255.255.255.0 |
| GATEWAY_ADDRESS | 0.0.0.0 |

这些地址仅用于独立测试网络，当前板尚未按UID指定为最终A角色。后续B板的192.168.10.2不在本轮使用。只在同网段通信，不需要默认网关或DNS；若所在网络已有设备使用192.168.10.1，先调整测试网络方案，不能制造地址冲突。

后续电脑接收目标约定为192.168.10.100、UDP端口5005，当前不填写到CubeMX的板端IP栏；UDP目标端口由应用配置，不是CubeMX生成LwIP的成功条件。

### 3.4 缓冲、堆与校验

| 参数 | 本步设置 | 说明 |
| --- | --- | --- |
| ETH_RX_BUFFER_SIZE | 应随ETH得到1536 | 如不可见，生成后核对；不能与heth.Init.RxBuffLen不一致 |
| ETH_RX_BUFFER_CNT | 4 | 是接收池缓冲数量，与4项描述符区分；不沿用模板默认12而不检查内存 |
| MEM_ALIGNMENT | 4 | 本机2.1.2数据库提供4或2，保持4；不手改生成宏强设32 |
| MEM_SIZE | 4096 | 本阶段LwIP内部堆初始容量，实际占用还含分配器开销 |
| LWIP_RAM_HEAP_POINTER（如显示） | 0x20048000 | F7数据库默认地址；生成时需检查保留策略，本次修复已按第7节保留 |
| MEM_LIBC_MALLOC / MEMP_MEM_MALLOC | 0 / Disabled | 先保留LwIP内部堆/池分配方式 |
| CHECKSUM_BY_HARDWARE | 保留Enabled | 生成后检查软件校验宏与HAL校验功能一致，不另行手改宏 |

I/D Cache及MPU保持M1.1状态，不在此步开启Cache或复制H7的MPU/缓存维护方案。1536字节长度和模板中的对齐声明不证明所有实际缓冲地址都32字节对齐，后续必须检查接收池元素布局、CPU/DMA所有权；目前不添加随机Clean/Invalidate调用。

**重要内存边界**：M1.1的包装脚本只保护两个描述符及其上方SRAM2区域，普通newlib堆可增长到约0x2004BC00。固定LwIP堆指针0x20048000位于这个范围内，单凭构建通过不会阻止两个堆重叠。LwIP的固定堆还可能不显示为ELF中完整占用段，因此生成后必须扩展用户拥有的保留/检查策略；本次修复结果见第7节，不能直接沿用M1.1的ETH_LAYOUT_OK结论。

## 4. 生成后停在这里，不烧录

确认Project Manager → Code Generator → Keep User Code when re-generating开启，然后Generate Code并保存。生成完成后回传“已生成”，由代码审查继续；不要为了消除构建错误手改生成代码，也不要直接执行Flash任务。

预期出现或变化的文件：

- `SBCG.ioc`、`.mxproject`、HAL配置及生成CMake。
- `LWIP/App/lwip.c`、`LWIP/App/lwip.h`、`LWIP/Target/ethernetif.c/.h`、`lwipopts.h`等，具体目录以本机生成结果为准。
- `Middlewares/Third_Party/LwIP/`及LAN8742组件生成引用。
- `Core/Src/main.c`中的LwIP初始化及主循环处理；ETH句柄/描述符/MSP定义可能从main相关文件转移到网络接口文件。

下一轮必须先确认：

1. 只有一份heth、描述符、HAL_ETH_MspInit及LAN8742编译源，不与现有顶层CMake重复。
2. MX_LWIP_Init完成后，主循环持续调用MX_LWIP_Process；不能在一秒心跳条件内才调用。模板应处理ethernetif_input、sys_check_timeouts和周期链路检查。
3. 网络接口承担ETH启动、链路及MAC协商配置；原M1.1观察模块的READY状态判断需适配网络运行状态，不能持续报NOT_READY，也不重复HAL_ETH_Init或抢占PHY管理。
4. PHY实际地址探测仍有效，NO_SYS/UDP/静态IP均正确，未引入RTOS或未经说明的IRQ。
5. 接收缓冲长度/池数量/分配回收回调符合实际HAL；检查ETH内部DMA、pbuf生命周期与无RTOS轮询收发路径，不能只看PHY灯亮。
6. 建立固定堆及Rx池的内存保留/断言，核对真实ELF/map、栈余量与Cache状态。现有M1.1检查工具可能需要扩展，不机械复制PASS结论。
7. main的USER CODE入口、USART1心跳、原时钟、I2C3及调试设置保留，遵守CubeMX生成边界。

通过上述审查后，再限定一个固件增量接入单向UDP及电脑接收工具，使用既有CMake Debug工作流构建。助手当前没有Flash写入授权；用户手动烧录或另行授权之后，以真实串口及UDP接收结果验收，不在本轮预报PASS。

## 5. 电脑准备与未来验收

本轮只读检查发现：Realtek有线网卡为Up、100 Mbps，IPv4为169.254.212.178/16；这是检查时快照，不保证以后不变。电脑网络设置本轮未修改。

后续接入应用时，用户在Windows有线网卡的IPv4设置中使用192.168.10.100、掩码255.255.255.0，网关/DNS留空；Wi-Fi设置不变。先记下原有线配置，实验结束可按原状恢复。使用独立交换机或电脑有线口与当前板连接，不占用现有192.168.10.1设备所在网络。若需要防火墙放行，后续仅允许测试UDP端口和测试网段，不关闭整个防火墙；本轮不添加规则。

将来由`Tools/Monitor/`工具绑定有线地址接收数据，验收至少30个逐秒数据报，记录版本、连续序号、接收时刻和来源地址；UDP本身不保证交付，串口发送成功不代表电脑收到。额外记录断线/重连和恢复后的包序号，缺包如实计数，不补造数据。ping只能辅助诊断，不能替代UDP到达证据；PC不承担课程正式测距计算。

## 6. 本步验证及停止点

2026-10-18的M1.2.0准备增量仅修改文档，没有新增IRQ、DMA启动或回调，调用链仍是M1.1。当时构建通过只证明M1.1基线，不能证明LwIP或UDP功能；随后生成及集成修复另见第7节。

本步检查：`python -B Tools/Monitor/autodebug.py build`及`python -B Tools/Monitor/check_eth_layout.py`通过，构建报告为`Logs/AutoDebug/20261018T153653-6808d332/report.json`；三份文档的相对文件链接检查通过。变更文件仅为本配置单、`Docs/README.md`及`Docs/Project_State.md`，未改.ioc、固件、构建配置或电脑网络。

等待用户经CubeMX生成的依据为根目录[AGENTS.md第4节](../../AGENTS.md)：硬件和生成配置变化须走CubeMX，生成后重新审查；不得手改.ioc或用手写初始化替代。用户完成生成后再继续M1.2代码工作，不自动推进到双板/PTP。

配置来源：本机CubeMX的`db/mcu/IP/LWIP-v2.1.2_Cube_Modes.xml`、`db/templates/ethernetif_f7.ftl`、`db/templates/lwip_c.ftl`及CubeF7 V1.17.4附带的LwIP头文件；具体生成输出优先于模板推断。协议API语义参考上文LwIP官方文档。

## 7. M1.2 LwIP代码集成修复

### 修复范围与文件

独立目标是恢复生成后的构建、主循环网络处理及内存隔离，不增加UDP应用或进入下一阶段。用户生成快照已确认：LAN8742绑定、NO_SYS=1、WITH_RTOS=0、UDP启用、TCP/DHCP关闭；IP/掩码/网关=192.168.10.1/255.255.255.0/0.0.0.0，MEM_SIZE=4096，Rx池4个、每缓冲1536字节。

| 文件 | 修复内容 |
| --- | --- |
| `CMakeLists.txt` | 有生成的LwIP目标时使用其驱动引用，避免LAN8742重复编译；定义SBCG_USE_LWIP。旧PHY-only快照保留独立驱动引用 |
| `cmake/ethernet-layout.cmake` | 校验生成的固定堆地址/容量，设置LwIP链接标志，新增生成选项依赖 |
| `cmake/linker/stm32f746_eth.ld` | 同时接纳旧DecripSection和新DescripSection输入段；保留固定堆、移动MSP、放置接收池并添加ASSERT |
| `LWIP/Target/ethernetif.c` | 仅在USER CODE 1/2放置接收池extern属性及静态断言；不改生成初始化、HAL调用或描述符声明 |
| `App/app_main.c` | 每轮先调用MX_LWIP_Process再调度日志；新增版本SBCG-M1.2-INT，未生成LwIP的旧模式保留M1.1标识 |
| `App/BSP/bsp_ethernet.c` | 接受READY/STARTED；LwIP模式复用已发现的PHY地址和对象的只读副本，不重新扫描、初始化或改写所有者回调 |
| `Tools/Monitor/check_eth_layout.py` | 在实际ELF中核对堆、栈、Rx池及其它RAM段，与生成堆参数对照 |
| `Tools/Monitor/tests/test_eth_layout.py` | 固定堆/栈/接收池的10项正反例检查 |
| `Tests/Network/test_ethernet.c`、`Tests/Network/stubs/main.h`、`Tests/Network/run_tests.py` | 独立PHY与LwIP共享对象两种模式，共19项行为检查 |
| `Tests/App/test_app_main.c`、`Tests/App/run_tests.py`、`Tests/App/stubs/lwip.h` | 5项应用检查，验证未到心跳时也处理网络及调用顺序 |
| `Drivers/BSP/Components/lan8742/LICENSE.txt` | 恢复CubeMX生成时删除的本机软件包原版许可证，与HEAD原内容一致 |
| 本页、`Docs/Project_State.md`、`Docs/README.md`、`README.md`、`Docs/Project_Roadmap.md`、`Docs/Hardware_Resources.md`、`Docs/SerialWeb.md` | 更新当前状态、证据与实物验证步骤 |

No CubeMX change required。本修复未手改.ioc、main/MSP、HAL配置、生成CMake、lwip.c、lwipopts.h、生成链接脚本或工具链；这些用户生成文件按起点哈希核对保持不变。ethernetif.c的非USER CODE内容也经去除用户区后的哈希核对保持不变。生成CMake原有的行尾空白保持原样，不为diff格式提示改写用户生成内容。

### 调用链、IRQ、DMA与Cache

初始化：`main → MX_LWIP_Init → lwip_init/netif_add → ethernetif_init → low_level_init → HAL_ETH_Init/RX_POOL初始化/LAN8742发现`。链路UP时生成接口配置MAC协商结果并调用HAL_ETH_Start；ETH初始化及控制权仍属于生成网络接口。

运行：`main循环 → App_Process → MX_LWIP_Process → ethernetif_input + sys_check_timeouts + 周期链路检查`，之后执行限速心跳和PHY日志。MX_LWIP_Process不受一秒心跳条件限制；PHY日志模块只读取已发现设备，不接管ETH启动/停止。

无新增IRQ。仍为NO_SYS主循环轮询，现有SysTick提供时基，未启用ETH global IRQ或DMA1/DMA2中断。HAL回调不自动等于ISR：本配置下回调由主循环里的HAL收发路径调用。

| DMA关系 | 当前生成配置与生命周期 |
| --- | --- |
| RX | ETH内部DMA：MAC接收→RX_POOL中的payload；4个缓冲，每个1536字节，4项Rx描述符；不是DMA1/DMA2的Normal/Circular模式 |
| 接收分配/回收 | HAL_ETH_RxAllocateCallback从池分配，HAL_ETH_RxLinkCallback组装pbuf；pbuf释放后pbuf_free_custom归还池；链路由生成代码轮询控制 |
| TX | LwIP pbuf→ETH内部DMA→MAC，4项Tx描述符；当前生成glue使用HAL_ETH_Transmit轮询，20 ms超时；没有新增应用发包 |
| 半传输/完成IRQ | 无DMA1/DMA2半完成关系；没有新增ETH完成中断处理。保留生成接口已有的分配、链接、释放回调 |

I/D Cache保持未启用，MPU配置不变。RxLinkCallback原本已有Invalidate调用，本次保留，未添加新的维护调用。当前对齐和内存隔离不代表未来可直接开启D-Cache；TX清理、RX所有权和缓存策略仍须另行验证。

### ELF内存结果

| 区域 | Debug及Release实际结果 |
| --- | --- |
| MSP顶端/向量初始MSP | 0x20048000 |
| 最小MSP范围 | 0x20047C00～0x20047FFF，保留1024字节；非实际栈峰值证明 |
| 固定LwIP堆保留窗 | 0x20048000～0x2004BFFF，共16 KiB，NOLOAD；MEM_SIZE实际4096，另含分配器开销 |
| Tx描述符 | 0x2004C000～0x2004C09F，160字节，NOLOAD |
| Rx描述符 | 0x2004C0A0～0x2004C13F，160字节，NOLOAD |
| RX_POOL存储/段 | 起点0x2004C140；存储符号6275字节，段含对齐共6304字节，到0x2004D9DF，NOLOAD |

现有_sbrk按新_estack减_Min_Stack_Size限制普通堆，因此不能增长进固定LwIP堆。LwIP的mem_init和memp_init_pool初始化各自NOLOAD区域；不依赖启动代码将这些区域清零。静态断言要求MEMP_SIZE=0、MEM_ALIGNMENT=4、池数4、payload1536、RxBuff_t步长及buff偏移为32的整数倍；结合池基址32字节对齐，当前每个payload也保持32字节对齐。

RAM打印97.02%是从RAM起点到固定高地址段末尾的地址跨度，不代表普通数据用了约310 KiB。Debug的.data+.bss为12168字节；另有最小堆/栈1536字节、堆保留窗16384字节、描述符320字节及Rx池段6304字节。不能用该百分比替代栈余量、实际堆压力或收发性能测试。

### 验证证据

```powershell
python -B Tests/Network/run_tests.py
python -B Tests/App/run_tests.py
python -B -m unittest discover -s Tools/Monitor/tests -p test_eth_layout.py -v
python -B Tools/Monitor/autodebug.py build
python -B Tools/Monitor/check_eth_layout.py
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

- 修复前Debug链接失败：LAN8742有14个重复定义，旧描述符段匹配断言失败；报告`Logs/AutoDebug/20261018T155241-68695fb4/report.json`。
- PHY两种模式19项、应用5项、布局10项，共34项主机检查通过；新增STARTED及调度检查在修复前失败，修复后通过。布局反例初始缺检查时失败，实现后通过。
- Debug构建及ELF检查通过，报告`Logs/AutoDebug/20261018T160220-283b989f/report.json`，Flash52072字节。
- Release构建及ELF检查通过，报告`Logs/AutoDebug/20261018T160403-e0bc58b6/report.json`，Flash32504字节；两次构建均未见新增编译警告。
- 编译数据库中的lan8742.c条目数为1；源文件保护及ELF哈希见`Logs/M1_2_Integration/verification.json`。
- 使用同一项目及Debug预设，在Logs下分别注入过大静态数据、过大Rx池；两者都被预期链接ASSERT阻止，未改实际工程或.ioc。证据为`Logs/M1_2_Integration/guards/results.json`及对应build.txt。
- 将HEAD的PHY-only快照与本次用户拥有的构建/App/检查器修改组合，在`Logs/M1_2_Integration/legacy-snapshot/`用同一Debug预设构建及旧模式布局检查通过；该兼容性检查不替代新LwIP固件的实测。
- Debug ELF SHA256：`0439d2a3abb88171b58cbf49d15386ac2808341281c2d2d944fe053a77db8428`。
- Release ELF SHA256：`4b125913ada20a4c71188a7bd56cded508b4628a29c594810ded6d88d916f3c9`。
- 2026-10-19收尾复验：同一Debug/Release预设构建及ELF布局检查再次通过，报告分别为`Logs/AutoDebug/20261018T162117-c1e12dc0/report.json`和`Logs/AutoDebug/20261018T162117-148954d6/report.json`；ELF哈希与上列一致。报告目录使用UTC日期，本页日期为Asia/Shanghai。
- 复验PHY 19项、应用5项、布局10项全部通过；另外完整运行主机工具原有10项测试通过，共44项。工具测试有意模拟构建失败、超时等错误，其最终unittest结果为20项通过（工具10项+布局10项）。
- 将已暂存源码导出至`Logs/M1_2_Integration/staged-snapshot/`，不依赖原工作区构建产物，从零执行相同Debug工作流和布局检查通过，构建诊断为空；报告为该目录下`Logs/AutoDebug/20261018T162522-baf635ff/report.json`。此独立目录ELF仅用于提交完整性检查，不作为用户板端固件匹配证明。
- 助手未写Flash或连接目标；Runtime依据第8节用户日志确认。当前没有UDP应用发包或接收工具，不能宣称UDP通过。

### 实物复验步骤

1. 用户手动执行VS Code的STM32: Flash，确认Programming Finished/Verified OK；助手当前不写Flash。
2. 用SerialWeb连接115200、8N1，复位后确认BOOT中的FW=SBCG-M1.2-INT、HCLK_HZ=200000000及当前板UID。
3. 回传INIT=OK、PHY_ADDR=0、ID=0007/C131、至少10条连续心跳且TX_ERRORS=0；插拔网线后观察DOWN/UP，不应持续出现INIT=NOT_READY。
4. 在独立测试网络中把电脑有线IPv4设为192.168.10.100/24，网关/DNS留空，保留原配置以便恢复。连接当前板后执行`ping -n 4 192.168.10.1`，回传真实结果，以检查ARP/ICMP及主循环帧处理。ping通过仍不等于UDP应用已实现。
5. 按AGENTS.md第19节，收到新版本实物结果后才做本集成增量的功能提交。第8节已满足基本验收；新版插拔恢复等未测项单独保留，不把旧固件结果移植过来。此处停止，不自动开始UDP业务。

## 8. 用户实物验收（2026-10-19）

证据来自用户在会话中提供的SerialWeb串口文本和Windows ping输出；助手未直接操作板端、未烧录、未读取本版本Flash。会话内容转录及解析摘要保存在本机忽略目录`Logs/M1_2_Integration/user-validation-20261019.txt`、`user-validation-20261019.json`。串口中的`RX`为工具显示前缀。

| 检查项 | 用户实际输出与判定 |
| --- | --- |
| 固件与时钟 | FW=SBCG-M1.2-INT，HCLK_HZ=200000000 |
| 当前板UID | W0/W1/W2=001A002D/30365105/36363631，与既有单板记录一致 |
| PHY识别 | INIT=OK，PHY_ADDR=0，PHY_ID1=0007，PHY_ID2=C131，AUTONEG=1 |
| 链路 | 启动DOWN，随后UP至100 Mbps全双工，后续两次状态刷新仍为UP；未见持续NOT_READY |
| 心跳 | SEQ=1～12，TICK_MS=1016～12016；11个相邻差均为1000 ms；TX_ERRORS全为0 |
| ICMP | `ping -n 4 192.168.10.1`：4发4收，0%丢失，32字节，TTL=255 |
| 往返时间 | 四次为5 ms、3 ms、<1 ms、<1 ms；Windows汇总最短0 ms、最长5 ms、平均2 ms |

**判定：本次LwIP集成修复的基本实物验收通过。** 新版本能够启动并处理基本ARP/IPv4/ICMP收发，主循环心跳在这段约12秒记录内正常。启动DOWN后协商至UP属于本次实际观察到的过程。

实测范围不包含新版拔线再插入、长期运行、网络压力、UDP应用、第二板通信、PTP、音频或测距。ping往返时间不能作为同步误差、单向延迟或测距精度。未提供本版本烧录器Verified OK或Flash回读结果，版本对应关系依据BOOT标识和当前构建记录，不能声称设备Flash与所列ELF逐字节一致。

本增量提交同时包含用户已生成的`.mxproject`、`SBCG.ioc`、`Core/Inc/stm32f7xx_hal_conf.h`、`Core/Src/main.c`、`Core/Src/stm32f7xx_hal_msp.c`、`cmake/stm32cubemx/CMakeLists.txt`、`LWIP/`及`Middlewares/Third_Party/LwIP/`，保证队友能从源码复现。除ethernetif.c的上述USER CODE扩展外，修复未改写用户生成配置；I2C3器件通信仍未验证。

下一独立目标仍是当前板向电脑每秒发送带版本、序号和长度的UDP消息并核对接收；本次到集成验收和提交为止，不自动实施UDP或M2。
