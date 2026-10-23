# M2.1.3 UDP应答的TX硬件时间戳设计

<!-- doc-status: archived-plan; reviewed: 2026-10-23 -->

> **已完成的历史方案，请勿重复实施。** 仅保留当时范围、授权和生成检查；其中“当前/下一步/待验”均按原日期理解，不构成新授权。验收见[对应报告](../Milestones/M2_1_PTP_TX.md)，现行入口见[文档导航](../README.md)。

日期：2026-10-20。设计基线6ea7a37，实施基线99b787d。用户已明确批准本方案，并单独授权本固件烧录与自动验收。SBCG-M2.1-TX通过109项主机检查、两配置构建/布局、实际再生成、只读复核及实物验收：30组唯一RX/TX/ACK，70包零缺口，91条PTP/心跳，错误和溢出0。仍为LOCAL/SYNC=0，见[本次记录](../Milestones/M2_1_PTP_TX.md)。

## 一个独立目标

为电脑每秒一次的30个UDP请求，记录当前板对应应答帧的MAC发送硬件时间，与原请求序号、客户端tick及UID关联；保持已验收RX事件、应答、5005发送和串口心跳正常。仍使用原28字节type=2请求/type=3应答，不新增网络协议或主机工具。

本轮仅观察单pbuf、单DMA描述符的应答帧，固件标识为SBCG-M2.1-TX。TX/RX均为同一板的LOCAL时间、SYNC=0；不计算两板偏差、不校频、不做PPS、同步协议、音频或第二板。

## 路径选择

| 路径 | 取舍 | 本次选择 |
| --- | --- | --- |
| 用户linkoutput包装+BSP受控访问已完成描述符 | 保留生成代码和原发送流程，明确管理申请、读取、清理和失败归属 | 推荐 |
| 修改本机HAL的GetTxTimestamp及生成发送函数 | 可直接暴露HAL返回值，但扩大第三方/生成文件维护范围，并增加再生成风险 | 本轮不采用 |
| 自建发送路径或改为中断发送 | 同时改变缓冲所有权、完成回调及原网络时序 | 留待另行论证 |

## 已核对的本地事实

