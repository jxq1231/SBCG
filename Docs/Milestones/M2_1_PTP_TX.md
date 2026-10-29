# M2.1.3 单板UDP应答的TX硬件时间戳

<!-- doc-status: completed-evidence; reviewed: 2026-10-23 -->

> **已归档的增量验收记录。** 仅证明本页所列固件、窗口和测试范围；不是当前待办。后续进展以现行状态页为准，旧版本的未测项和失败不改写为已通过。 当前配置/任务见[项目状态](../Project_State.md)，阅读范围见[文档导航](../README.md)。

更新：2026-10-20。基线99b787d；用户已明确批准[TX设计](../Plans/2026-10-20_PTP_TX_Design.md)，并单独授权本固件烧录及自动验收。**SBCG-M2.1-TX通过109项主机检查、Debug/Release构建与布局、实际Generate Code回归、只读复核及实物验收：30个请求各有唯一RX/TX/ACK，70个UDP包零缺口，91条PTP及心跳连续，错误与溢出0。** 本板仍为LOCAL/SYNC=0，未完成两板同步；旧RX版本的验收作为历史证据保留。

## 独立目标与文件

为电脑192.168.10.100:6006发往本板192.168.10.1:5006的30个逐秒请求，各关联一条单缓冲应答的MAC发送硬件时间；同时复验原RX、ACK、5005接收及串口心跳。网络仍用原28字节协议。所有时间均属于本板LOCAL时钟、SYNC=0，未实现两板同步或测距。

| 文件 | 用途 |
| --- | --- |
| App/BSP/bsp_ptp_tx.c、bsp_ptp_tx.h | 按帧申请TTSE、一次性令牌、读取DESC7/6及受控清理 |
| App/Sync/ptp_tx.c、ptp_tx.h | 包装linkoutput、应答键解析、4条CPU观察队列及限速日志 |
| App/app_main.c | 在PTP/RX初始化后接入TX，标识SBCG-M2.1-TX |
| CMakeLists.txt | 用户拥有的LwIP分支注册两个新源码 |
| Tests/Ptp/test_bsp_ptp_tx.c、test_ptp_tx.c、run_tests.py | 描述符、失败归属、解析、发送透明性及队列测试 |
| Tests/Ptp/stubs/main.h | 原生测试专用屏障替代与描述符写入计数，不进入固件 |
| Tests/App/test_app_main.c | 接入顺序、实际UID与固件标识检查 |
| Docs/Plans/2026-10-20_PTP_TX_Design.md、2026-10-20_PTP_TX_Implementation.md | 已批准设计与执行状态 |
| Docs/Milestones/M2_1_PTP_TX.md、Docs/Project_State.md、Docs/README.md、README.md | 验证证据、限制及阅读入口 |

## 调用链、所有权与错误

初始化：main → MX_LWIP_Init → App_Init → PtpClock_Init → PtpRx_Init → PtpTx_Init。TX包装保存原gnetif.linkoutput，不改生成接口。

发送：原UDPProbe.Receive → udp_sendto/ARP → 用户linkoutput包装 → BSP_PTP_TxBegin → 原low_level_output/HAL_ETH_Transmit → BSP_PTP_TxEnd → 固定队列 → 主循环PtpTx_Process。

仅对本板UID、正确源/目标IP与端口、未分片IPv4、正确长度及type=3应答申请。链式pbuf应答累计UNSUPPORTED并继续原发送，非目标流量不申请。ARP排队的应答以真正到达linkoutput时的帧为准。包装保留原返回值，不分配、持有或释放pbuf，不重复发送。

BSP先检查PTP模式、ETH状态、增强格式、环地址、当前索引、CPU所有权和空PacketAddress，再清旧TTSS、请求TTSE并保存地址/长度/索引及发送前MAC读时。结束时要求索引恰好推进一项、地址和长度匹配、FS/LS/TTSS均成立、OWN/ES均为0，读取DESC7秒/DESC6纳秒，并检查发送前≤样本≤发送后MAC读时。失败不覆盖输出，也不用当前读时替代硬件事件时间；令牌只可消费一次。

DMA仍持有时不写OWN或TTSE；清理标记留到后续入口/主循环，在环映射一致且OWN清除时处理。HAL超时分支会把DESC0覆盖为FS|LS，该状态没有TTSS，必须拒绝，且TTSE已清除时不再写该描述符。**不能把所有故障中的OWN=0都解释为DMA自然完成；本轮不证明HAL超时或fatal DMA故障恢复。** 生成low_level_output忽略HAL返回值属于既有行为，故观察器依靠完成状态和快照校验，不能只依据其ERR_OK。

队列容量4，只复制请求序号、客户端tick和硬件时间，不引用网络缓冲。每秒至多输出一条事件及一条累计统计。VALID包括因队列满而未输出的有效样本；OVERFLOW单列。ARM_ERR为申请失败，SNAP_ERR为完成快照/原发送返回失败或映射故障，INIT_ERR为缺失原入口/UID。正常窗口上述错误及UNSUPPORTED均应为0。

