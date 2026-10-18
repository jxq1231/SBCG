# M1.2 当前板每秒发送UDP

日期：2026-10-19。前置基线为`63aef9b`：LwIP基本ping通过。本增量目标是当前板→电脑的逐秒UDP和序号核对，不涉及第二板、接收命令、PTP、音频或测距。SBCG-M1.2-UDP已通过主机验证及用户实物验收：电脑收到31个连续数据报，序号2～32、缺口0，相邻板端tick差均为1000 ms。新版网线拔插恢复仍未验证。

## 方案与文件

继续采用已经选定的NO_SYS主循环、Raw API和静态IPv4。Socket/Netconn需要引入RTOS；手写IP/UDP增加维护负担，本增量均不采用。用户确认电脑Realtek「以太网」为192.168.10.100/24。板端保持192.168.10.1/24，源/目标UDP端口5005。

| 文件 | 职责 |
| --- | --- |
| `App/Network/udp_config.h` | 集中定义目标IPv4、端口、1000 ms周期及报文长度 |
| `App/Network/udp_heartbeat.c/.h` | 主循环调度、显式序列化、Raw API发送及限速串口状态 |
| `App/app_main.c` | 在网络输入处理后调用发送模块，传入UID与毫秒时基；固件标识SBCG-M1.2-UDP |
| `CMakeLists.txt` | 在LwIP目标存在时注册用户拥有的UDP源文件 |
| `Tests/Udp/test_udp_heartbeat.c`、`Tests/Udp/run_tests.py` | 使用真实LwIP头文件、替代网络边界，验证周期、缓冲所有权和错误恢复 |
| `Tests/App/test_app_main.c` | 验证应用入口传参和持续调用顺序 |
| `Tools/Monitor/udp_monitor.py` | 电脑收包、严格解码、序号统计与JSONL日志，仅依赖Python标准库 |
| `Tools/Monitor/tests/test_udp_monitor.py` | 协议错误、缺包补到、重复、乱序、回绕、疑似重启及本地socket测试 |
| 本页、`Docs/Project_State.md`、`Docs/README.md`、`README.md` | 操作步骤、当前状态及验收边界 |

No CubeMX change required。保留生成配置、引脚、时钟、ETH描述符/RX池、MPU与Cache状态。测试不代表板端已通过；AGENTS.md第19节要求收到用户实物结果后才做功能完成提交。

## 报文v1

一个UDP payload固定28字节，显式写入字节数组，不发送C结构体。多字节整数采用网络字节序（大端），所有整数无符号。

| 偏移 | 长度 | 字段 | 约定 |
| --- | --- | --- | --- |
| 0 | 4 | magic | ASCII `SBCG` |
| 4 | 1 | version | 1 |
| 5 | 1 | type | 1，逐秒观测消息 |
| 6 | 2 | length | 28，含完整payload |
| 8 | 4 | sequence | uint32，启动为0，首周期为1，按经过的1000 ms时隙递增，允许回绕 |
| 12 | 4 | tick_ms | uint32，本次处理的启动后毫秒数，允许回绕；不是同步时间 |
| 16 | 12 | uid_w0/w1/w2 | 三个uint32，与串口BOOT一致 |

链路断开、内存不足和发送失败时也消耗对应时隙的序号，不补发旧包。主循环长时间延迟时跳过错过的时隙并记录MISSED，恢复后一次最多发一包，防止突发。链路恢复不重置序号。UDP PCB初始化失败按每秒周期重试，失败绑定释放PCB；PBUF_RAM逐包分配，复制后调用udp_sendto，不论返回是否成功均释放应用持有的pbuf引用。

串口`$UDP`每周期最多一行，区分链路跳过、初始化/分配/发送错误。`STACK_OK`仅表示Raw API返回ERR_OK；生成low_level_output没有传播HAL_ETH_Transmit返回值，且ARP可能暂存数据，因此不能据此宣称电脑收到。电脑接收结果为交付证据。原`$HEARTBEAT,TX_ERRORS`仍仅为串口输出错误。

## 接收统计边界

