# M2.1.2 单板UDP请求的RX硬件时间戳

更新：2026-10-20。基线951ad78；用户已确认[RX优先设计及TimestampAll配置范围](../Plans/2026-10-20_PTP_RX_Design.md)。**SBCG-M2.1-RX通过92项主机检查、Debug/Release构建/布局、实际Generate Code回归，以及本固件授权烧录和串口/UDP联合实物验收。** 30个请求分别匹配唯一RX硬件时间和ACK，70个UDP包零缺口，98条PTP/心跳连续，错误和溢出0。只证明本板RX事件关联，仍为LOCAL/SYNC=0；此前M2.1.1、M1.4保留为各自旧固件证据。

## 独立目标与数据来源

电脑192.168.10.100:6006向本板192.168.10.1:5006每秒发送原28字节请求；在本板串口为每个合法请求输出其DMA捕获的MAC接收时间、序号与客户端tick标记。原type=2请求/type=3应答、5005逐秒发送及原本地PTP日志格式不变。

RX时间来自本机HAL_ETH_ReadData从最后描述符DESC7/DESC6复制的缓存，经HAL_ETH_PTP_GetRxTimestamp读取；它不是进入UDP回调后临时读取的MAC当前时间。当前MAC只用于核对事件时间不应晚于后读本地时间。本机[RM0385](https://www.st.com/resource/en/reference_manual/dm00124865-stm32f7-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)的增强描述符定义为依据，不套用H7上下文描述符规则。

## 文件与职责

| 文件 | 修改用途 |
| --- | --- |
| App/BSP/bsp_ptp.c、bsp_ptp.h | 通过已获授权的用户BSP HAL配置开启TimestampAll；读取RX缓存及验证模式/状态/时间范围 |
| App/Sync/ptp_rx.c、ptp_rx.h | 包装netif.input、建立同步请求上下文、4条固定队列及限速输出 |
| App/Network/udp_probe.c | 合法请求释放前领取一次时间戳记录，原应答继续 |
| App/app_main.c | 在本地PTP初始化后接入RX观察，循环最后限速输出；新固件版本 |
| CMakeLists.txt | 用户拥有的LwIP分支注册ptp_rx.c |
| Tests/Ptp/test_bsp_ptp.c、test_ptp_rx.c、run_tests.py | BSP状态检查、同步上下文与包解析/队列测试 |
| Tests/UdpProbe/test_udp_probe.c | 验证合法请求释放前通知；非法请求不生成有效记录 |
| Tests/App/test_app_main.c | 版本、初始化及主循环接入顺序 |
| Docs/Plans/2026-10-20_PTP_RX_Design.md、2026-10-20_PTP_RX_Implementation.md | 已确认设计与实施检查表 |
| Docs/Milestones/M2_1_PTP_RX.md、Docs/Project_State.md、Docs/README.md、README.md | 本次状态、证据边界与阅读入口 |

## 调用与所有权

初始化：main → MX_LWIP_Init → App_Init → PtpClock_Init/BSP_PTP_Init → PtpRx_Init保存并包装gnetif.input。

接收：MX_LWIP_Process → ethernetif_input → HAL_ETH_ReadData复制DMA时间 → 用户输入包装复制本帧请求键/缓存时间 → 原ethernet_input → UDPProbe.Receive校验合法请求 → PtpRx_Observe一次领取并入队 → 原UDP应答。原输入返回后上下文立即作废，不持有或释放pbuf。后续主循环PtpRx_Process每秒最多输出一条记录和一条累计统计。

完整28字节请求键包含序号、客户端tick和目标UID。仅观察发给本板的未分片IPv4 UDP请求，支持IPv4选项与链式pbuf；VLAN、片段、其它协议、错端口、非法长度/封套仍交给原网络输入，但不提供有效观察上下文，避免错误关联。NO_SYS同步交付是此设计前提，改RTOS或延迟投递必须重新核对。

队列容量4条，仅存时间和两个32位请求字段，无动态分配；满时累计OVERFLOW并丢弃观察记录，原网络应答继续。VALID是关联成功的硬件样本总数，含因队列满而未输出的样本；OVERFLOW单列。MISSING表示无可领取的匹配帧上下文，INVALID表示上下文匹配但硬件时间无效，INIT_ERR表示没有可包装的原接收函数。失败不以软件读时替代，也不触发ETH重置。

## STM32、CubeMX、IRQ与DMA

No CubeMX change required。仅用户BSP中的PTP接收时间戳开关改变：TimestampAll=ENABLE；coarse/十进制/5 ns增量、主PLL、引脚、NVIC、DMA配置和生成文件不改。读取前检查PTP门控/模式、增强描述符、CPU拥有的最后描述符及ES汇总；bit0在增强模式是扩展状态有效标志，不能套旧错误别名。损坏/越界时间不覆盖调用方输出。

无新增IRQ或回调ISR。HAL接收缓存与netif输入都由主循环服务；沿用ETH内部帧DMA和原4×1536接收池，不新增DMA1/2流或半完成/完成路径。新增观察队列只由CPU使用，不与DMA共享。I/D Cache及MPU保持原状态，无新增Cache维护。

## 主机验证

TimestampAll配置、RX有效缓存、netif包装、UDP通知及App接入均先观察对应测试失败，再实现。PTP BSP 8组、本地时钟5组、RX观察8组、应用5组、UDP应答7组、原发送6组、PHY双模式19组、Python34项，共92项通过。包括一次领取、相邻帧隔离、全请求键错配、错误/未来硬件时间、链式缓冲/IP选项、分片/VLAN、溢出/排空、tick回绕和重复初始化。主机测试替代硬件IO，不证明实物RX捕获已成功。

```powershell
python -B Tests/Ptp/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/UdpProbe/run_tests.py
python -B Tests/Udp/run_tests.py
python -B Tests/Network/run_tests.py
python -B -m unittest discover -s Tools/Monitor/tests -v
python -B Tools/Monitor/autodebug.py build
python -B Tools/Monitor/check_eth_layout.py
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

| 构建 | Flash占用 | ELF SHA256 |
| --- | --- | --- |
| Debug | 60616字节 | 9586d37bb6c6c8b304711061d581ea91a6d1d15ed361f7419192748e2032cb20 |
| Release | 38636字节 | 05ecd9297d675e3415ad02527f2995ba0f3552624776826df5baa63c40845183 |

初次报告：Logs/AutoDebug/20261020T083251-a89b6184/report.json、20261020T083251-e706ef0f/report.json。两配置布局通过，未发现新未解释诊断；RAM打印仍是含固定高地址段的跨度，不代表普通数据实际占用。实际Generate Code快照与比较位于Logs/M2_1_PTP_RX/regeneration：417个受检文件字节一致，CubeMX删掉的LAN8742许可证从快照原样恢复，无新增/缺失文件，.ioc和生成C源码均未变化。两配置再构建报告为Logs/AutoDebug/20261020T083502-10600144/report.json及20261020T083502-03229089/report.json，ELF哈希保持；各115个C单元共享PTP宏。只读复核未发现阻塞问题。

## 实物验收（2026-10-20）

用户明确授权本固件通过现有ST-LINK烧录并自动验收。先打开COM3 115200/8N1、DTR/RTS关闭记录，再核对Debug ELF哈希，使用.vscode/openocd-stm32f746.cfg及program {build/Debug/SBCG.elf} verify reset exit。一次烧录退出0，Programming Finished、Verified OK、Resetting Target齐全，无选项字节改动。采集包含烧录前旧固件残留，以下只统计唯一新BOOT之后，保留原文不删改。

| 项目 | 本版本实测 |
| --- | --- |
| BOOT | SBCG-M2.1-RX，HCLK_HZ=200000000，UID=001A002D:30365105:36363631 |
| RX请求关联 | REQ_SEQ=1～30各一条；CLIENT_TICK_MS逐条等于电脑请求标记；SYNC=0、SOURCE=DMA；秒/纳秒组合严格递增，纳秒范围合法且为5 ns倍数 |
| RX统计 | 最终VALID=30，MISSING/INVALID/OVERFLOW/INIT_ERR始终0；队列排空 |
| RX事件间隔 | 999848800～1003281240 ns，与每秒一次的主机请求合理一致，非独立测频证明 |
| 观察时间界限 | 每条RX时间均早于输出前最近的本地PTP读时；差值966243285～992209245 ns，反映限速日志等待，不能当作网络传播延迟 |
| 请求/应答 | 30发30收，序号、UID、来源及tick回显匹配；超时和错误项0；RTT最小/平均/最大0.426/4.770/14.113 ms |
| 板端原UDP统计 | 最终VALID=30、ACK_OK=30，BAD/FOREIGN/ACK_ERR/INIT_ERR均0 |
| 5005接收 | 70.001秒内收到70包，SEQ=8～77，缺口/重复/乱序/非法/外来0；69个板端tick差均1000 ms，与串口发送序号/tick逐条对应 |
| 本地时钟/心跳 | 各98条，SEQ=1～98连续；PTP为LOCAL/SYNC=0，DELTA_NS与相邻秒/纳秒逐条一致，ERRORS=0；心跳TX_ERRORS=0 |
| PHY | 启动协商后100 Mbps全双工UP |

实测首尾RX原文：

```text
$PTP_RX,REQ_SEQ=1,CLIENT_TICK_MS=7443715,SEC=7,NSEC=23831620,SYNC=0,SOURCE=DMA
$PTP_RX,REQ_SEQ=30,CLIENT_TICK_MS=7472741,SEC=36,NSEC=50232580,SYNC=0,SOURCE=DMA
```

本次软件RTT高于M2.1.1正常窗口；现有串口是主循环轮询发送，新增观察输出会占用网络服务时间，本次未隔离各延迟来源，因此不作精确因果归因。RX时刻取自DMA捕获而非日志输出时刻；本增量不验证软件RTT、端到端实时性或同步精度指标，串口输出的时延影响留作后续调度设计依据。

原始文件在忽略的Logs/UDP/，统一前缀m212-rx-1792485342324；计算记录在Logs/M2_1_PTP_RX/runtime-acceptance.json：

| 后缀 | 原始文件SHA256 |
| --- | --- |
| -serial.txt | c529d96b73cf7999dfe321295070bf68133e843af6ff78ec5644edc79556dcda |
| -flash.txt | a16cb66e4faa562440e28d5019261584cb1bdcd270ea7e8933210f148e0cbc8a |
| -heartbeat.jsonl | c5c8114a57830ea5833e1a08ea4df3e57b12e17ca941adea2a58021499b32432 |
| -probe.jsonl | c2ba1463e5bbc57c7490cac61e3d610bac5e9ad8793110a1c15efde9903b12ef |

工具原summary的hardware_verified=false保留，表示工具只判断网络包；本次实物结论由烧录/BOOT、串口RX记录及请求/应答联合核对得出。当前正常窗口不覆盖拔插、冷启动、长期压力、VLAN或分片实物注入；相应防错行为仅有主机测试。

## 复验步骤

使用现有ST-LINK/OpenOCD配置program verify reset，不改选项字节。采集COM3或[SerialWeb](https://conductance-lab.xyz/SerialWeb/) 115200/8N1；观察新BOOT、至少60条PTP与心跳，保持网线连接且不复位。

同时运行原工具，使用独立日志名；最后请求结束后保持串口采集至少5秒，让观察队列排空：

```powershell
python -B Tools/Monitor/udp_monitor.py --uid 001A002D:30365105:36363631 --duration 70 --log Logs/UDP/m212-rx-heartbeat-01.jsonl
python -B Tools/Monitor/udp_probe.py --uid 001A002D:30365105:36363631 --count 30 --log Logs/UDP/m212-rx-probe-01.jsonl
```

输出格式示例，非实测：

```text
$PTP_RX,REQ_SEQ=1,CLIENT_TICK_MS=123456,SEC=7,NSEC=320000000,SYNC=0,SOURCE=DMA
$PTP_RX_STATS,VALID=30,MISSING=0,INVALID=0,OVERFLOW=0,INIT_ERR=0
```

必须30个请求各有且仅有一条键匹配的RX记录，并有30个匹配ACK；事件时间单调、纳秒范围正确、错误和溢出0。正常5005观察区间至少30包零缺口；PTP状态和心跳连续且错误0。保存原文、ELF及日志哈希，再联合核对。仍为LOCAL/SYNC=0；不宣称TX时间戳、两板同步精度、独立测频、冷启动、拔插恢复或长期压力通过。
