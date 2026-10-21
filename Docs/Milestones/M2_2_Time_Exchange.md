# M2.2.2 单板UDP时间戳回报

设计与实施始于2026-10-20，交付状态更新于2026-10-21。状态：实现、177项主机检查、构建/布局、再生成回归、独立复核及本版本实物联合验收通过。当前板运行SBCG-M2.2-IO；仍为LOCAL/SYNC=0。

## 本轮目标

电脑发30次专用请求，本板正常应答，并随后回报每个请求RX与对应ACK TX硬件快照。电脑用完整会话键将它们与软件t1/t4配对，保留4个时刻的来源。原5005逐秒发送、5006请求/应答、串口心跳必须复验。

协议见[UDP协议](../Protocol.md)，批准范围见[设计](../Plans/2026-10-20_Time_Exchange_IO_Design.md)，执行状态见[清单](../Plans/2026-10-20_Time_Exchange_IO_Implementation.md)。原始测试日志保存在gitignored的Logs/M2_2_Time_Exchange。

## 文件与调用关系

本增量涉及以下18个源文件、测试及文档：

- App/Network/udp_config.h
- App/Network/udp_time_exchange.c
- App/Network/udp_time_exchange.h
- App/app_main.c
- CMakeLists.txt
- Docs/Milestones/M2_2_Time_Exchange.md
- Docs/Plans/2026-10-20_Time_Exchange_IO_Design.md
- Docs/Plans/2026-10-20_Time_Exchange_IO_Implementation.md
- Docs/Project_Roadmap.md
- Docs/Project_State.md
- Docs/Protocol.md
- Docs/README.md
- README.md
- Tests/App/test_app_main.c
- Tests/TimeExchange/run_tests.py
- Tests/TimeExchange/test_udp_time_exchange.c
- Tools/Monitor/tests/test_time_exchange_probe.py
- Tools/Monitor/time_exchange_probe.py

```text
App_Init → UDPTimeExchange_Deinit → 原UDP/PTP初始化 → UDPTimeExchange_Init
MX_LWIP_Process → 新Input → 原PtpRx Input → ethernet_input → 新Receive → ACK
ACK → 新Output → 原PtpTx Output → low_level_output → HAL_ETH_Transmit
App_Process → UDPTimeExchange_Process → REPORT及限速统计
```

硬件快照通过既有BSP_PTP_ReadRx / Read及TxBegin / TxEnd取得。无有效完整配对时不得填充软件读时或发送有效REPORT。4条CPU记录不持有pbuf，2000ms到期；重复/冲突键不覆盖，Process每轮至多发送一条REPORT。

## CubeMX、IRQ、DMA及Cache

No CubeMX change required。不改变.ioc、Core、HAL、生成网络胶水、生成CMake、旧PtpRx/PtpTx或BSP。没有新中断关系；Raw API回调在NO_SYS主循环执行。

使用既有ETH内部双向帧DMA，不新增DMA1/2流或完成回调。TX/RX描述符4×40字节位于0x2004C000 / 0x2004C0A0，接收池4×1536；ACK仍为原PBUF_RAM，BSP按实际帧长验证，只有新ACK按帧申请TTSE。REPORT不申请时间戳。CPU只保存时间副本，不保留DMA缓冲。

当前I/DCache未启用、MPU不变，原生成RX维护保留，不新增缓存维护调用。LwIP UDP PCB默认4个，本服务加原2个共3个；固定堆及链接保留窗不变。

## 复验步骤

