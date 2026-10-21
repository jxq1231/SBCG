# M2.2.2 单板UDP时间戳回报与交换样本采集设计

日期：2026-10-20。基线943fdef，工作区干净。状态：用户已批准实施及本版本烧录，主机验证、构建/布局、再生成回归和30组样本联合实物验收通过（2026-10-21）。当前板运行SBCG-M2.2-IO。当前估算器已完成纯算法验收，方案提出时板端运行SBCG-M2.1-TX；本轮继续不检查第二块板。

## 一个可验收目标

电脑每秒向当前板发送一次专用时间请求，共30次。板端正常返回ACK，并通过单独REPORT回传该请求RX和对应ACK的TX硬件时间；电脑以完整会话键组合PC软件发送/接收时刻，保存30组来源明确、无错配的四时刻样本。同时复验原5005发送、5006请求/应答和心跳。

本增量打通时间戳数据链，不调用估算器校时，不输出SYNC=1、LOCK或同步精度结论。电脑的两个时刻是软件观察点，不是网卡硬件捕获；正式两板同步仍需另行实现并按课程条件测试。

## 方案选择

| 方式 | 作用及维护范围 | 本次安排 |
| --- | --- | --- |
| 新5007/6007端口及独立用户模块 | 新请求/ACK/时间回报带会话键；用外层包装调用原网络链，保留原5005及5006协议和工具 | 推荐 |
| 在现有5006/6006回显中增加时间戳 | 需改变原28字节封套、选择器及旧工具解码，或让旧工具收到不认识的额外报文 | 本轮不采用 |
| 直接实现双板IEEE1588协议与伺服 | 同时涉及双端角色、完整配对、校时/校频和收敛验证 | 后续独立设计 |

当前HAL轮询接口在ACK发送完成后才提供其TX硬件快照，因此本方案另发REPORT，不能把发送REPORT时临时读取的MAC当前时间填成ACK时间。现有BSP_PTP_TxBegin/End已能按实际帧长度校验完成描述符，本轮明确请求将其用于新type=5 ACK，仍不改BSP、HAL或生成源码。

## 新协议：明确宽度、字节序及身份

当前板192.168.10.1:5007，电脑192.168.10.100:6007；使用一个新板端UDP PCB。旧5005、5006/6006及type=1/2/3不变。当前MEMP_NUM_UDP_PCB由本机LwIP opt.h默认4提供，原两个PCB加本服务共3个，不需要改LwIP生成选项；实现时须再核对实际配置。

新消息沿用SBCG/version=1封套，但使用独立type和明确长度。所有跨网络的整数均为无符号、大端，不直接发送C结构体：

| 偏移 / 字节 | 字段 | 含义 |
| --- | --- | --- |
| 0 / 4 | MAGIC | ASCII SBCG |
| 4 / 1 | VERSION | 1 |
| 5 / 1 | TYPE | 4=TIME_REQUEST，5=TIME_ACK，6=TIME_REPORT |
| 6 / 2 | LENGTH | REQUEST/ACK为36，REPORT为52 |
| 8 / 4 | SEQ | 本次电脑运行内1开始递增的请求序号，0拒绝 |
| 12 / 4 | CLIENT_TICK_MS | 电脑t1软件时刻整毫秒的低32位，作为回显标记 |
| 16 / 12 | UID_W0/W1/W2 | 当前目标板96位UID；ACK/REPORT保持一致 |
| 28 / 8 | SESSION | 电脑每次运行生成的非零随机64位会话标记 |
| 36 / 4 | RX_SEC | 仅REPORT：该请求DMA接收时间的秒 |
| 40 / 4 | RX_NSEC | 仅REPORT：纳秒，范围0～999999999 |
| 44 / 4 | TX_SEC | 仅REPORT：对应ACK DMA发送时间的秒 |
| 48 / 4 | TX_NSEC | 仅REPORT：纳秒，范围0～999999999 |

ACK回显完整请求，仅将TYPE改5；长度仍36。REPORT复制同一键，TYPE改6、LENGTH改52，附加四个32位时间字段。默认20字节IPv4头时，ACK的完整以太网数据长度为78字节；BSP仍按实际pbuf长度验证，不硬编码帧长。

完整配对键为SESSION+UID+SEQ+CLIENT_TICK_MS。SESSION用于区分电脑工具的不同运行，不是板端启动纪元或认证凭据。板端重启/时间跳变不能仅靠该标记证明安全；本轮联合验收要求唯一新BOOT及连续状态，工具遇到板端时间倒退应标记REVIEW。

## 模块和调用链

新App/Network/udp_time_exchange.c/.h管理本端UDP PCB、专用输入/输出包装、固定记录和限速统计。使用既有BSP接口读取时间，不碰描述符寄存器、硬件配置或旧观察器源码。