```text
$PTP_TX,REQ_SEQ=...,CLIENT_TICK_MS=...,SEC=...,NSEC=...,SYNC=0,SOURCE=DMA
$PTP_TX_STATS,VALID=...,ARM_ERR=...,SNAP_ERR=...,UNSUPPORTED=...,OVERFLOW=...,INIT_ERR=...
```

## CubeMX、IRQ、DMA与Cache

No CubeMX change required。未修改.ioc、HAL或生成源码。新增BSP仅在已批准范围内按帧申请及读取发送时间。GPIO、TIM、主PLL、NVIC、SAI、I2C、ADC、LTDC配置不变。

无新增中断关系；原ETH轮询发送及观察器都在NO_SYS主循环中执行，没有新增ISR日志或完成回调。沿用ETH内部帧DMA，源为原应答pbuf，长度为完整帧实际字节数，目标为MAC发送FIFO；无新增DMA1/2流、循环模式或半完成回调。TX描述符仍为0x2004C000起4项，RX为0x2004C0A0起4项；接收池仍为4×1536字节。

I/D Cache保持关闭，MPU不变，无新增Cache维护。内存屏障用于CPU/DMA所有权和写回读取顺序，不能替代Cache一致性或证明发送已完成。4条观察记录仅由CPU访问。

## 主机验证

BSP、观察器及App接入均先观察对应缺失行为断言失败，再实现。追加HAL超时测试先因多余描述符写入失败，再加入TTSE检查通过。原生测试替代寄存器IO和发送完成，不执行真实MAC/DMA，不能代替实物验收。

| 检查 | 通过数 |
| --- | ---: |
| PTP BSP / 本地时钟 / RX观察 | 8 / 5 / 8 |
| TX BSP / TX观察 | 9 / 8 |
| App / 请求应答 / UDP发送 / PHY双模式 | 5 / 7 / 6 / 19 |
| Python主机工具 | 34 |
| 合计 | 109 |

覆盖索引回绕、旧TTSS、空/超长缓冲、占用/映射/时钟异常、HAL申请失败、状态/地址/长度错误、过早/未来/越界时间、一次领取、DMA持有时不写、HAL超时残留、合法UID、IPv4选项、分片/VLAN/非目标/链式帧、延迟发送、原返回值及pbuf生命周期、队列溢出、tick回绕及重复初始化。

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

| 配置 | Flash字节 | ELF SHA256 |
| --- | ---: | --- |
| Debug | 63548 | d17578c1ca71589ee4ec1dd838df091b4025ee9f51790eb8d3da2f94c64736a5 |
| Release | 40416 | 0f78a4e56591d00a32e052af8dcf76a3c19148bf5d6c58a839ced53e7bc2f987 |

构建报告：Logs/AutoDebug/20261020T130111-2295c0a0/report.json及20261020T130111-8e57cb04/report.json。无新未解释诊断，两配置布局通过，各117个C单元统一HAL_ETH_USE_PTP。RAM打印317920字节含固定高地址段的跨度，不等于普通数据实际占用。

最终实际Generate Code回归：Logs/M2_1_PTP_TX/regeneration-final。426个受检文件字节一致，CubeMX删除的LAN8742许可证从快照原样恢复，无源码/配置差异。再生成后重新构建与布局通过，ELF哈希均保持；报告为Logs/AutoDebug/20261020T130208-9ccb0587/report.json及20261020T130208-cef051e0/report.json，摘要见regeneration-final/post-build.json。只读复核未发现新增阻塞问题。

## 实物验收（2026-10-20）

按用户对本固件的明确授权，先打开COM3 115200/8N1、关闭DTR/RTS并记录；再次核对Debug ELF及17个已复核变更文件的SHA256。使用原.vscode/openocd-stm32f746.cfg执行program {build/Debug/SBCG.elf} verify reset exit，一次烧录退出0，Programming Finished、Verified OK、Resetting Target齐全，没有改选项字节。串口总采集110秒后正常关闭，UDP工具均正常结束，板端保持运行。

新BOOT前89行是旧固件残留，原文保留，联合检查只统计唯一新BOOT之后；无不完整尾行，无再次启动。主机工具原summary保持hardware_verified=false，联合验收通过版本哈希、烧录日志、新BOOT、真实UID、串口和网络记录另行建立证据。

