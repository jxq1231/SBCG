# M1.3 电脑请求与板端UDP应答

更新：2026-10-20。基线`d09b3c3`，工作区开始时干净。独立目标为电脑每秒发一次请求，当前板校验并回复同一序号，保留已有5005端口逐秒观测。**M1.3已通过用户实物验收：电脑30发30收，序号、请求标记、UID及来源逐项匹配；串口最终VALID=30、ACK_OK=30，各项错误计数为0。** 主机71项检查及Debug/Release构建、布局检查通过。本次提交收录服务、工具、测试与验收文档，不涉及第二板、硬件同步、声音采集或LCD。

## 设计及实施范围

继续采用NO_SYS、LwIP Raw API和主循环。复用原5005端口会混合旧观测与应答，新建独立应用端口更容易分别验收；Socket/Netconn需要RTOS，本阶段不采用。板端使用192.168.10.1:5006，电脑使用192.168.10.100:6006。原板端5005→电脑5005保持。IPv4仍由CubeMX生成，应用端口不属于硬件配置。

| 文件 | 用途 |
| --- | --- |
| `App/Network/udp_probe.c` | 独立PCB、请求校验、应答及每秒汇总日志 |
| `App/Network/udp_probe.h` | 主循环初始化、处理及日志回调接口 |
| `App/Network/udp_config.h` | 新增应用端口配置 |
| `App/app_main.c` | 初始化与主循环接入，固件标识SBCG-M1.3-UDP |
| `CMakeLists.txt` | 在现有LwIP目标下注册新用户源码 |
| `Tests/UdpProbe/run_tests.py` | 使用真实LwIP头文件构建并运行原生测试 |
| `Tests/UdpProbe/test_udp_probe.c` | 替代网络IO，验证初始化、链式pbuf、校验、内存释放和故障恢复 |
| `Tests/App/test_app_main.c` | 验证入口传参、版本和网络处理顺序 |
| `Tools/Monitor/udp_probe.py` | 标准库UDP往返探测、超时/来源/UID/序号校验、JSONL记录 |
| `Tools/Monitor/tests/test_udp_probe.py` | 真实本地socket、错误/迟到应答、超时及报文验证；命令行复用现有IPv4/UID转换器 |
| `Docs/Milestones/M1_3_UDP_Request_Reply.md` | 本页：操作步骤、实物结果及验收边界 |
| `Docs/Project_State.md` | 更新当前已验证状态，区分历史版本与本次结果 |
| `Docs/README.md` | 更新文档入口及本阶段范围 |
| `README.md` | 更新GitHub首页进度、阶段标识及验收入口 |

No CubeMX change required。生成初始化、ETH配置、缓存/MPU、描述符/RX池和中断保持原样。先写失败测试，再实现最小接收与应答；运行已有Debug/Release工作流和布局检查后，由用户烧录验证。AGENTS.md第19节要求取得真实结果后才作功能完成提交，不自动推进M2。

## 协议v1扩展

继续使用固定28字节、大端整数的SBCG报文：magic[4]、version[1]、type[1]、length[2]、sequence[4]、tick_ms[4]、uid_w0/w1/w2[12]。不直接发送C结构体。version=1、length=28。

- type=1仍为原5005端口逐秒观测。
- type=2为电脑请求；UID必须为目标板UID，sequence为本次探测序号，tick_ms为电脑单调时钟毫秒的低32位。
- type=3为板端应答；完整回显请求sequence、UID和tick_ms，仅将type改为3。电脑同时匹配序号和毫秒标记，不把其它请求的迟到应答计入当前请求。

type=1的tick仍为板端启动毫秒，type=2/3的tick均为本次电脑请求标记，两种时间域不直接相减。电脑使用自身单调时钟计算往返RTT；它包含操作系统调度和网络处理，不是PTP误差或单向传播时间。32位标记可能在约49.7天回绕，不作为持久会话号或认证；仅用于独立测试网络，每次最多一个未完成请求。