主机检查使用各Tests/*/run_tests.py及Tools/Monitor/tests的unittest discovery；Debug/Release沿用autodebug.py build与check_eth_layout.py。构建只证明主机编译、链接及布局，不证明MAC捕获通过。

获得具体新ELF授权后才使用原ST-LINK配置program verify reset，不修改选项字节。联合运行串口COM3 115200 8N1、5005接收70秒、旧工具30请求及以下新工具；保存每次独立原始日志：

```powershell
python -B Tools/Monitor/time_exchange_probe.py --uid 001A002D:30365105:36363631 --count 30 --log Logs/UDP/m222-time-exchange.jsonl
```

要求唯一新BOOT、真实UID、新30组完整键各有唯一ACK/REPORT，t4≥t1、板TX≥RX且两路板时间分别递增；原30次请求/应答全部匹配、5005窗口零缺口、至少60条PTP/心跳连续，所有异常及溢出计数0。留至少5秒排空观察。电脑工具的hardware_verified保持false，联合分析另建立实物证据。

## 验证结果

177项主机检查全部通过：应用9、PHY19、逐秒UDP6、旧请求/应答7、原PTP38、估算器13、新时间服务26、Python工具59（原34+新25）。估算器13组包含1044次真实函数调用、1000个物理模型；不将调用次数混作独立测试数。

新服务原生测试连接真实旧PtpRx/PtpTx，只替代底层IO，覆盖正常36/52字节消息、IPv4选项/链式RX、完整键、重复/冲突、4条容量、2000ms到期/回绕、无效/未来/倒退时间、一次领取、ARP延迟发送、链式ACK、原返回值、引用释放、资源失败恢复、重试/重复初始化/入口归属与链路等待。电脑测试包含真实本地回环socket、REPORT先到、首个有效ACK取时、错键及缺项、JSON大整数。日志见Logs/M2_2_Time_Exchange下各回归文件。

RED证据：firmware-red.log为服务空实现运行断言失败；firmware-link-red.log为补测断链回报时的预期失败；host-red.log为电脑工具空实现的断言失败；app-red.log为缺少Deinit顺序的运行断言失败。对应实现后GREEN及全回归通过。主机测试没有访问开发板。

| 配置 | 构建/布局 | Flash加载量 | ELF SHA256 |
| --- | --- | --- | --- |
| Debug | PASS，无构建诊断 | 67696字节 | 807cbc9f708b3c1f2ca84f0f350cbb5efc4ab7de496178d0784f6fbd5006e165 |
| Release | PASS，无构建诊断 | 43084字节 | 3e3773515b4a7ab83b06b16ca7084e1fdae195a9562dcbd83f6ca532a29d2116 |

两配置119个C编译单元均带统一HAL_ETH_USE_PTP，新服务各编译一次。Debug报告Logs/AutoDebug/20261020T143206-d4bb5f5a/report.json，Release报告20261020T143206-943f0714/report.json；摘要为Logs/M2_2_Time_Exchange/build-verification.json。MSP、固定LwIP堆及ETH区域布局检查通过。共享日志入口允许255字节；BOOT/心跳仍只分配160字节局部缓冲，新统计复用一个256字节缓冲，避免同时保留两行。

实际Generate Code退出0，444个受检文件字节一致、无新源码；CubeMX删除的Drivers/BSP/Components/lan8742/LICENSE.txt从生成前快照原样恢复。再生成后重新构建/布局通过，未手改生成文件。证据为Logs/M2_2_Time_Exchange/regeneration/comparison.json。相对任务起点的342个受保护硬件/生成/工具配置文件均保持原字节。

电脑工具与App的独立规格、质量复核通过，固件规格复核通过26组真实包装链测试；最终固件质量及整体集成复核通过，未发现本批准范围内的阻塞问题。

## 本版本授权烧录与联合实物验收（2026-10-21）

用户明确批准SHA256前缀807cbc9f708b对应的SBCG-M2.2-IO烧录及自动验收。通过原ST-LINK配置仅执行一次`program {build/Debug/SBCG.elf} verify reset exit`，返回0，观察到Programming Finished、Verified OK、Resetting Target；没有修改选项字节、调试配置或硬件生成配置。

COM3按115200 8N1、DTR/RTS=0同步采集，随后启动5005接收70秒、旧6006请求30次、新6007时间交换30次；总串口观察约90秒，给最后请求留出充分排空时间。捕获线程与工具均已正常关闭，板保持运行。

| 检查 | 实测 |
| --- | --- |
| 启动/身份 | 唯一SBCG-M2.2-IO BOOT，HCLK200000000，UID001A002D:30365105:36363631；无预BOOT行或截断尾行排除 |
| 新专用服务 | 30请求、30ACK、30REPORT、30four_stamp_sample；SESSION=9C4A07F2A52542DE，SEQ1～30，完整键一一对应 |
| 时间来源 | t1/t4为PC_SW，t2/t3为BOARD_HW；JSON纳秒字符串完整，t4≥t1、TX≥RX，两路板时间分别递增且为5ns刻度 |
| 原5006服务 | 30发30收，30条旧RX和TX与旧ACK的SEQ/tick各唯一匹配，异常0 |
| 原5005观察窗 | 70秒实收70包，SEQ6～75，丢失/重复/乱序/非法/外来均0，69个tick间隔均1000ms |
| 串口/PTP | 90条心跳与90条LOCAL/SYNC=0 PTP状态连续，所有观察错误和溢出0 |
| 链路 | 启动DOWN后UP至100Mbps FULL，之后没有DOWN；启动SEQ1为LINK_DOWN本地跳过，在5005观察窗之外 |

最终TS_IO为REQ_OK=30、ACK_OK=30，其余项0；TS_PAIR为RX_OK=30、TX_OK=30、REPORT_OK=30，其余项0。原UDP_RX、PTP_RX_STATS、PTP_TX_STATS各VALID=30，原ACK_OK=30，全部异常字段0。所有统计从启动到观察末尾均未出现非零错误或溢出。

新服务电脑软件RTT最小/平均/最大0.516000 / 0.658683 / 1.116300 ms；本板ACK TX−请求RX为180320 / 309990.667 / 370880 ns（最小/平均/最大）。旧请求软件RTT为0.454 / 0.620 / 1.040 ms。它们是本次并行采集条件下的往返和处理时间，不能当作同步误差、频率准确度或测距精度。

原始前缀：Logs/UDP/m222-io-1792561420906。运行命令及授权记录：Logs/M2_2_Time_Exchange/runtime-run.json。独立于工具summary的联合结论：runtime-acceptance.json，status=PASS、hardware_verified=true；三个工具的原summary均保留hardware_verified=false，不回写原始数据。

原始日志SHA256（原始数据按项目规则不提交Git）：

| 文件 | SHA256 |
| --- | --- |
| m222-io-1792561420906-serial.jsonl | eb9a5b5302757cd8768298bba9f43180e2f14a5b95e687247ab8d0b46bc17b20 |
| m222-io-1792561420906-monitor.jsonl | ba6e6b6ab4c47daa8aa373a53cc4bbb8316a6b89038ee41232a74738f36eaadc |
| m222-io-1792561420906-probe.jsonl | bdb2b8e60629175d37f75c5dc8bac2eb962eef2eb8c5b6a28352c3f34313e214 |
| m222-io-1792561420906-time.jsonl | 3ffda33be646e91327913e68fdcf2191c983c69ac7c531c6762738bbad9ae53b |
| m222-io-1792561420906-flash.log | 361c78ce615ef8feceb0a3ccaa37468c31c3c6cdd4c61f8b0e7c1a164499d03e |
| m222-io-1792561420906-serial.bin | 9beed2b9d2606157f5f29a11a88d54bdb61aece2e61a1f94de9624f6fef46c05 |

独立运行证据复核通过：6份原始证据哈希一致，serial.bin与801条JSONL逐行一致，无截尾或丢弃；烧录前10份实现/工具/测试文件哈希与实际运行版本吻合。未通过再次烧录或重采样替换本窗口。

实物通过后按jxq提交此独立功能，Git交付以本次提交和远端核对为准。停止本增量，不自动进入校时或第二板联调。

本轮始终LOCAL/SYNC=0，不调用四时间戳估算器，不调整时钟、不检查第二板。PC软件时刻不是网卡硬件时刻，软件RTT及板ACK处理时间不等于同步误差或测距精度。