初始化：App_Init先调用UDPTimeExchange_Deinit解除本模块上一次安装的包装和PCB，再执行既有初始化；在PtpClock_Init、PtpRx_Init、PtpTx_Init之后调用UDPTimeExchange_Init安装本模块包装。新增Init接收真实UID、当前tick、逻辑tick读取函数和日志函数；App提供已有BSP_Console_GetTickMs。Process接入主循环，版本标识拟为SBCG-M2.2-IO。

输入链：本模块Input → 原PtpRx输入包装 → 原ethernet_input → 新UDP Receive。输出链：本模块Output → 原PtpTx输出包装 → 原low_level_output/HAL_ETH_Transmit。

- Input仅为未分片IPv4、正确IP/端口、type=4/36字节的候选请求保存完整键及BSP_PTP_ReadRx快照，并以后读本地MAC时间检查上界；支持pbuf_copy_partial读取链式接收及IPv4选项。原LwIP仍负责校验和及正常交付，包装不绕过它。
- 新Receive校验来源、长度、封套、目标UID、非零SESSION及SEQ，再一次性领取本次Input上下文。只有上下文完整匹配且硬件时间有效时才建立RX待配对记录；无效时统计错误，仍可正常回ACK，但不得生成有效REPORT。
- Output只对新5007→6007/type=5/36字节ACK、且完整键匹配有效待配对记录时调用BSP_PTP_TxBegin → 原发送 → BSP_PTP_TxEnd。只观察单pbuf帧；链式ACK继续原发送但记UNSUPPORTED，不伪造时间。快照成功且TX≥RX后复制到记录，等待Process回报。
- 原PtpTx包装仅识别旧5006/type=3/28字节帧，新包装仅识别新5007/type=5/36字节帧；必须测试二者互斥，保证同一帧不会重复Begin。原包装在新令牌活动期间调用TxClean得到NOT_READY是预期，无额外写描述符；结束后仍由共享BSP规则清理。
- REPORT由Process发送，type=6不申请TX时间。Input/Output包装自身不发送REPORT、打印日志、额外分配网络缓冲或持有pbuf；正常ACK分配由Receive完成。UDP回调/Process的ACK/REPORT使用现有Raw API引用规则，分配、复制、发送失败均正确释放自己的引用。
- 包装保留原输入/输出返回值，收到非目标、VLAN或片段仍交原入口处理。上下文在原Input返回后失效，不把前一帧时间留给后一帧。
- Deinit仅在本模块仍拥有netif函数指针时恢复保存入口；清空本模块CPU记录、移除自己的PCB，不改其它模块。若已安装后的入口归属无法确认，记INIT_ERR并拒绝重新挂接，不能盲目保存未知包装链。重复Init及整个App初始化顺序必须测试，防止多个包装互相保存后形成递归。

## 固定缓存、失败和可观察行为

最多4条CPU记录，仅复制键、RX/TX时间及创建tick，不保留网络缓冲。状态为等待ACK时间、可回报、已处理；生命周期上限2000 ms，使用无符号tick差处理回绕。Process每轮最多发送一条REPORT，不忙等、不自动重发；串口累计统计最多每秒两行，使用有界缓冲和32位格式化，不重新引入64位printf问题。

同一未过期SESSION/UID/SEQ再次到达：完整键相同记重复并忽略；CLIENT_TICK_MS不同记冲突并忽略，不覆盖旧时间。处理后的键保留到原2000 ms期限作为短期重复标记；过期才释放。容量满时显式记OVERFLOW，ACK可以继续但不得错配REPORT。缺RX、TX申请/快照失败、链式ACK、过期及REPORT发送失败均有独立统计，不以软件读时补样本。

拟提供两组串口统计：TS_IO包含REQ_OK/REQ_BAD/FOREIGN/DUPLICATE/CONFLICT/ACK_OK/ACK_ERR/INIT_ERR；TS_PAIR包含RX_OK/RX_INVALID/TX_OK/TX_ERR/UNSUPPORTED/REPORT_OK/REPORT_ERR/EXPIRED/OVERFLOW。各用256字节缓冲，先核对最坏32位数值时长度；统计表示本地处理结果，REPORT_OK不单独证明电脑收到了数据。

初期PCB创建/绑定失败延后1000 ms重试，沿用当前网卡与静态IP。链路或资源失败不复位ETH、不改PTP模式、不覆盖旧PCB或改变原业务。正常验收后保留足够时间排空记录；已处理键自然过期清理不记EXPIRED，只有尚未完成的记录到期才计异常。

## 电脑工具及四个时刻的来源

新建Tools/Monitor/time_exchange_probe.py，原udp_probe.py保持不变。默认本地6007、目标5007、30次、间隔1秒、每次总等待0.8秒，UID必须明确指定；每次运行生成新非零SESSION。不更改电脑网卡、防火墙或系统时钟。