默认接收窗口60秒、至少30个唯一报文。只接收约定来源IP、端口和UID，严格检查magic/版本/type/长度。以本次观测的第一个有效序号为起点，统计到最高序号之间的唯一包数、缺口、重复与乱序；晚到的包可填补缺口。首包之前和停止后的丢失无法仅凭本次接收确定。uint32回绕使用半范围比较，单次实验的序号跨度须小于2^31；最多保存100000个唯一序号，达到边界明确停止。

序号缺口包含本地失败、链路断开、跳过时隙及网络未到达，不能单独归因于网络丢包。报文没有跨复位持久化会话号；若更高序号的tick明显倒退、旧序号搭配更新tick，或同一序号具有不同tick，则标记疑似重启并停止，重新启动接收工具建立新窗口；重启后的低序号/低tick仍可能与旧包乱序或重复无法区分，不宣称完全识别重启。每次主动复位应重启接收工具建立新窗口。正常乱序与uint32 tick回绕分别处理。

`tick_step_ms_min/max`只统计接收顺序向前且序号恰好相邻的板端tick差，正常约1000 ms；不把跨缺口、乱序或重复包当作单周期。JSONL另记录电脑UTC和本地单调时钟经过秒数；两者都不是两板同步精度证明。

## 实施与验证步骤

1. 先写主机解析/统计和固件发送测试，运行确认预期失败，再实现最小功能。
2. 原应用测试增加UDP初始化参数和MX_LWIP_Process先于发送的断言；保留PHY/心跳测试。
3. 执行本页后续列出的Debug/Release构建、ELF布局检查和主机测试，记录哈希；不写Flash。
4. 提供电脑接收命令与用户手动烧录步骤；先验证30个连续包，另做拔线/重连，保存真实日志。
5. 收到用户新版本日志后更新验收、审查diff，以jxq身份提交本增量；不自动进入双向UDP或M2。

## 调用链、IRQ、DMA与CubeMX

初始化：`main → MX_LWIP_Init → App_Init → UDPHeartbeat_Init`，复制UID并建立调度基准，不重新初始化网络硬件。

运行：`main循环 → App_Process → MX_LWIP_Process → UDPHeartbeat_Process → pbuf_alloc/pbuf_take → udp_sendto → IPv4/ARP → netif.linkoutput → HAL_ETH_Transmit`。MX_LWIP_Process每轮执行，UDP发送仅在1000 ms时隙到达且netif/链路UP时进行；链路观察和原串口心跳继续执行。

无新增IRQ：保留SysTick时基，无ETH global IRQ。应用发送和串口状态均在主循环，不在ISR。没有新增DMA1/DMA2流、Normal/Circular或半完成回调。

TX继续使用ETH内部DMA和4项Tx描述符。新payload为28字节；应用先在栈上组装，再复制至LwIP堆中的PBUF_RAM，DMA不使用栈数组。UDP/IP/Ethernet头部由协议栈添加；正常HAL_OK路径等待描述符OWN清除后返回，应用按Raw API约定释放自己的pbuf引用，ARP如需排队则管理其自己的引用/副本。生成glue忽略HAL返回值；HAL超时会清理当前描述符，fatal DMA错误路径的恢复和内存所有权仍属于未验证的生成驱动边界，本增量不宣称故障注入通过。已有RX_POOL仍为4×1536、4项Rx描述符，生成分配/链接/释放回调不变。I/D Cache保持未启用，未添加Cache维护调用或修改MPU、保留内存及CubeMX文件。今后开启D-Cache必须另行审查TX所有权和对齐。

## 已执行的主机验证