板端只接受应用配置中的电脑IP及6006源端口，严格检查长度、magic、版本、type、目标UID。非法/其它来源不应答。接收回调在MX_LWIP_Process主循环中执行，不是ISR；复制28字节后释放接收pbuf，再分配PBUF_RAM回复，所有错误路径释放自身引用。PCB创建/绑定失败每秒重试；日志在Process中每秒最多一行，不在回调中printf。

电脑逐个请求，收到匹配来源、type、UID和序号的应答才确认；迟到或其它序号不确认当前请求，超时不无限重试。正常验收需要30发30收，无超时/本地发送错误；保留原始JSONL，不把本地socket模拟当成板端通过。

## 实施检查表

- [x] 先验证接收模块失败测试和应用未接入时的失败。
- [x] 实现最小Raw API服务、标准库探测工具和错误路径。
- [x] 原UDP/心跳/PHY测试及新测试通过。
- [x] Debug/Release构建和ELF布局通过，检查生成文件未改、记录ELF哈希。
- [x] 提供用户操作命令及预期日志，不操作Flash。
- [x] 取得用户30次匹配往返及串口结果，按本次已验证增量提交；不自动开始第二板或M2。

## 调用、IRQ、DMA与内存

初始化：`main → MX_LWIP_Init → App_Init → UDPProbe_Init`。本模块复制UID、清零计数，主循环`UDPProbe_Process`在netif和链路UP后申请并绑定独立PCB。PCB创建/绑定失败每1000 ms重试；成功后由生成netif管理链路，应用不重做ETH初始化或PHY扫描。

接收及应答：`App_Process → MX_LWIP_Process → ethernetif_input → IPv4/UDP → Receive → pbuf_copy_partial → pbuf_free(RX) → pbuf_alloc(PBUF_RAM)/pbuf_take → udp_sendto → 生成ETH轮询发送路径 → pbuf_free(TX引用)`。pbuf_copy_partial用于兼容接收链，接收长度必须28字节且实际复制完整。应用每轮仍处理旧5005发送、心跳及PHY监视，随后调用UDPProbe_Process做初始化重试/日志汇总；回调内没有串口输出。

无新增IRQ。SysTick仍是时基，Raw API回调由主循环调用；不在ETH ISR内发包或printf。无新增DMA1/DMA2控制器流、Normal/Circular或半完成回调。RX沿用ETH内部DMA、4项Rx描述符及4×1536池，回调先复制再释放pbuf，已有自定义释放函数归还RX池；TX把28字节栈数组复制入LwIP堆的PBUF_RAM后交给协议栈，DMA不读取栈数组，沿用4项Tx描述符和正常HAL_OK同步轮询路径。ARP自行管理排队的引用/副本。

I/D Cache保持未启用，MPU与保留区不变，无新Cache维护。Raw API的ACK_OK只表示返回ERR_OK；原生成glue没有传播所有HAL发送错误，fatal DMA等异常路径仍未验收。当前同时使用两个UDP PCB（旧5005和新5006），未更改CubeMX的池配置；布局仍由既有检查器验证，RAM的97.02%打印是固定高地址段的地址跨度，不代表实际普通数据用量。

## 主机验证记录