- t1_pc_sw_ns：构造并发送请求前读取time.monotonic_ns，CLIENT_TICK_MS由它导出。
- t4_pc_sw_ns：recvfrom返回后立即读取，只有来源/完整键正确的首个TIME_ACK才登记为t4，不能用REPORT到达时刻代替。
- t2_board_hw_ns及t3_board_hw_ns：仅由TIME_REPORT的RX/TX秒纳秒组合，不由PC模拟。
- ACK和REPORT可任意先后到达；只有本次键的二者都有效才输出一条four_stamp_sample。重复、迟到、错会话/UID/序号/tick、非法包、超时及缺REPORT均不确认本次样本，保留明确原因和计数。异常窗口summary为REVIEW，不靠收到任意应答宣称通过。
- 大纳秒值在JSONL中用十进制字符串，SESSION用16位十六进制字符串，UID仍为三个8位十六进制字；避免后续JavaScript查看器丢失大整数精度。每个时刻保留PC_SW或BOARD_HW来源标记。
- PC的t4≥t1、本板t3≥t2；同一运行中成功样本的两路板端时间应分别递增。禁止检查跨域t2≥t1或计算未经说明的UTC偏差。记录PC往返时长，既不在线调用估算器，也不输出LOCK或同步精度。

Python的[monotonic_ns文档](https://docs.python.org/3/library/time.html#time.monotonic_ns)规定这是整数纳秒的单调时钟接口；参考点未定义。本方案选择它保存电脑软件观察时刻，不将其当作MAC硬件时间戳或UTC。此阶段不实现NTP或完整IEEE1588协议。

## 文件范围与硬件关系

批准后计划涉及：

| 文件 | 用途 |
| --- | --- |
| App/Network/udp_time_exchange.c、udp_time_exchange.h（新建） | 独立服务、包装、键匹配、硬件快照配对、回报及统计 |
| App/Network/udp_config.h | 新5007/6007应用端口常量 |
| App/app_main.c | 明确Deinit/Init顺序、Process及SBCG-M2.2-IO标识 |
| CMakeLists.txt | 用户拥有的LwIP分支注册新源码 |
| Tests/TimeExchange/test_udp_time_exchange.c、run_tests.py（新建） | 新协议/缓存/生命周期/原始包装链的原生测试 |
| Tests/App/test_app_main.c | 新服务顺序、版本及原调度保持 |
| Tools/Monitor/time_exchange_probe.py、tests/test_time_exchange_probe.py（新建） | PC软件时刻、消息配对、异常及真实本地socket测试 |
| 本设计及后续Implementation清单 | 用户批准与实施轨迹 |
| Docs/Protocol.md、Docs/Milestones/M2_2_Time_Exchange.md（新建） | 新旧协议边界和实际验收证据 |
| Docs/Project_State.md、Docs/Project_Roadmap.md、Docs/README.md、README.md | 更新准确进度与阅读入口 |

No CubeMX change required。不改.ioc、Core、HAL、生成网络胶水或生成CMake；不改旧PtpRx/PtpTx模块、BSP_PTP模式、TTSE底层实现或数学估算器。GPIO/TIM/NVIC/SAI/I2C/ADC/LTDC/Cache/MPU不变。使用既有ETH内部帧DMA、描述符和收发池；ACK源为原PBUF_RAM，长度按实际帧，目标仍为MAC发送FIFO。无新增DMA1/2流、ISR或完成回调。

## 测试和实物通过标准

先观察失败再实现。新原生测试须覆盖封套/大端字段、完整会话键、零/错UID或SESSION、过期/重复/冲突/容量满、tick回绕、无效硬件时间、一次领取、ARP延迟发送、链式ACK、返回值/引用计数、错误后继续处理、PCB重试及Deinit/重复初始化。集成测试应连接真实旧PtpRx/PtpTx包装与新服务，仅替代底层IO，证明旧/新帧互斥捕获和无递归，不能只测试互不相干的空包装。

Python测试覆盖ACK/REPORT乱序、重复/迟到/缺项、错误来源/键/时间值、t4采样点、大整数JSON表示与本地socket往返。运行新测试及原122项检查，测试数按实数记录；完成Debug/Release构建、ETH布局、宏/PCB数量检查及只读复核。做实际Generate Code保留性回归并保留LAN8742许可证，不手改生成文件来通过测试。

可审阅的新固件准备好后，另取得该ELF的烧录及自动验收授权，才使用原ST-LINK配置program verify reset。联合采集COM3、70秒5005接收、旧工具30次5006应答和新工具30次时间交换；网线保持连接，不检查第二板。要求唯一新BOOT/真实UID、新30组完整键各对应唯一ACK/REPORT、四时刻来源正确、板端时间范围/顺序合法；新旧业务均无异常，5005窗口零缺口，至少60条PTP/心跳连续、观察错误及溢出0。

保存原文、ELF及日志哈希、实际软件RTT和丢失/重复统计；工具hardware_verified保持false，由独立联合验收建立硬件证据。串口打印或udp_sendto返回OK不能代替PC实收。确认本版本实物结果后，以jxq提交并按已有协作授权推送；停止在时间戳数据链，不继续校时、校频、PPS或第二板。