| 检查 | 本版本实测 |
| --- | --- |
| BOOT | SBCG-M2.1-TX，HCLK_HZ=200000000，UID=001A002D:30365105:36363631 |
| RX/TX关联 | 两路各30条，REQ_SEQ=1～30各出现一次，CLIENT_TICK_MS与电脑请求及ACK逐条一致，SYNC=0、SOURCE=DMA |
| 时间范围 | 纳秒均合法且为5 ns倍数；两路时间分别严格递增，全部TX≥同请求RX；每条事件时间不晚于输出前最近的本地PTP读时 |
| TX−RX | 最小/平均/最大131280 / 137457.167 / 142955 ns，仅本板MAC事件间隔 |
| RX事件间隔 | 999575320～1001699080 ns |
| TX事件间隔 | 999574205～1001701920 ns，与每秒主机请求一致，不是独立测频证明 |
| 时间与日志 | 事件早于输出前最近本地PTP读时：RX为417878125～428786005 ns，TX为417738965～428650085 ns，包含观察队列等待，不是网络传播延迟 |
| RX累计统计 | 最终VALID=30；MISSING/INVALID/OVERFLOW/INIT_ERR始终0，队列排空 |
| TX累计统计 | 最终VALID=30；ARM_ERR/SNAP_ERR/UNSUPPORTED/OVERFLOW/INIT_ERR始终0，队列排空 |
| 原请求/应答 | 30发30收；来源、UID、序号、tick回显匹配；超时、发送错误、非法、外来及非预期应答0；软件RTT最小/平均/最大0.441 / 0.750 / 4.735 ms |
| 板端请求统计 | VALID=ACK_OK=30；BAD/FOREIGN/ACK_ERR/INIT_ERR始终0 |
| 5005接收 | 70.001秒内70包，SEQ=7～76，缺口/重复/乱序/非法/外来0；69个tick间隔均1000 ms，并与串口RESULT=OK序号/tick逐条匹配 |
| 启动链路 | 协商前时隙1、2为LINK_DOWN，LINK_SKIPS最终2；发生在电脑观察窗口之前，与窗口零缺口分别记录 |
| PTP/心跳 | 各91条，SEQ=1～91连续，相邻tick差1000 ms；PTP为LOCAL/SYNC=0，DELTA_NS逐条等于相邻MAC时间差，ERRORS=0；心跳TX_ERRORS=0 |
| PHY | 地址0，ID=0007/C131，协商后100 Mbps全双工UP |

首尾事件原文：

```text
$PTP_RX,REQ_SEQ=1,CLIENT_TICK_MS=23736216,SEC=6,NSEC=587292295,SYNC=0,SOURCE=DMA
$PTP_TX,REQ_SEQ=1,CLIENT_TICK_MS=23736216,SEC=6,NSEC=587428215,SYNC=0,SOURCE=DMA
$PTP_RX,REQ_SEQ=30,CLIENT_TICK_MS=23765227,SEC=35,NSEC=598624895,SYNC=0,SOURCE=DMA
$PTP_TX,REQ_SEQ=30,CLIENT_TICK_MS=23765227,SEC=35,NSEC=598764055,SYNC=0,SOURCE=DMA
```

原始记录前缀Logs/UDP/m213-tx-1792501619729，以下文件均本机保留并被Git忽略；联合验收摘要在Logs/M2_1_PTP_TX/runtime-acceptance.json。源码提交保存本节结论和哈希，不把原始运行日志加入Git。

| 文件后缀 | SHA256 |
| --- | --- |
| -serial.log | 04da669803b2c12adc7358302973e0b30602f11892932f284e23f5564999a6bc |
| -flash.log | 2125ed72820cc57fc2b77962c38e3b5fa9ef219674a9c2330fdb222d21158ec4 |
| -heartbeat.jsonl | 35a79bd3a9da6df098b99a5ad0628d23bddb2af2609922f807ad9d7c9d66ccdf |
| -probe.jsonl | dff25b57fd73192c44f5672db325f67cdcc4cac9e728236340eb65dbae35d175 |

## 复验步骤与边界

复验新一轮烧录时先核对当前ELF哈希及对应授权，再按上述原配置操作。要求Programming Finished、Verified OK、Resetting Target及新BOOT版本/UID匹配。

电脑有线网卡192.168.10.100/24，保持网线连接，关闭占用COM3的SerialWeb。两路UDP和串口同时采集，原始文件放Logs/UDP下的唯一前缀：

```powershell
python -B Tools/Monitor/udp_monitor.py --uid 001A002D:30365105:36363631 --duration 70 --log Logs/UDP/m213-heartbeat.jsonl
python -B Tools/Monitor/udp_probe.py --uid 001A002D:30365105:36363631 --count 30 --log Logs/UDP/m213-probe.jsonl
```

两条命令分别运行，串口覆盖烧录及其后至少100秒，保留末尾至少5秒排空观察队列。只统计新BOOT后的数据；前一固件残留原文保留但不混算。要求30个请求各有唯一匹配的RX、TX和ACK，SEQ/客户端tick/UID正确；纳秒合法、两路事件分别单调，TX不早于同请求RX且不晚于后读MAC时间，错误和溢出0。至少60条本地PTP和心跳连续，5005正常观察窗口零缺口；原主机summary的硬件状态不改写，另存联合验收与原文哈希。

RTT为电脑软件往返测量，TX−RX为本板MAC事件间隔，都不能当作两板同步误差。长期压力、链式TX时间戳、故障恢复、冷启动和独立测频不在本轮通过范围。本次实物验收通过后提交并停止本增量，不继续同步算法或第二板。