```powershell
python -B Tests/UdpProbe/run_tests.py
python -B Tests/Udp/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/Network/run_tests.py
python -B -m unittest discover -s Tools/Monitor/tests -v
python -B Tools/Monitor/autodebug.py build
python -B Tools/Monitor/check_eth_layout.py
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

- 新服务7项、旧发送6项、应用5项、PHY双模式19项、Python工具/布局/接收/探测34项，共71项通过。新服务空实现先在回调注册断言失败，应用未接入先在初始化断言失败；请求标记未回显/未核对时也先观察到失败，再修复。
- Debug构建及布局通过，报告`Logs/AutoDebug/20261020T071326-fe25577a/report.json`；Flash56508字节。ELF SHA256：`735a8882fa40d1f1aa8658e9d4f652998eb654aa0514f6ea76bef398c5278f8f`。
- Release构建及布局通过，报告`Logs/AutoDebug/20261020T071326-92c3c73a/report.json`；Flash35980字节。ELF SHA256：`7b93dcce1116b7d9a7d8a471ecfbd97b8d93e7b7406a9a7fb4cf47a91767e3d0`。构建诊断均为空。
- 原生C测试生成的`Logs/M1_3_UDP/test-ack.bin`由Python解析正确；CLI另用真实本地socket模拟服务器完成3次匹配往返，记录`Logs/M1_3_UDP/synthetic-8acebc95.jsonl`。这是主机模拟，不是板端证据。
- `.ioc`、Core、LWIP生成文件、cmake生成支持、HAL和中间件未改；没有操作电脑网络、防火墙或目标设备。电脑仍具有192.168.10.100/24，检查时6006未被占用。
- 2026-10-20收到用户结果后复跑上述71项检查、Debug/Release构建及布局检查，全部通过；ELF哈希与上列记录一致，无固件源码追加修改。复验报告分别为`Logs/AutoDebug/20261020T072520-9fb7bc78/report.json`和`Logs/AutoDebug/20261020T072520-af17f6b5/report.json`。
- 助手Flash：NOT_RUN；开发板请求/应答Runtime：用户电脑与串口日志联合验收PASS。完整本机摘要位于`Logs/M1_3_UDP/verification.json`。

## 用户实物验收（2026-10-20）

用户执行下节的30次探测命令并提供电脑全文、串口片段。本机`Logs/UDP/probe-01.jsonl`的事件与粘贴电脑日志逐条一致。原文副本及核对摘要保存在忽略的`Logs/M1_3_UDP/`，不提交运行日志。

| 核对项目 | 实际结果 |
| --- | --- |
| 网络端点 | 电脑192.168.10.100:6006 → 当前板192.168.10.1:5006 → 电脑 |
| 请求/应答 | requested/sent/received均30；请求和应答序号均为1～30，无缺口或重复 |
| 关联字段 | 30个echo_tick_ms逐项等于对应client_tick_ms；UID均为001A002D/30365105/36363631，来源均为192.168.10.1:5006 |
| 电脑错误项 | timeouts/send_errors/invalid/foreign/unexpected/unanswered均0；status=PASS |
| 停止方式 | stop_reason=count，自然完成；duration_s=29.012，首包立即发送，30包之间有29个间隔 |
| 软件往返RTT | 最小0.395 ms，平均0.611 ms，最大2.115 ms；不代表PTP、单向延迟或测距精度 |
| 板端接收与应答 | 35条UDP_RX汇总；可见VALID/ACK_OK由11同步增至30，之后保持30；BAD/FOREIGN/ACK_ERR/INIT_ERR始终0 |
| 原串口心跳 | 34条，SEQ=70～103，TICK_MS=70016～103016，相邻差1000 ms，TX_ERRORS始终0 |
| 原5005发送调度 | 34条UDP，SEQ=70～103，RESULT=OK；ERR/ERRORS/MISSED均0，LINK_SKIPS保持1，STACK_OK=SEQ−1 |
| PHY状态 | 7条刷新均LINK=UP、100 Mbps、FULL；本片段未见DOWN |

证据SHA256：

- `Logs/UDP/probe-01.jsonl`：`aac4a174b41f6d4e0aa2538cab906172079689eabca5d7acecfae96fa2dd10de`。
- `Logs/M1_3_UDP/user-pc-validation-20261020.txt`：`c9ba9091dafbb594632876a5af9213e78babe3ff1f316af40e957994ffcb0f25`。
- `Logs/M1_3_UDP/user-serial-validation-20261020.txt`：`76eb828a71535ec7a8c000b9b74ffab7b12f2228e95d73c377d284c712a8e0f6`。

结论仅覆盖当前板与电脑的正常30次请求/应答。串口截取从累计11开始，不宣称观察到了初始0及前10次板端汇总；电脑全文已覆盖全部30次匹配应答。工具固定输出hardware_verified=false，保留原值；本次实物通过结论来自用户提供的两端证据，不由工具自行认证。

本次附件没有BOOT及烧录输出，未新增确认固件启动标识、HCLK或Flash字节一致性；本板UID和请求应答协议已实际核对。原5005通道在板端持续调度，但本窗口没有对应电脑5005接收记录，不把旧M1.2的31包结果移用于新版本。本次未执行M1.3拔插恢复、非法包实物注入、第二板、PTP或测距测试。

## 复验操作

1. 在VS Code运行「STM32: Flash」，等待Programming Finished/Verified OK及复位完成。此步骤由用户执行，助手当前没有Flash写入授权。
2. 用SerialWeb、115200/8N1观察，确认BOOT为`SBCG-M1.3-UDP`、HCLK=200 MHz、UID与本板一致，等待PHY为UP/100 Mbps/FULL。
3. 在工程根目录独立终端执行下列命令，等待30次请求完成，不要中途Ctrl+C，也不要同时运行另一个占用6006的探测进程：

```powershell
python -B Tools/Monitor/udp_probe.py --uid 001A002D:30365105:36363631 --count 30 --log Logs/UDP/probe-01.jsonl
```

默认电脑绑定192.168.10.100:6006，板端目标192.168.10.1:5006；每秒一请求，单次超时0.8 s。每次运行更换新的日志名，工具排他创建，不覆盖旧证据。约30秒后应看到完整summary：`sent=30`、`received=30`、`timeouts=0`、`send_errors=0`、`invalid=0`、`unanswered=0`、`stop_reason=count`、`status=PASS`。unexpected记录其它序号/毫秒标记的应答，foreign记录其它来源，二者不能计作当前请求成功。

串口应出现以下汇总（格式示例，非实测结果）：

```text
$UDP_RX,VALID=30,BAD=0,FOREIGN=0,ACK_OK=30,ACK_ERR=0,INIT_ERR=0
```

如果本次只运行一次30请求，VALID和ACK_OK应各增加30；若此前已有请求，按测试前后增量核对，不要求累计值恰为30。原`$UDP`和心跳应继续；原5005接收工具可同时运行，其正常格式仍为type=1。请回传BOOT、若干心跳/UDP_RX行以及电脑完整summary，必要时提供本次JSONL。

工具summary中的hardware_verified=false固定表示工具不自行宣称硬件验收；实物结论由实际操作、BOOT/UID、串口计数和电脑30个匹配应答共同形成。退出码0表示探测窗口通过，1表示缺应答/错误包/提前中断等需查看summary，2表示参数或socket/文件错误。RTT是电脑软件往返时间，不是时间同步、单向延迟或声波测距精度。

### 如果超时

- 先核对固件版本、PHY UP、PC的192.168.10.100/24以及默认6006→5006，不能使用仍只发送的旧M1.2固件验证本目标。
- INIT_ERR持续增加：查看新PCB的绑定/分配是否成功；BAD增加：检查UID、type和长度；FOREIGN增加：检查电脑源IP/端口。
- VALID、ACK_OK增加但电脑无应答：检查电脑6006入站防火墙和探测目标/UID；只放行本测试地址/端口，不关闭整个防火墙。
- ACK_ERR增加：查看LwIP内存及发送路径，不把ACK_OK或单独串口日志当作电脑已收到。工具能忽略错误序号和毫秒标记的迟到应答，不因此确认当前请求。

本次正常请求/应答验收已完成，按AGENTS.md审查diff并以jxq身份提交本增量，在此停止；不自动进入第二板或M2。
