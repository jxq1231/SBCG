# M2.1.2 接收硬件时间戳设计提案

日期：2026-10-20。基线：44fd676。状态：用户已确认方案及本增量TimestampAll配置例外；实现、92项主机检查、两配置构建/布局、Generate Code回归及只读复核通过；本固件已授权烧录并通过实物验收：30个请求各匹配唯一RX时间和ACK，70个UDP包零缺口，98条PTP/心跳连续，错误及溢出0。

## 一个独立目标

当前板收到电脑的每个合法UDP探测请求时，把该帧DMA捕获的MAC时间与请求序号、客户端tick标记对应，串口记录30个请求的硬件接收时间。保持现有28字节请求/应答、5005心跳及本地PTP日志正常。

本步不产生TX硬件时间戳，不计算两端时钟偏差，不修改本地时钟速率，不实现同步协议、PPS、音频或第二板。

## 路径选择

| 路径 | 收益与代价 | 选择 |
| --- | --- | --- |
| 先RX，后独立验证TX | RX已有HAL缓存及同步交付路径，先解决一帧与一个时间戳的归属问题 | 推荐本轮采用 |
| 先TX | 需要先处理本版本HAL接口与描述符字段不一致、发送成功与描述符归属问题 | 留待独立增量 |
| 同时实现RX/TX | 一次涉及两套描述符生命周期，失败时难以区分接收、发送与关联问题 | 本轮不采用 |

## 已核对的本地事实