1. gnetif.linkoutput当前为生成的low_level_output，按pbuf链逐项建立ETH_BufferTypeDef后调用HAL_ETH_Transmit。当前函数忽略HAL返回值，正常返回ERR_OK；包装层不能以此单独认定物理发送成功。
2. HAL_ETH_PTP_InsertTxTimestamp给CurTxDesc指向的描述符设置TTSE。随后ETH_Prepare_Tx_Descriptors保留该位，并在准备完成后把CurTxDesc移到最后描述符；HAL_ETH_Transmit等待该最后描述符OWN清除，返回前将CurTxDesc推进一项。
3. 多pbuf链可能涉及多个描述符，而上述申请接口仅标记当前起始描述符。因此本步只对单pbuf且len=tot_len的合法应答申请时间戳；链式应答正常调用原发送函数，但记为UNSUPPORTED，不伪造TX观察成功。
4. 当前HAL_ETH_PTP_GetTxTimestamp使用releaseIndex读取DESC0/1，与同一驱动HAL_ETH_ReleaseTxPacket中读取DESC6/7的代码不一致，不能直接使用该接口。本步不修改或调用其错误读取路径，也不以调用ReleaseTxPacket代替轮询发送的缓冲管理。
5. [ST RM0385第38.6.7节](https://www.st.com/resource/en/reference_manual/dm00124865-stm32f7-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)的增强发送描述符定义：TDES6/7保存发送帧时间，必须属于LS末尾描述符。TTSS用于确认硬件已为本帧写入时间；不能只看到纳秒范围合法就当作本次捕获。

以上仅描述当前CubeF7 1.17.4源码，不推断其它版本具有同一问题。

## 接入与数据归属

初始化在现有PtpClock_Init、PtpRx_Init之后安装PtpTx_Init，传入App已有的真实板端UID，保存原gnetif.linkoutput并替换为用户包装函数。无生成代码修改。

发送链：UDPProbe.Receive → 原udp_sendto/ARP → 用户linkoutput包装 → 原low_level_output → HAL_ETH_Transmit → 用户BSP校验并复制TX时间 → 固定观察队列 → 主循环PtpTx_Process限速输出。

- 包装层每次进入都先尝试清理先前未完成申请，但仅可修改已归还CPU、且映射仍匹配当前ETH环的描述符。ARP、5005心跳等非目标帧继续原发送流程，不申请本步TX时间戳。
- 解析只识别本板5006→电脑192.168.10.100:6006的未分片IPv4 UDP、正确长度、封套、真实本板UID及type=3应答。应答28字节键含SEQ、客户端tick与UID，复制后与硬件样本一起排队，不持有pbuf。
- 对单缓冲目标帧，BSP先验证PTP模式、ETH状态、增强描述符、当前索引/地址映射、CPU所有权及PacketAddress为空，记录起始索引、帧地址、长度和发送前本地MAC时间。先清除上一帧残留TTSS，再使用HAL_ETH_PTP_InsertTxTimestamp设置TTSE。申请失败仍按原发送路径处理，单列ARM_ERR。
- 原发送函数返回后，BSP必须验证CurTxDesc恰好推进一个描述符、保存的描述符地址仍一致、DESC2/长度匹配本帧、FS/LS同时成立、OWN=0、ES=0、TTSS=1。再复制DESC7/6，检查纳秒范围及发送前≤TX时间≤发送后本地MAC时间。失败不输出有效时间，不拿当前MAC读时作替代，单列SNAP_ERR。
- 每个申请令牌只消费一次；复制必须发生在下一次linkoutput复用描述符之前。成功后仅在CPU拥有时清除TTSE，避免后续非目标帧继承申请。
- 如果返回时DMA仍拥有描述符，不清OWN、不清TTSE、不忙等；本帧记录失败并保留受控清理标记。后续入口/主循环只在OWN已清除且环映射一致时清理。不得写DMA正在使用的描述符。环映射变化或不可识别状态要显式报告，不能盲目沿用旧索引。
- 包装层保留原linkoutput返回值和pbuf生命周期，不重复发送、不分配网络缓冲。目标帧经过ARP排队时，只在真正到达linkoutput的那次调用申请并关联，不在udp_sendto入口预先假定描述符归属。

## 日志、缓冲与错误

固定4条CPU队列，每秒最多一条TX记录及一条累计统计；满时OVERFLOW增加，丢弃观察记录，网络继续。VALID表示通过全部硬件验证的样本数，包括因队列满未输出的样本；ARM_ERR、SNAP_ERR、UNSUPPORTED、OVERFLOW、INIT_ERR分别统计，正常验收均应0。

格式示例（非实测）：

```text
$PTP_TX,REQ_SEQ=1,CLIENT_TICK_MS=123456,SEC=7,NSEC=320100000,SYNC=0,SOURCE=DMA
$PTP_TX_STATS,VALID=30,ARM_ERR=0,SNAP_ERR=0,UNSUPPORTED=0,OVERFLOW=0,INIT_ERR=0
```

字段为显式uint32秒/纳秒和请求标记，使用160字节有界格式化。日志不在linkoutput、HAL等待或任何ISR内发送。新增串口观察可能增加软件RTT，必须如实记录；不把软件RTT、日志等待或本机TX−RX间隔宣称为网络传播延迟/同步精度。

## 拟修改文件

| 文件 | 原因 |
| --- | --- |
| App/BSP/bsp_ptp_tx.c、bsp_ptp_tx.h（新建） | 单描述符申请令牌、完成快照、边界检查和仅CPU拥有时的清理；复用现有BSP_PTP_Read验证本地时钟 |
| App/Sync/ptp_tx.c、ptp_tx.h（新建） | linkoutput包装、应答键解析、固定队列、计数与限速观察 |
| App/app_main.c | 初始化/主循环接入及SBCG-M2.1-TX标识 |
| CMakeLists.txt | 在用户拥有的LwIP分支注册新源码 |
| Tests/Ptp/test_bsp_ptp_tx.c、test_ptp_tx.c（新建）、run_tests.py | 真实HAL/CMSIS声明下的描述符归属/旧状态/失败清理及LwIP包装测试 |
| Tests/App/test_app_main.c | 接入顺序、版本及旧调度保持 |
| Docs/Plans/2026-10-20_PTP_TX_Design.md、2026-10-20_PTP_TX_Implementation.md | 设计复核与实施记录 |
| Docs/Milestones/M2_1_PTP_TX.md（新建） | 代码链路、复验操作和实测证据 |
| Docs/Project_State.md、Docs/README.md、README.md | 更新准确状态与阅读入口 |

不预先重构RX模块、原UDP协议或第三方驱动。若实现必须改变上述边界，先说明具体原因。

## STM32与CubeMX关系

No CubeMX change required。PTP本地时钟、TimestampAll接收配置及所有引脚、主PLL、NVIC、DMA映射/描述符数量/内存地址保持既有配置。新BSP仅在现有轮询发送路径按单帧申请TTSE、校验及读取已完成增强描述符，并受控清理申请。

用户已明确批准这部分用户BSP访问边界；不把本次批准扩大为任意描述符改写或第三方驱动修改权限。禁止修改生成初始化、HAL源码或.ioc。

无新增IRQ。沿用ETH内部帧DMA；单帧长度为实际完整应答帧长度，源是原发送pbuf，目标是现有MAC发送FIFO；无新增DMA1/2流、循环音频缓冲或半完成回调。完成仍由原HAL轮询OWN判断，PTP日志在主循环稍后处理。观察队列不与DMA共享。

I/D Cache及MPU保持原状态，不加Cache维护调用。对CPU/DMA交接采用必要的所有权检查和内存屏障，必须说明顺序，不能把Cache失效当作描述符已完成的证明。

## 测试与验收

1. 原生测试先观察失败，再实现：申请检查、索引回绕、正确DESC6/7来源、残留TTSS清除、HAL失败、OWN/ES/TTSS/FS/LS/地址/长度不符、未来/过早/越界时间、一次领取、失败后安全清理及DMA拥有时不写描述符。
2. 包装层测试：只匹配合法应答、ARP实际发送归属、链式应答显式UNSUPPORTED且原调用继续、非目标帧不继承TTSE、原返回值与pbuf所有权不变、固定队列溢出/排空、tick回绕、重复初始化及无原入口。
3. 运行全部既有RX/UDP/PHY/应用/Python测试，构建Debug/Release并检查ETH布局与公共宏；测试数量按实际结果记录。
4. 快照后实际Generate Code回归，保留LAN8742许可证并比较源码/配置/ELF哈希，不以纸面判断代替保留性检查。
5. 准备好已复核固件后，按项目规则取得该固件的烧录及自动验收授权，再用原ST-LINK配置写入、校验和复位，不改选项字节。
6. 同步采集COM3、70秒5005接收及每秒一次的30个请求：每个请求各有唯一匹配的RX记录、TX记录和ACK，UID/SEQ/tick一致；TX时间单调且不早于同请求RX时间，软件/观察错误与溢出0。至少60条本地PTP及心跳连续，正常5005窗口零缺口；保留末尾至少5秒排空日志。
7. 记录原文、哈希、实际RTT和验证边界；只读复核通过、实物验收通过后以jxq提交该功能并按既有协作授权推送。停止在TX观察，不继续同步算法。

已完成实现、主机及本版本实物验证。复核确认既有HAL超时会将DESC0覆盖为FS|LS：观察器拒绝缺少TTSS的样本，TTSE已撤销时不再写描述符；对应主机回归已通过。这不证明HAL超时或fatal DMA故障恢复。完整证据及复验步骤见[里程碑记录](../Milestones/M2_1_PTP_TX.md)。
