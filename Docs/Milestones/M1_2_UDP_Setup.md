# M1.2 当前板到电脑的UDP：CubeMX准备

更新：2026-10-18。起点：`3807edd`，M1.1当前板PHY识别和链路插拔观察已通过。本文仅完成M1.2.0配置准备；LwIP尚未生成、UDP应用尚未实现，不能按本文标题认定通信通过。

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
| LWIP_RAM_HEAP_POINTER（如显示） | 0x20048000 | F7数据库默认地址；**还没有被当前链接脚本安全保留** |
| MEM_LIBC_MALLOC / MEMP_MEM_MALLOC | 0 / Disabled | 先保留LwIP内部堆/池分配方式 |
| CHECKSUM_BY_HARDWARE | 保留Enabled | 生成后检查软件校验宏与HAL校验功能一致，不另行手改宏 |

I/D Cache及MPU保持M1.1状态，不在此步开启Cache或复制H7的MPU/缓存维护方案。1536字节长度和模板中的对齐声明不证明所有实际缓冲地址都32字节对齐，后续必须检查接收池元素布局、CPU/DMA所有权；目前不添加随机Clean/Invalidate调用。

**重要内存边界**：现有包装链接脚本只保护两个描述符及其上方SRAM2区域，普通newlib堆仍可增长到约0x2004BC00。固定LwIP堆指针0x20048000位于这个范围内，单凭构建通过不会阻止两个堆重叠。LwIP的固定堆还可能不显示为ELF中完整占用段。生成后需扩展用户拥有的链接保留/检查策略，核实LwIP堆连同开销、Rx池、普通堆及栈；此处尚未改链接配置。不能把M1.1的ETH_LAYOUT_OK直接当作新增LwIP内存安全结论。

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

本步仅修改文档，没有新增IRQ、DMA启动或回调，调用链仍是M1.1。现有工程构建通过也只证明M1.1基线，不能证明尚未生成的LwIP或UDP功能。

本步检查：`python -B Tools/Monitor/autodebug.py build`及`python -B Tools/Monitor/check_eth_layout.py`通过，构建报告为`Logs/AutoDebug/20261018T153653-6808d332/report.json`；三份文档的相对文件链接检查通过。变更文件仅为本配置单、`Docs/README.md`及`Docs/Project_State.md`，未改.ioc、固件、构建配置或电脑网络。

等待用户经CubeMX生成的依据为根目录[AGENTS.md第4节](../../AGENTS.md)：硬件和生成配置变化须走CubeMX，生成后重新审查；不得手改.ioc或用手写初始化替代。用户完成生成后再继续M1.2代码工作，不自动推进到双板/PTP。

配置来源：本机CubeMX的`db/mcu/IP/LWIP-v2.1.2_Cube_Modes.xml`、`db/templates/ethernetif_f7.ftl`、`db/templates/lwip_c.ftl`及CubeF7 V1.17.4附带的LwIP头文件；具体生成输出优先于模板推断。协议API语义参考上文LwIP官方文档。