- App_Init在MX_LWIP_Init之后执行；NO_SYS=1，ethernetif_input在主循环逐帧调用netif->input，当前为ethernet_input。
- 本机HAL_ETH_ReadData在最后一个接收描述符上复制DESC7为秒、DESC6为子秒，之后返回pbuf；HAL_ETH_PTP_GetRxTimestamp只返回这份缓存。它不会自动把时间戳附着到UDP请求上，也不能仅凭HAL_OK就证明本包有有效时间戳。
- 本机HAL默认启用增强描述符，当前32字节硬件描述符及额外软件字段已纳入原内存布局检查。本步应检查配置满足要求，而非改写DMA配置。
- HAL_ETH_PTP_GetTxTimestamp当前读取DESC0/1，与同一驱动发送回收路径的DESC6/7不一致。该发现仅来自当前版本源码比较；不修改第三方驱动，也不推断其它版本都存在相同问题。
- 当前PTP配置TimestampAll关闭。普通SBCG请求不是IEEE1588事件报文，不能假定现配置会为其产生接收时间戳。
- [ST RM0385](https://www.st.com/resource/en/reference_manual/dm00124865-stm32f7-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)第38.6.8节、增强接收描述符说明：最后描述符的RDES6/7保存该帧时间；两字段全1应视为损坏。该器件的状态字段必须按RM0385解释，不能套用STM32H7的时间戳上下文描述符或丢弃标志。

## 接入设计

1. BSP_PTP_Init继续是唯一PTP配置入口，保持200 MHz、coarse、十进制纳秒与5 ns增量，仅将TimestampAll设为ENABLE。配置仍在启动阶段执行，不在收包中重新初始化时钟。新增读取接口返回HAL缓存的秒/纳秒及本包最后描述符状态。
2. 新建用户模块ptp_rx，在PTP初始化后保存gnetif.input原函数并安装包装函数。不改ethernetif.c、lwip.c或HAL驱动，也不移动原有网络处理顺序。
3. 包装函数在调用原接收函数前复制该帧时间戳及状态，先清除上一帧上下文。只为本板IPv4、未分片UDP、电脑192.168.10.100:6006到本板5006、长度及封套正确的探测帧建立上下文；解析使用pbuf有界复制，支持pbuf链，不依赖结构体布局。
4. 本轮不为VLAN、IP分片重组或其它协议提供有效时间戳；这些报文仍交给原接收函数处理，上下文无效。不能把最后一个IP分片的时间误标为完整请求的接收时间。
5. 原ethernet_input同步调用现有UDPProbe接收回调。回调完成来源、长度、UID及封套校验后，在释放请求前，使用完整28字节请求键核对当前帧上下文并复制时间戳记录。键包含序号、客户端tick和UID；记录只可领取一次。原输入函数返回后上下文立即失效，后续帧不可复用旧值。
6. 无效时间戳不阻止原UDP应答，但计入独立错误，不能用回调时读到的当前MAC时钟替代硬件接收时间。有效记录写入4条固定容量队列，不持有pbuf、不动态分配内存；队列满时计数并丢弃观察记录，不阻塞网络。
7. App_Process在网络处理之后调用PtpRx_Process，每秒最多输出一条记录或汇总。正常验收请求速率为每秒一次，采集时在最后请求后继续等待队列排空。突发超量记录必须显示丢弃计数，不悄悄覆盖。

初始化链：main → MX_LWIP_Init → App_Init → PtpClock_Init → BSP_PTP_Init → PtpRx_Init。

接收链：MX_LWIP_Process → ethernetif_input → HAL_ETH_ReadData → netif.input包装 → 原ethernet_input → UDPProbe.Receive → 关联并入队 → PtpRx_Process限速输出。

输出提议：$PTP_RX,REQ_SEQ=<uint32>,CLIENT_TICK_MS=<uint32>,SEC=<uint32>,NSEC=<uint32>,SYNC=0,SOURCE=DMA。另有每秒限速的累计VALID/MISSING/INVALID/OVERFLOW统计。秒、纳秒均为本板MAC本地时间；客户端tick只是原请求的回显标记。字段使用32位格式化，160字节有界行；固件标识改为SBCG-M2.1-RX。

## 有效性与所有权

- 必须处于已配置PTP、专用时钟门控开启、coarse/十进制模式、正确子秒增量且TimestampAll开启的状态。
- 必须确认增强描述符启用，缓存状态为CPU拥有的最后描述符，且RDES0的ES错误汇总位为0；增强模式下bit0表示扩展状态有效，不能直接套用旧别名把它当接收错误。不把HAL句柄的pRxLastRxDesc字段误认为描述符地址，该字段在本版本实际保存状态字。
- 拒绝全1损坏时间戳、纳秒越界、配置丢失与HAL失败；校验采样不晚于随后读取的本地MAC时间。只读当前MAC时间用于合理性核对，不代替帧时间戳。
- 本机NO_SYS同步调用关系是上下文有效期的前提；将来若改RTOS、延迟投递或更换驱动，必须重新设计归属，不能沿用这一假设。
- 原输入返回值及pbuf释放职责保持原样；包装层不持有或释放网络缓冲。协议仍由LwIP解析，应用仍决定请求是否合法。

## 拟修改文件与原因

| 文件 | 原因 |
| --- | --- |
| App/BSP/bsp_ptp.c、bsp_ptp.h | 唯一PTP配置入口开启RX时间戳；读取与校验缓存/状态 |
| App/Sync/ptp_rx.c、ptp_rx.h（新建） | 接收入口包装、请求关联、固定队列与限速日志 |
| App/Network/udp_probe.c | 合法请求释放前记录关联结果，保持应答格式 |
| App/app_main.c | 初始化/主循环接入及新固件版本 |
| CMakeLists.txt | 在用户拥有的LwIP分支注册新模块 |
| Tests/Ptp/test_bsp_ptp.c、run_tests.py | 新配置与缓存有效性失败路径 |
| Tests/Ptp/test_ptp_rx.c（新建） | 同步上下文、链式pbuf、分片拒绝、错包/旧值防护、队列所有权 |
| Tests/UdpProbe/test_udp_probe.c、run_tests.py | 原应答行为与时间戳通知顺序、无效请求不产生有效记录 |
| Tests/App/test_app_main.c | 初始化及处理顺序、版本标识 |
| Docs/Milestones/M2_1_PTP_RX.md（新建） | 操作、代码链路与实际验收证据 |
| Docs/Project_State.md、Docs/README.md、README.md | 实施/验收后更新准确状态与入口 |

Tools/Monitor/udp_probe.py沿用，无需新增报文或主机依赖。若实现发现需要新增其它文件或生成代码例外，先说明原因与归属。

## STM32资源与CubeMX边界

涉及ETH的PTP接收过滤/时间戳功能及既有内部帧DMA；不更改GPIO、TIM、NVIC、DMA映射、ETH描述符数量/地址、RX池长度或主PLL。主循环轮询，无新增IRQ、DMA流、半完成/完成ISR；I/D Cache及MPU保持原配置，不新增Cache维护调用。

No CubeMX change required，前提是用户明确允许本增量继续通过用户BSP的HAL配置开启TimestampAll。上一增量的例外仅实现本地时钟，本提案不把它自动扩展为所有后续硬件配置权限。该开关不是新增外设或引脚，当前CubeMX界面也未找到完整PTP配置项；不选择RMII_PTP_Synchro来绕过限制，不手改.ioc。

## 验证顺序与通过条件

1. 先添加并观察失败的测试：TimestampAll配置；合法/损坏/缺失时间戳；秒/纳秒顺序；链式pbuf、IP选项/分片及非法长度；请求键错配；相邻帧与旧上下文隔离；队列满/排空及不改变pbuf所有权；原UDP应答不回归。
2. 最小实现，运行全部相关主机测试；Debug/Release构建及现有ETH布局检查通过，无新增未解释诊断。测试数量按实际运行记录，不预报固定总数。
3. 项目快照后实际Generate Code回归，核对用户模块、宏、入口、生成差异和许可证，再复跑构建/布局。保持.ioc只由CubeMX维护。
4. 准备好可审阅固件后，确认当前增量的烧录/验收授权，再用现有ST-LINK配置写入、校验并复位；不修改选项字节。旧M2.1.1烧录授权不在本提案中自动扩大。
5. 新BOOT后同步采集至少60条PTP/心跳、70秒5005接收，以及既有工具每秒一次的30个请求。每个请求必须有且仅有一条键匹配的有效RX记录，纳秒范围正确、事件时间单调且不晚于本地后读时间；MISSING/INVALID/OVERFLOW均0，30/30应答，正常UDP观察区间零缺口，心跳错误0。
6. 保存原文与哈希，区分实际通过和主机模拟；完成只读代码复核与差异检查，实物通过后以jxq提交本功能。到此停止，不自动进入TX或同步算法。

## 当前进度

- [x] 核对仓库基线、生成边界、HAL收发时间戳路径及RM0385描述符说明。
- [x] 提出RX优先设计与具体验收标准，完成范围、缓冲所有权和未测边界自检。
- [x] 用户复核本提案及本增量PTP配置例外。
- [x] 编写实施计划并执行测试、实现、构建、再生成回归与实物验收。

本文件记录已确认的设计；实物结论只按验收文档中的实际证据更新。