```powershell
python -B Tests/Udp/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/Network/run_tests.py
python -B -m unittest discover -s Tools/Monitor/tests -v
python -B Tools/Monitor/autodebug.py build
python -B Tools/Monitor/check_eth_layout.py
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

- UDP发送6项、应用5项、PHY双模式19项、Python工具/布局/UDP接收30项，共60项检查通过。发送空实现先在首周期断言失败，应用未接入先在初始化断言失败；接收器的重复tick校验及周期统计也先观察到失败再修复。
- Debug报告：`Logs/AutoDebug/20261018T163449-bc263c40/report.json`，Flash55140字节；Release报告：`Logs/AutoDebug/20261018T163449-53d4a409/report.json`，Flash34976字节。无新增编译警告，两个ELF布局检查通过；目录日期为UTC。
- Debug ELF SHA256：`debd9f2e26d0314bec977d6088154bbadc52094107ac7e08c5124787fec40ec7`。
- Release ELF SHA256：`2557660e345488f816bd986b0e42ccbc97b4fa3b6b03856520946a25ea52f460`。
- 原生C测试生成`Logs/M1_2_UDP/test-packet.bin`，经Python解析并由真实本地UDP socket运行接收CLI，3个模拟包、序号1～3、缺口0、tick差1000 ms。记录`Logs/M1_2_UDP/synthetic-6803962c.jsonl`仅为模拟，不是开发板证据。
- 只读确认电脑「以太网」为192.168.10.100/24，检查时5005没有监听进程；未修改电脑网络或防火墙。
- 收尾复验：60项主机检查、Debug/Release构建和两个ELF布局检查再次通过，构建诊断为空；最新报告分别为`Logs/AutoDebug/20261018T164502-f4fe8aa9/report.json`和`Logs/AutoDebug/20261018T164502-d45a1068/report.json`，ELF哈希与上列一致。
- 助手Flash/设备访问：NOT_RUN；开发板UDP Runtime：已按下节用户真实接收结果验收。此前SBCG-M1.2-INT的ping结论仍不代替UDP证据。

## 用户实物验收（2026-10-19）

用户执行本页默认60秒接收命令，并提供完整终端文本及新固件串口日志。附件原文已保存在本机忽略目录`Logs/M1_2_UDP/user-pc-validation-20261019.txt`，SHA256为`1e86b45c519944ba98e980a309b278114e18e7a6fea36e62c10dc6a663480991`；与本机`Logs/UDP/baseline-01.jsonl`逐条JSON记录一致。解析结果见`Logs/M1_2_UDP/user-validation-20261019.json`，原始日志不提交Git。

| 验收项 | 实际观察 |
| --- | --- |
| 固件/时钟 | BOOT为SBCG-M1.2-UDP，HCLK_HZ=200000000 |
| UID | 001A002D / 30365105 / 36363631，电脑每包与串口BOOT一致 |
| PHY | INIT=OK，地址0、ID=0007/C131、自协商开启；启动DOWN，随后100 Mbps全双工UP |
| 电脑监听/来源 | 192.168.10.100:5005 / 192.168.10.1:5005 |
| 包格式 | 31包均为version=1、length=28，UID一致 |
| 序号 | first_sequence=2，highest_sequence=32；共31包、31个唯一序号 |
| 缺口/重复/乱序 | missing=0、gap_percent=0.0、duplicates=0、reordered=0 |
| 非法/其它来源 | invalid=0、foreign=0、before_baseline=0 |
| 板端时间 | tick_ms=2016～32016，30个相邻差均为1000 ms，cadence_samples=30 |
| 接收窗口 | duration_s=60.0，首包在elapsed_s=29.576283，末包59.564647；正常窗口结束，status=PASS |
| 串口状态 | 已贴出的UDP及心跳序号为1、2～5、22～33；UDP ERRORS=0、MISSED=0，心跳TX_ERRORS=0 |

**判定：本次“当前板每秒向电脑发送UDP并核对序号”的独立目标通过。** 接收UTC为2026-10-18 16:42:47～16:43:17，对应Asia/Shanghai的2026-10-19 00:42:47～00:43:17。助手独立重算了31条记录的序号跨度、去重和tick差，结果与工具摘要一致。

SEQ=1在链路DOWN时跳过，STACK_OK=0、LINK_SKIPS=1；SEQ=2开始发送，后续可见串口状态的LINK_SKIPS保持1。首包之前的等待时间不折算为缺包，接收窗口内实际观测区间2～32没有缺口。工具中的hardware_verified=false是“不单凭接收器自行宣称硬件验收”的固定字段；本次判定结合用户新版本BOOT、UID及实际电脑接收结果形成，保留原始字段不改写。

用户串口粘贴顺序为1、22～33、2～5，不能将这段粘贴顺序当作运行时序号回退或自动复位证据；也不声称未贴出的6～21条串口心跳已逐条观察。电脑接收记录本身为2～32连续递增。助手未烧录、复位目标或读取本版本Flash，不宣称设备Flash与ELF逐字节一致。

本结论仅覆盖当前板到电脑的短时单向UDP。新版拔线再插入、长期稳定性、压力/吞吐、fatal DMA恢复、反向UDP、第二板、PTP及测距均未验收。本次收到基本发送结果后提交这一独立增量；拔线恢复可按后文另开窗口记录，不阻塞本次单向发送与序号核对的收尾。

## 用户实物操作

### 1. 先启动电脑接收

在SBCG工程根目录的独立终端执行；电脑以太网保持192.168.10.100/24，串口观察继续使用SerialWeb、115200/8N1：

```powershell
python -B Tools/Monitor/udp_monitor.py --duration 60 --min-packets 30 --uid 001A002D:30365105:36363631 --log Logs/UDP/baseline-01.jsonl
```

看到`"event": "listening"`后再烧录。默认绑定192.168.10.100:5005，筛选192.168.10.1:5005；如果不是当前板UID，修改`--uid`，不要把其它设备数据混入本窗口。日志使用排他创建，重复实验改成baseline-02等新文件名，避免覆盖原始证据。

### 2. 用户烧录并观察

运行VS Code「终端 → 运行任务 → STM32: Flash」，等待Programming Finished、Verified OK和复位成功。助手当前没有Flash写入授权，因此不会代为执行。若60秒接收窗口已结束，再启动一个新窗口即可；不用为重启接收工具重复烧录。

SerialWeb应出现BOOT中的`FW=SBCG-M1.2-UDP`、200 MHz与当前UID，随后PHY识别及100 Mbps全双工。以下仅为格式示例，实际tick、首序号和累计值以实测为准：

```text
$UDP,SEQ=3,TICK_MS=3016,RESULT=OK,ERR=0,STACK_OK=1,ERRORS=0,LINK_SKIPS=2,MISSED=0
```

启动协商时出现`RESULT=LINK_DOWN`是允许的；链路UP后应每秒一条RESULT=OK，同时原心跳连续。RESULT=OK不等于电脑已收到，必须查看接收窗口。

### 3. 正常链路验收

接收工具每包显示sequence、tick_ms、UID、disposition及当前missing，窗口结束显示`event=summary`。回传本版本BOOT、至少10条心跳、若干UDP行及**完整summary**。

- `unique >= 30`，`missing=0`，正常窗口`duplicates=0`、`reordered=0`、`invalid=0`；tick相邻差约1000 ms，记录实际最小/最大值。
- `status=PASS`表示接收窗口达到最小唯一包数且无缺口、无非法包并正常结束；仍要核对上述串口与来源。`hardware_verified=false`是工具不自行宣称硬件验收的固定边界，由用户实际操作及回传结果形成实物证据。
- 退出码0为该接收窗口通过，1为需查看摘要（不足30包、缺口、非法包、中断或会话异常），2为参数、文件或socket错误。原始包与摘要保存在指定JSONL，不能只回传串口STACK_OK。

### 4. 独立记录拔线恢复

正常窗口通过后，启动新窗口并用新日志名，例如`replug-01.jsonl`；运行期间拔线约5秒再插回，全程不复位开发板。观察串口DOWN/UP、LINK_DOWN及恢复后UDP，再核对电脑恢复接收、序号继续增长、缺口真实保留。该窗口可能因缺口得到REVIEW/退出码1，这是所记录故障注入的结果，不能把它混成正常链路丢包率。确认恢复后的连续包即可，未测则保留未测。

### 5. 没有收到时

先确认BOOT版本、链路UP、目标配置`App/Network/udp_config.h`、电脑IPv4和监听地址；用ping辅助诊断。若socket绑定失败，检查是否已有接收进程占用5005或日志重名。若持续RESULT=OK但0包，再检查Windows防火墙对当前Python程序、UDP5005及192.168.10.1来源的入站规则；只放行测试地址/端口，不关闭整个防火墙。需要修改板端IP/掩码时必须通过CubeMX；修改应用目标IP/端口只改用户拥有的udp_config.h并重新构建、烧录。

## 停止点

已收到并核对真实串口与31包电脑接收结果，满足根目录[AGENTS.md第19节](../../AGENTS.md)要求的本独立目标实物证据。代码、构建、主机检查和基本实物发送验收通过，提交本增量后停止；不自动开始拔线测试、双向UDP或M2。
