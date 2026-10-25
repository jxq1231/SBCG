# 串口观测与UDP应用协议

<!-- doc-status: current-interface; reviewed: 2026-10-23 -->

更新：2026-10-23。当前A/B板网络基线为 **SBCG-M2.3-ROLE**；**SBCG-M2.3-SYNC**已由用户手动烧录并通过1分钟运行内部观测。以下按首次引入增量列出仍有效的接口；旧“本版本”是对应报告的实验范围，不表示板上仍运行旧固件。原UDP type1～7及温度命令均保留，新增AUDIO_POS、AUDIO_ANCHOR及AUDIO_MAP/CHECK仅为串口行。

M3.2.3当时完成纯算法与离线回放验证；M3.2.4已将原内核接入板端并以SBCG-M3.2-MAP实测；其后M2.3.1以SBCG-M2.3-ROLE复验原业务并验收A身份。当前状态见[Project_State](Project_State.md)。

## M2.3.1 UID角色与网络身份（A联合实测、双板网络已通过）

M2.3.1的BOOT FW为`SBCG-M2.3-ROLE`；M2.3.2改为`SBCG-M2.3-SYNC`，HCLK_HZ及UID_W0/W1/W2格式不变。随后新增ASCII/CRLF行；以下为配置格式示例；本轮A行已在授权烧录/123秒联合验收中观察到，B网络身份已由双板UDP/ARP验证，B本版BOOT/完整外设未在所供网络日志覆盖；UNCONFIGURED仅软件测试：

```text
$BOARD,ROLE=A,IP=192.168.10.1,MAC=02:00:00:00:00:01,PEER_IP=192.168.10.2,SYNC=0
$BOARD,ROLE=B,IP=192.168.10.2,MAC=02:00:00:00:00:02,PEER_IP=192.168.10.1,SYNC=0
$BOARD,ROLE=UNCONFIGURED,NET=DISABLED,SYNC=0
```

每次正常启动仅输出匹配的一行。ROLE为A/B/UNCONFIGURED文本；IP/PEER_IP为点分十进制IPv4，MAC为6个十六进制字节，SYNC为0。UID按BOOT的三个uint32字精确匹配；A/B真实UID见[当前状态](Project_State.md)。身份未知时不启动LwIP或业务，不回退到默认A。字段是软件配置声明，实际网口需用带UID的UDP来源与ARP另验；本行不声明硬件读回或同步已锁定。

原UDP type1～7字节布局/宽度/字节序和端口不变，只改变B的源IP/MAC。`dual_board_monitor.py`可在同一PC5005分别核对两UID及来源，严格窗口不足/重复/乱序/缺包均不能PASS。双板工具已经在60秒窗口核对各60包且无身份冲突，详见[验收步骤](Milestones/M2_3_1_Dual_Board_Identity.md)。

## M2.3.2 固定A/B PTPv2与闭环日志（软件及1分钟运行观测通过，外部精度未测）

沿用全部旧UDP字节布局与端口；新增板间UDP319事件/320一般消息，LwIP `MEMP_NUM_UDP_PCB=6`由用户CubeMX生成。固定domain0、portNumber1、单播、two-step Sync/Follow_Up/Delay_Req/Delay_Resp；34字节标准头，前三者44字节，Delay_Resp54字节。消息时间为48位秒+32位纳秒的大端编码，但实现只接受可落入uint32秒且纳秒<1e9的值；sourcePortIdentity按已登记MAC映射EUI64+port1，Delay_Resp校验requestingPortIdentity。非零correctionField拒绝。本实现不含BMCA/Announce选主，不宣称完整第三方互操作。

A实际Sync TX=t1，B实际Sync RX=t2，B实际Delay_Req TX=t3，A实际Delay_Req RX=t4。B计算 `OFFSET_X2_NS=(t2-t1)-(t4-t3)`，`DELAY_X2_NS=(t2-t1)+(t4-t3)`，正时差为B领先A。B仅一项在途/待消费，TTL1000ms；A每至少250ms发送，启动及检测重启后6秒静默，依赖受控网络/前台推进，不保证跨掉电唯一会话。

新增ASCII/CRLF日志（以下仅字段格式，不是实测证据）：

```text
$PTP_SYNC_RAW,T1_NS=1000000000,T2_NS=1000001100,T3_NS=1000002100,T4_NS=1000003000,EPOCH=1
$SYNC_CTRL,ROLE=B,STATE=TRACKING,INTERNAL_LOCK=0,SYNC=0,OFFSET_X2_NS=200,DELAY_X2_NS=2000,DRIFT_PPB=100,FREQ_PPB=-100,EPOCH=1,RATE_REV=2,VALID=10,REJECT=0,ERRORS=0
$SYNC_ADJUST,KIND=FREQUENCY,PHASE_NS=0,FREQ_PPB=-100,STATUS=0,EPOCH=1,RATE_REV=2,SYNC=0
```

- `PTP_SYNC_RAW`四时刻是无符号uint64纳秒、EPOCH为本地uint32时间代际。仅B消费完整交换时输出；未带与CTRL共享的样本ID，不得按时间邻近假称逐项配对。
- `SYNC_CTRL`每至少1秒输出：A=`REFERENCE`；B=`ACQUIRING/TRACKING/LOCKED_INTERNAL/HOLDOVER/UNSYNCED/FAULT`。`INTERNAL_LOCK`为协议内部证据；`SYNC`始终0，不表示独立精度通过。OFFSET/DELAY为有符号int64的两倍纳秒；DRIFT为残余漂移int32 ppb，FREQ为已成功加载的绝对int32修正ppb，范围±100000。EPOCH/RATE_REV/VALID/REJECT/ERRORS为uint32计数；CTRL的REJECT是协调器计数，协议详细拒绝另看STATS。
- `SYNC_ADJUST`记录每个硬件请求：KIND=`STEP/FREQUENCY`；PHASE为有符号int64纳秒，正值推进B；FREQ正值加速B。STATUS是BSP枚举，0为成功，非0不得视为校正通过。STEP行FREQ字段是动作参数，不代表重设实际频率；实际修正看CTRL。
- `PTP_SYNC_STATS`每至少1秒输出OK/BAD/FOREIGN/CORRECTION/SEQ/EXPIRED/MISSING/TX_ERR/SEND_ERR/BIND_ERR/EPOCH，均uint32；OK为完成交换统计，SEQ是序号拒绝计数。
- B初始大偏差在3个一致样本后只做受控对齐，之后有界调频；连续8个样本且偏差≤10us、已有真实斜率≤500ppb、平均路径≤2ms才允许内部锁。坏时效/非法四时刻立即清除锁与连续证据，但不刷新最后有效时刻；无有效数据2秒HOLDOVER、5秒UNSYNCED。命令不确定或频率记录不符进入FAULT。
- 频率变化清除旧配对和音频三点模型，等待稳定分段重新建立；PCM DMA不停。没有新增IRQ、DMA或Cache维护。

采集：`python -B Tools/Monitor/serial_monitor.py --port COM3 --duration 620 --log-prefix Logs/TimeSync/M2_3_2/a-01`。B用其电脑实际COM号及`b-01`前缀；采集器不复位目标、拒绝覆盖文件、保存原始bin和JSONL，返回CAPTURED_PENDING_ANALYSIS仅表示采集结束。分析：`python -B Analysis/board_sync_analysis.py --serial <b-01-serial.jsonl> --out <新结果.json>`；Fraction重算原始时差/时延，CTRL独立汇总，不把同源内部锁定当作外部同步误差。

## M3.1 音频串口状态（连续数据流及受控幅度响应已实测）

仅新增原USART1文本行，不改变UDP的5005/5006/5007及PC端口。
以下均为格式示例，不是实测：

```text
$AUDIO_INIT,RESULT=OK,NOMINAL_HZ=48000,DERIVED_MILLI_HZ=47991071,START_ERR=0,CODEC=0,I2C_ERR=00000000,REG=FFFF,WRITES=24
$AUDIO,SEQ=1,STATE=RUN,FRAMES=40960,H=3,F=2,OK=5,DROP=0,ERR=0,HAL=00000000,DMA=00000000,RATE_HZ=47976,VALID=1,P0=100,P1=200,R0=30,R1=40
```

全部ASCII、CRLF。SEQ/H/F/OK/DROP/ERR/RATE_HZ为uint32十进制：H/F是半满/全满
回调次数，OK是已处理且结束校验通过的块数，DROP是积压或被覆盖的丢块，ERR是
回调顺序或HAL错误次数。FRAMES按`(H+F)*8192`用uint64十进制输出，只代表已观察
回调所覆盖的帧数。HAL/DMA/I2C_ERR为32位十六进制错误位图。
STATE为RUN、STALLED（回调超过1秒未更新）或ERROR。VALID=1时附带P0/P1、R0/R1，
分别为两个有效时隙通道的无符号峰值和整数RMS，范围0..32768，PCM满幅计数单位；
不预先声明它们对应实物左右位置。VALID=0时省略幅度字段，不能沿用旧幅度当作本次样本。
NOMINAL_HZ是配置名义值；DERIVED_MILLI_HZ是按时钟分频推算的毫赫兹值；
RATE_HZ是以回调计数和HAL tick窗口估计的整数帧率，**不是声音到达时间戳**。

START_ERR：0正常、1句柄未就绪/仍被占用、2外部芯片配置失败、3DMA启动失败、
4无效时钟、5停止失败。CODEC沿用BSP状态枚举：0成功、1忙、2I/O错误、
3ID不符、4非法参数；REG为失败寄存器uint16十六进制，成功为FFFF，
WRITES为成功寄存器写次数uint16十进制。输出限速每秒一条AUDIO；DHT启动低电平
期间延后统计与输出，IRQ仍只更新计数。完整验收见[M3.1](Milestones/M3_1_Audio_DMA.md)。

## M3.0 芯片身份串口状态（本版本已实测）

以下是格式示例，不是已采集的芯片响应；它只通过原 USART1 输出，不新增 UDP 报文。

```text
$CODEC,SEQ=1,TICK_MS=2016,ADDR7=1A,RESULT=OK,ID=8994,HAL_ERR=00000000,READ_MS=1,OK=1,ERRORS=0
```

SEQ/TICK_MS/READ_MS/OK/ERRORS 是无符号 32 位十进制数，时间单位 ms；
ADDR7 是 7 位地址的十六进制文本，ID 是完整读取的无符号 16 位十六进制数，
HAL_ERR 是本地 STM32 HAL 的 32 位错误位掩码（十六进制）。
RESULT 为 OK、BUSY、IO_ERROR、ID_MISMATCH 或 INVALID_ARG；BUSY/IO_ERROR 时
省略 ID，不能把旧值或半帧当作芯片响应。BUSY 表示 handle 非 READY 或总线忙，
没有启动事务时 HAL_ERR=0，避免把上次错误归到本次。
OK/ERRORS 是读取成功/失败的累计次数；DHT11 低电平避让不产生读取、不增加序号或错误。
正常间隔至少 2000 ms，READ_MS 是实际调用耗时，2 ms HAL 参数并非整体硬上限。
数据行以 CRLF 结尾；初始化不读取，首读在应用初始化至少 2000 ms 后。
ID=8994 通过仅能证明芯片控制接口通信，不代表麦克风、采样率或 DMA 通过。

所有整数无符号、网络大端；按偏移编码，不发送C结构体。板192.168.10.1/24，电脑物理以太网192.168.10.100/24。

## 已有5005及5006服务

| 偏移 / 字节 | 字段 |
| --- | --- |
| 0 / 4 | ASCII SBCG |
| 4 / 1 | VERSION=1 |
| 5 / 1 | TYPE：1心跳、2请求、3应答 |
| 6 / 2 | LENGTH=28 |
| 8 / 4 | SEQ |
| 12 / 4 | TICK_MS：心跳为板tick，请求/应答为电脑回显标记 |
| 16 / 12 | UID_W0、UID_W1、UID_W2各32位 |

板5005→电脑5005每秒发送type1；电脑6006→板5006发送type2，板5006→电脑6006回type3。旧应答除TYPE外逐字节回显请求。原工具udp_monitor.py和udp_probe.py保持此协议。

## M2.2.2专用时间交换

电脑6007→板5007：TIME_REQUEST(type4)，36字节。板5007→电脑6007：TIME_ACK(type5)，36字节；随后TIME_REPORT(type6)，52字节。ACK不是REPORT，REPORT不是ACK发送时刻的替代观察点。

| 偏移 / 字节 | 字段 | 约束 |
| --- | --- | --- |
| 0 / 4 | MAGIC | ASCII SBCG |
| 4 / 1 | VERSION | 1 |
| 5 / 1 | TYPE | 4、5或6 |
| 6 / 2 | LENGTH | 36、36或52 |
| 8 / 4 | SEQ | 1开始，0拒绝 |
| 12 / 4 | CLIENT_TICK_MS | floor(t1_pc_sw_ns/1000000)低32位 |
| 16 / 12 | UID_W0/W1/W2 | 当前板UID，3个32位字 |
| 28 / 8 | SESSION | 每次电脑运行生成的非零随机64位值 |
| 36 / 4 | RX_SEC | 仅REPORT；请求MAC/DMA接收秒 |
| 40 / 4 | RX_NSEC | 仅REPORT；0～999999999纳秒 |
| 44 / 4 | TX_SEC | 仅REPORT；对应ACK MAC/DMA发送秒 |
| 48 / 4 | TX_NSEC | 仅REPORT；0～999999999纳秒 |

ACK保留请求全部字段，仅TYPE改5。REPORT复制完整键，将TYPE改6、LENGTH改52，追加真实硬件快照。Python布局分别为`!4sBBHIIIIIQ`及`!4sBBHIIIIIQIIII`。

完整键：SESSION + UID + SEQ + CLIENT_TICK_MS。板保留最多4条记录，2000 ms到期；相同SESSION/UID/SEQ的重复键忽略，tick不同记冲突，不覆盖。已回报键保留至原到期时刻。缺少有效RX或TX、容量满、过期及发送失败均不能产生有效REPORT。REPORT不重发；电脑按每次请求总等待0.8秒记录缺项。

SESSION区分电脑运行，不是认证或板启动纪元。发生重启或时钟倒退时需复核BOOT和连续状态，不能只依靠SESSION证明没有重启。

电脑在一次运行内不得重用完整键；本工具使用单调递增SEQ且不重发，重新运行则生成新SESSION。板端仅提供2000ms窗口的重复标记，不能区分窗口结束后完全相同键的重放与仍留在ARP队列的旧ACK，不宣称无限期重放保护。正常30组采样必须保持键唯一。

## 四时刻和JSONL

- t1_pc_sw_ns：电脑构造/发送请求前的monotonic_ns。
- t2_board_hw_ns：REPORT中的RX_SEC×1000000000+RX_NSEC。
- t3_board_hw_ns：REPORT中的TX_SEC×1000000000+TX_NSEC。
- t4_pc_sw_ns：recvfrom返回后立即取时，且仅属于完整键匹配的首个ACK。

ACK和REPORT允许乱序；只有二者齐全且合法才产生four_stamp_sample。纳秒数在JSON中保存十进制字符串，SESSION为16位十六进制字符串，UID为3个8位十六进制字符串。来源标明PC_SW或BOARD_HW。仅比较各自时钟域的顺序，不将PC单调时钟参考点与板本地参考点当作UTC。

工具PASS仅表示本次网络样本检查通过；hardware_verified恒false，实物结论必须由串口BOOT、板状态、新旧网络采集与固件哈希联合建立。M2.2.2旧固件不调用估算器；M2.2.4实现如下，已通过本版本30组板端计算与真实C逐条对照实测。不校时或宣称同步精度；[M2.2.3](Milestones/M2_2_Sync_Observation.md)仅在电脑端对归档样本调用原C内核。实现边界及联合验收要求见[批准设计](Plans/2026-10-20_Time_Exchange_IO_Design.md)。


## M2.2.4 板端观测提交（本版本实测通过）

在现有5007/6007服务增加 OBSERVATION_SUBMIT(type7)，电脑发送、板不回新消息。原type1～6与端口不变。

| 偏移 / 字节 | 字段 | 约束 |
| --- | --- | --- |
| 0 / 36 | 同本次请求封套与完整键 | VERSION=1、TYPE=7、LENGTH=52 |
| 36 / 8 | T1_PC_SW_NS | 原请求发送前软件时刻，非提交发送时刻 |
| 44 / 8 | T4_PC_SW_NS | 首个有效type5 ACK接收时刻，非REPORT接收时刻 |

两个时刻都是无符号64位大端纳秒，Python布局`!4sBBHIIIIIQQQ`。板检查来源、长度、UID、非零SESSION/SEQ及`(t1//1000000)&0xffffffff == CLIENT_TICK_MS`。t2/t3只使用板端同键保存的RX/TX硬件快照，不接受电脑提供的板时刻。

仅REPORT发送返回OK的有效配对可领取一次；尚待ACK或REPORT记EARLY，失败/缺失/过期或完整键不匹配记NO_PAIR。领取后相同t1/t4记DUPLICATE，不再计算；改变电脑时刻记CONFLICT，不覆盖。队列满记QUEUE_FULL，保留原记录可供显式再次提交，工具不会自动重试。原缓存4项、原创建时刻起2000ms不变；成功/已提交/失败终止记录到期安静清除，未完成WAIT_ACK/READY仍按原TS_PAIR EXPIRED规则统计。

网络回调只做校验、配对和CPU副本入队。主循环`SyncObservation_Process`每次至多一项、结果最多1Hz；原created_ms达到2000ms输出STATUS=6且不计算。其它状态0～5直接沿用原C估算器，窗口800000000ns。不会修改PTP时间、频率或系统时钟。

成功串口示例（数值仅演示格式）：

```text
$SYNC_OBS,SESSION=ABCDEF0100000001,REQ_SEQ=1,CLIENT_TICK_MS=1000,UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631,STATUS=0,OFFSET_X2_NS=1999999599,DELAY_X2_NS=601,SYNC=0,PC=SW,BOARD=HW
```

OFFSET_X2_NS、DELAY_X2_NS为有符号64位整数十进制，单位为二倍纳秒；正偏差表示板时钟领先电脑，601表示300.5ns。STATUS非零的行省略两个数值，禁止复用旧结果。SESSION由两个32位十六进制字段拼接；结果经有界转换后用%s输出，不依赖64位printf。成功行含CRLF最坏226字节，应用上限256字节。

新增每秒统计：

- `$OBS_RX`：ACCEPTED、BAD、FOREIGN、NO_PAIR、EARLY、DUPLICATE、CONFLICT、QUEUE_FULL；独立于旧TS_IO/TS_PAIR。
- `$SYNC_OBS_STATS`：QUEUED、CALC_OK、CALC_ERR、EXPIRED、QUEUE_FULL、FORMAT_ERR。

电脑工具显式使用`--submit-observation`；默认模式不发送type7，保留旧事件和summary字段。显式模式要求CLI间隔至少1秒、总等待不超过0.8秒；每份完整样本最多补交一次，局部发送成功仅证明电脑sendto返回成功。新模式额外事件不能冒充原M2.2.3归档格式；联合验收直接对完整样本调用既有真实C适配器并与串口完整键/整数值比较，原始数据不改写。工具hardware_verified仍为false。

详见[M2.2.4实现及验收边界](Milestones/M2_2_Board_Observation.md)。当时SBCG-M2.2-OBS已获本版本授权烧录、校验、复位并实测30组通过；不表示双板同步或精度已达标。


## T1.1 温度与声速串口观测（R1正常窗口通过，R1当时未测拔插）

本节记录已实测的FW=SBCG-T1.1-TEMP-R1；R0实测FW=SBCG-T1.1-TEMP。后续T1.2曾获本版本授权烧录及联合实测，见下节。仅当前测试UID启用DHT11，旧UDP type1～7不变，无温度网络消息。

```text
$TEMP,STATE=OK,VERSION=1,T_MILLI_C=24400,RH_MILLI_PCT=53000,C_MM_S=346086,AGE_MS=0,FRESH=1,LAST=OK
$TEMP_STATS,ATT=2,OK=1,PRIME=1,INIT=0,BUS=0,START=0,TIMEOUT=0,PULSE=0,CLOCK=0,CRC=0,RANGE=0,FORMAT=0
```

以上为格式示例，非实测。各行ASCII并以CRLF结束，每秒最多各一条；本模块起始低电平与捕获期间延后日志。整数均为十进制：T_MILLI_C为有符号32位毫摄氏度，RH_MILLI_PCT为无符号32位毫%RH，C_MM_S为无符号32位毫米/秒，AGE_MS为uint32毫秒。VERSION及统计为饱和uint32；STATE/LAST为字符串，FRESH为0/1。

UNREADY/禁用时VERSION=0且不输出T/RH/C/AGE；首次读数仅预读，下次5秒周期正式更新。失败保留原值及版本，FRESH=0；HOLD_LAST表明历史快照，15秒以上为STALE。DISABLED表示测试UID或logger门控未满足。LAST分别为INIT/BUS/START/TIMEOUT/PULSE/CLOCK/CRC/RANGE/PRIMING/OK/DISABLED。INIT统计包含一次性初始化失败，CLOCK统计运行期时钟错误；FORMAT仅本地格式错误，UART发送错误归既有HEARTBEAT/TX_ERRORS。R1在起始低电平期间将所有应用日志放入16项FIFO，释放后每轮至多发送一条；队列溢出也计入TX_ERRORS。

声速为331300 + round_signed(606*T_MILLI_C/1000)，单位mm/s，恰半远离零。仅为干空气线性温度近似，湿度只观测，不参与模型；输出分辨率不是DHT11精度。接口与实机要求见[T1.1记录](Milestones/T1_1_Temperature.md)。

T1.1实物状态（2026-10-21）：R1经本版本授权烧录，17个正式版本均为T_MILLI_C=25700、RH_MILLI_PCT=25000、C_MM_S=346874，错误计数0，原网络联合窗口通过。用户延后拔插，因此上述HOLD_LAST/STALE及重预读恢复当时只有逻辑测试覆盖，未实物验收；不将正常采样通过扩展为全部故障行为通过。

## T1.2 手动温度命令（本版本实物通过）

命令为 ASCII 文本、区分大小写，以 LF 或 CRLF 结束，LF 前最多 63 字节（含可选 CR）。支持 `PING`、`TEMP?`、`TEMP SET <T>`、`TEMP AUTO`。温度允许可选负号、1～2 位整数及可选一位小数，范围 -20.0～60.0℃；额外空格、参数、指数及更多小数位拒绝。每轮最多 64 字节/一条命令，命令开始处理最小间隔 100 ms。请等待响应后再发下一条。

SET/AUTO/PING 成功返回 `$CMD,NAME=SET|AUTO|PING,RESULT=OK`；失败返回 `$CMD,NAME=SET|AUTO|INVALID,RESULT=ERROR,REASON=VALUE|UNAVAILABLE|COMMAND|FRAME`。VALUE 为温度格式/范围错误，UNAVAILABLE 为服务禁用/忙或版本耗尽，COMMAND 为未知命令，FRAME 为过长/坏字节/串口缺口后的重新定位。错误不改变设定值或版本。

TEMP? 返回 `$ENV` 和 `$CONSOLE`；SET/AUTO 成功后也返回 `$ENV`。ENV 顺序为 MODE、SOURCE、VALID、VERSION，VALID=1 时追加 T_MILLI_C、C_MM_S、AGE_MS。MODE=AUTO/MANUAL；SOURCE=DHT11/MANUAL/NONE，NONE 表示当前无可用值。VALID=0 时省略温度/声速/年龄，不应以旧值替代。数值宽度和单位沿用 T1.1；VERSION uint32 仅在有效发布时递增，切换到 AUTO 不重置版本。达到 UINT32_MAX 时停止发布新值，自动 LAST=VERSION；手动 SET 返回 UNAVAILABLE。

手动模式暂停 DHT11，值不自动过期、不持久化、不输出伪造湿度；每秒 `$TEMP` 为 STATE=MANUAL，并有 MODE=MANUAL、SOURCE=MANUAL。切换 AUTO 清除手动有效值，保持与最后一次采集至少 5 秒间隔并重新预读一帧，第二帧合格才更新。等待期间 UNREADY 可以带非零历史版本。正常 AUTO 的 `$TEMP`/`$TEMP_STATS` 格式保持原样。

CONSOLE 字段 RX_ERRORS、RX_OVERFLOWS、RX_REARM_ERRORS 都是饱和 uint32 事件计数：HAL 错误、环形缓冲溢出、续收启动失败。256 字节缓冲实际可存 255 字节。任何缺口作废队列和当前行，丢弃至 LF，再接受新命令；必要时先发送空行重定位。续收失败主循环最多每 10 ms 单次重试。HAL 回调不解析/打印；DHT 起始低电平期间命令响应推迟，既有日志队列保护仍生效。

本版本 FW=SBCG-T1.2-MANUAL，已完成手动保持/零温/负温/非法拒绝/分段/恢复自动及原网络联合实测，旧 UDP 协议及缓存配置未改。使用步骤与测试范围见 [T1.2](Milestones/T1_2_Manual_Temperature.md)。

2026-10-22补充实测：T1.2固件完成一次DHT11物理拔插恢复观察；HOLD_LAST/STALE保留旧值与版本、ENV无效值省略、PRIMING不发布新值及后续版本持续增长均已观察。TIMEOUT累计18并在恢复后停止增长。两段主机采集之间存在空档，不能由此证明精确15秒边界或全程连续网络零丢包；见[实测边界](Milestones/T1_1_Temperature_Replug.md)。

后续连续补测（2026-10-22）：T1.2同一固件已在单一约488.9秒窗口完成拔插验证，连续观察到AGE_MS=14744时HOLD_LAST、15744时STALE，ENV无效值省略、预读不发布版本、约5秒后恢复有效版本。488个UDP包序号完整，原30组请求及时间交换通过。此为约1秒日志分辨率的观察，不声称毫秒级过期切换精度；见[连续验收](Milestones/T1_1_Temperature_Replug.md)。


## M3.2.1 音频帧位置（正常窗口实机通过）

固件标识 `SBCG-M3.2-INDEX`。原 UDP type1～7、端口、PTP LOCAL/SYNC=0 和温度命令不变。
原 `$AUDIO` 格式保留，FRAMES 取 uint64_t 观察帧累计值；新增同频率、同 SEQ 的 ASCII/CRLF 行，先 AUDIO 后 AUDIO_POS：

```text
$AUDIO_POS,SEQ=1,SESSION=1,OBS_FRAMES=40960,FIRST=32768,COUNT=8192,GAP=0,INDEX_VALID=1,CONTIG=1,FAULTS=00000000,VALID=1
```

以上仅演示格式，不是实机数据。每秒最多一对；日志仍受 DHT 起始低电平延后机制保护。

| 字段 | 宽度、单位及含义 |
| --- | --- |
| SEQ | uint32 十进制；与同次 AUDIO 快照匹配，模 2^32 递增 |
| SESSION | 非零 uint32 十进制；本次 MCU 启动内 DMA 启动尝试号，失败尝试也可能消耗编号；耗尽拒绝启动（START_ERR=6），不回绕 |
| OBS_FRAMES | uint64 十进制、帧；累计观察到的 DMA 完成边界×8192，不随前台跳过而压缩，溢出时停止增加并标错 |
| FIRST / COUNT | uint64 / uint32 十进制、帧；最新成功处理块的零基首帧及帧数8192；左右声道同一采样时刻为一帧 |
| GAP | uint64 十进制、帧；该块相对上一成功 End 末尾的已知未交付范围；首块相对本会话起点0计算 |
| INDEX_VALID | 0/1；采集中且当前编号尚未发现歧义；不是独立证明硬件没有漏掉完整 DMA 循环 |
| CONTIG | 0/1；该消费块编号有效且 GAP=0；不能用于证明两条每秒日志之间首尾直接相接 |
| FAULTS | uint32，固定8位十六进制；1=回调顺序，2=观察间隔超过250ms，4=64位溢出，8=SAI/HAL错误，可组合；会话内锁存 |
| VALID | 0/1；本行位置来源于成功 End、同会话、未过期且编号可信的块。为0时 FIRST/COUNT/GAP/CONTIG 清零，不能将零解释为有效首帧 |

SESSION 不跨复位唯一；接收端必须以 BOOT 分段，重启或新采集后不能延续上一会话位置。
GAP 描述相邻成功处理块之间的缺口；每秒日志仅展示最新块，期间曾出现的缺口可能不落在展示块上，必须同时检查累计 DROP。
即便编号无效，原 AUDIO 仍可用于幅度诊断，不把其 VALID 与 AUDIO_POS 的位置 VALID 混用。

现有约170.7ms半块周期使用250ms保守观察阈值。IRQ标志合并、长关中断造成HAL tick漏计等情况，不能只靠本计数器排除；不可确认的硬件漏采不虚构缺口数量。
FIRST 是 DMA 数据帧位置，不是麦克风物理到达时间；completed_ms 仍只是回调 HAL tick。本增量不读PTP，不作校时、时间映射或同步精度声明。

正常窗口检查器：`Analysis/audio_position_analysis.py --serial <串口JSONL> --output <新JSON路径>`。
它要求预期固件BOOT、同一SESSION、原始AUDIO→AUDIO_POS顺序、至少60对连续日志、有效位置与累计计数一致，且DROP/ERR/HAL/DMA/FAULTS为0。
主机报告恒为hardware_verified=false；采集持续时间、固件哈希、原始串口及原网络联合验收另行核对，不能用行数代替90秒实际运行时间。

2026-10-22本版本实测：89对有效位置、SESSION=1、末观察4276224帧，缺口/编号错误0；原业务联合验收通过。codec最大2051ms和心跳1052ms保留为前台轮询抖动边界，见[M3.2.1完整记录](Milestones/M3_2_1_Audio_Frame_Index.md)。未作PTP时间映射或故障注入实物验收。


## M3.2.2 本地PTP/DMA位置锚点（正常窗口实机通过）

M3.2.2首次引入时FW=SBCG-M3.2-ANCHOR；当前MAP固件保留该接口，旧AUDIO/AUDIO_POS、UDP type1～7及端口不变。
新增ASCII/CRLF行，每1000ms最多一次尝试，DHT起始低电平期间延后；SEQ与AUDIO/POS独立，不据同序号强行配对：

```text
$AUDIO_ANCHOR,SEQ=1,STATUS=OK,VALID=1,SESSION=1,F_LO=47991,F_HI=47992,SEC=1,NSEC=0,WIDTH=1,SYNC=0,OK=1,REJECTS=0
$AUDIO_ANCHOR,SEQ=2,STATUS=BOUNDARY,VALID=0,PTP_ERR=0,SYNC=0,OK=1,REJECTS=1
```

以上仅格式示例，不是实机结果。

| 字段 | 宽度、单位及含义 |
| --- | --- |
| SEQ | uint32十进制，尝试号模递增；从初始化后的1开始 |
| STATUS / VALID | OK/1或CURSOR、PTP、SESSION、BOUNDARY、ORDER、WIDE、BACKWARD/0 |
| SESSION | 成功时才有，沿用本次启动内的采集会话，不能跨BOOT唯一标识 |
| F_LO / F_HI | 成功时uint64十进制，按NDTR折算的双通道传输组计数闭区间；不是PCM内存读取权或物理声学时刻 |
| SEC / NSEC | 成功时uint32十进制，本板PTP秒和0～999999999纳秒，来源为原BSP_PTP_Read；不是UTC |
| WIDTH | 成功时uint32十进制，F_HI-F_LO，最大4；0仍有一帧量化 |
| PTP_ERR | 仅失败时uint32十进制，原BSP_PtpStatus；0表示不是PTP读取错误，5为BAD_CONFIG，6为BAD_SNAPSHOT（含非法纳秒） |
| SYNC | 固定0；本板时钟仍为LOCAL |
| OK / REJECTS | uint32饱和累计的有效/拒绝尝试数；不是丢PCM块计数 |

CURSOR表示只读DMA位置无效；BOUNDARY表示两端边界generation变化；SESSION表示两端会话不一致或为0；ORDER表示帧倒退或同会话相对上个有效锚点不前进；WIDE表示区间>4帧。
PTP读取失败/非法纳秒为PTP；同会话相对前个已接受锚点PTP倒退/重复为BACKWARD并锁存，后续时间重新增长也不自动清除，只有新会话或服务重新初始化才重建基线。
失败省略SESSION/F_LO/F_HI/SEC/NSEC/WIDTH，禁止沿用旧值；固定字段和最宽整数的成功行编译期限制在256字节以内。

`Analysis/audio_anchor_analysis.py --serial <从新版BOOT开始的JSONL窗口> --output <新JSON路径>`核对全部尝试、计数、宽度、单调性、有效比例，并输出首末与相邻有效锚点之间的相对PTP平均率范围。
下/上界分别使用last.lo-first.hi-1和last.hi+1-first.lo，计入一帧量化；不是精密瞬时率或声学到达时间校准。报告hardware_verified=false，原音频位置及网络/温度/心跳仍须独立联合验收。
NDTR语义及[ST AN4031依据](https://www.st.com/resource/en/application_note/dm00046011.pdf)、完整限制和实机门槛见[M3.2.2记录](Milestones/M3_2_2_Audio_PTP_Anchor.md)。

2026-10-23本版本实测90次锚点全部有效，宽度0/1帧；原音频/位置、80包UDP、两组30请求及温度/codec联合检查通过。相对本地PTP的DMA进度平均率范围不等于声学时间校准；codec最大2068ms作为前台轮询抖动保留，详见本轮milestone。

## M3.2.3 离线时间估算（纯软件已验证）

本轮不新增UDP/串口报文。`AudioTimeMap_Estimate`接受同一BOOT/UID及SESSION的两端锚点和目标NDTR坐标，输出uint64纳秒的estimate/model_lower/model_upper及valid；状态0..8定义见[接口](../App/Audio/audio_time_map.h)。这是线性模型下的游标量化区间，不是麦克风声学时间误差界；不外推，失败输出清零。

在M3.2.3增量中，该内核尚未接入固件源列表或App调度；后续M3.2.4已完成接入。M3.2.3的4030组C/Fraction对照及30留出点回放通过，不新增实物同步/检测/测距结论；见[M3.2.3软件记录](Milestones/M3_2_3_Audio_Time_Map.md)。

## M3.2.4 板端时间估算观测（已实物验收）

原UDP type1～7、温度命令、AUDIO/POS/ANCHOR格式保持。新SBCG-M3.2-MAP在每三个连续有效锚点后输出MAP→CHECK，ASCII/CRLF、每行<256字节；不新增网络报文：

```text
$AUDIO_MAP,SEQ=1,STATUS=OK,CORE=0,VALID=1,SESSION=1,L=1,M=2,R=3,FRAME=95983,EST_NS=2000000000,LO_NS=1999979000,HI_NS=2000021000,SYNC=0
$AUDIO_MAP_CHECK,SEQ=1,OBS_NS=2000005000,RES_NS=5000,SYNC=0
```

仅为字段格式示例，不是已采集结果。SEQ为新服务独立uint32模序号；L/M/R为来源锚点uint32序号，SESSION为本启动内uint32会话。FRAME为uint64 NDTR相位坐标，用middle.lo+(middle.hi-middle.lo+1)/2避免绝对数相加溢出。EST_NS/LO_NS/HI_NS/OBS_NS为uint64本地PTP纳秒；RES_NS为int64，定义OBS−EST。SYNC固定0。

失败仅有`$AUDIO_MAP,SEQ=...,STATUS=ANCHOR|STALE|SEQUENCE|SESSION|ORDER|CORE,CORE=...,VALID=0,SRC=...,SYNC=0`，省略会话/位置/所有时间，不输出CHECK，不复用旧值。CORE仅内核拒绝时取原状态1..8，其他为0。失败和成功都消耗独立SEQ，窗口前两点静默；失败重建不跨断序/会话/过期窗口。新源到达前必须完成上一组CHECK配对。

服务是前台三点自检观测，不是声音事件查询接口。模型继承M3.2.3，未校准codec/SAI/FIFO/DMA物理延迟；新区间不代表声学误差界。`Analysis/audio_time_map_board_analysis.py --serial <从新BOOT开始的JSONL> --output-dir <新目录>`核对≥90原音频位置/锚点、≥30组MAP/CHECK，并与真实C及Fraction逐项比较。工具恒hardware_verified=false；本轮120秒实物采集结合UID/固件、原网络及温度联合验收通过，40组全部一致、最大绝对残差14.884us，仍LOCAL/SYNC=0。详见[M3.2.4验收记录](Milestones/M3_2_4_Audio_Time_Map_Board.md)。

## M4.1.2 本地声音候选（实验能量检测）

固件SBCG-M4.1-CAND-R1。沿用ASCII/CRLF，每行<256字节；无新UDP端口。

- `SOUND_INIT`：RESULT、CH（0/1）、WIN_FRAMES、ON_RMS、OFF_RMS（PCM幅度）、ON_WIN、QUIET_WIN、HOLDOFF_FRAMES；PROFILE固定EXPERIMENTAL、SYNC固定0。
- `SOUND_CAND`：SEQ为uint32饱和候选序号，SESSION为本次采集uint32会话，CH为0/1；FIRST、CONFIRMED为uint64本地帧坐标，分别是首个合格窗起点、末确认窗末帧。E_AC为uint32去直流均方幅度，PEAK为uint32原始绝对峰值，CLIP为0/1截幅标志。LOCAL=1、SYNC=0，不包含PTP到达时刻。
- `SOUND_STATE`：VALID_BLOCKS、REJECTED、RESETS、CANDIDATES、DROPPED、ERRORS为uint32饱和计数；PENDING为0..4队列长度，ARMED为0/1，CH为声道，SYNC=0。候选序号缺口可能来自有记录的队列丢弃；不可当作跨启动唯一键。

唯一的Audio Begin/End内暂算，真实End通过才提交；失败/断流清除确认历史与旧队列。候选至多每250ms发送一条，状态至多每1000ms一条；队列超龄1000ms丢弃计数，不补发。只能表明实验能量候选及其采样帧，不能当作标准音频识别、物理声到达时间或同步精度证明。字段与实测范围见[M4.1.2记录](Milestones/M4_1_2_Sound_Candidate_Integration.md)。

## M4.1.3 本地传输模型时间

SBCG-M4.1-TIME-R1新增ASCII/CRLF、每行<256字节，无新UDP：

- `SOUND_TIME`成功：SEQ/SESSION为uint32候选身份，FIRST为uint64零起点PCM帧号；STATUS=OK，L/R为uint32源锚序号；EST_NS/LO_NS/HI_NS为uint64本地PTP模型纳秒，查询坐标FIRST+1表示该组完整传输后位置。LOCAL=1/SYNC=0。
- 失败：相同SEQ/SESSION/FIRST，STATUS为CLOCK/STREAM/ANCHOR/SESSION/ORDER/GAP/STALE/NO_LEFT/TIMEOUT/CORE，CORE仅真实内核拒绝时为1..8、其他为0；不带L/R或任何模型时间字段。
- `SOUND_TIME_STATE`：QUEUED/OK/REJECT/DROPPED为uint32饱和累计，PENDING为0..4，LOCAL=1/SYNC=0。队满/非法提交计DROPPED；排队后失败计REJECT，不能将缺失结果补为0纳秒。

固定4锚点/4候选；等待2500ms超时、末锚年龄>1500ms拒绝。校时/调频及采集断流清旧模型。R1本组合日志统一排队，每主循环最多发送一行；接收时间不是模型时间。模型仅为线性传输进度，区间不包含物理声学延迟或同步误差。见[M4.1.3实测](Milestones/M4_1_3_Sound_Event_Time.md)。


## U1 SDRAM启动及自检日志（SBCG-U1-SDRAM-R1）

启动控制器/芯片准备完成后输出（仅表示可开始测试）：

```text
$SDRAM_INIT,RESULT=OK,BASE=C0000000,BYTES=8388608,SDCLK_HZ=66666666,CAS=3,REFRESH=1021
```

随后最多1Hz输出测试进度、终态一次：

```text
$SDRAM,STATUS=PASS,PHASE=5,PASSES=4,CHECKED_BYTES=33554432,ELAPSED_MS=50529,CMD=5,ERR_OFFSET=00000000,EXPECTED=00000000,ACTUAL=00000000
```

以上PASS/耗时为R1实物示例，不是每次启动固定值。所有字段为ASCII文本；BASE/ERR_OFFSET/EXPECTED/ACTUAL为无符号32位十六进制，其他数值为无符号32位十进制。BYTES为物理测试区字节数，CHECKED_BYTES为累计校验字节（8MiB×4=33554432），ELAPSED_MS为本次初始化起算的前台毫秒耗时，SDCLK_HZ单位Hz，REFRESH为SDRAM时钟周期计数。

PHASE：0未发布有效阶段、1数据位测试、2填充、3保持等待、4校验、5通过、6内存失败。PASSES是已完成轮数；CMD为尝试/最近完成步骤：0未开始、1Clock Enable、2PALL、3八次自动刷新、4模式字、5周期刷新配置，不是成功命令总数。

STATUS：TESTING/PASS/CONFIG_ERROR/COMMAND_ERROR/TIMEOUT/REFRESH_ERROR/MEMORY_ERROR。错误不会自动重试或继续访问内存；ERR_OFFSET为相对BASE字节偏移，期望/实际保留首次内存错误。仅STATUS=PASS、PASSES=4及完整累计字节才表示本次内存自检通过，INIT=OK不替代它。该日志不表示LTDC、标准音频识别、测距或双板同步精度通过。


## U2：RGB565状态页只读观测（SBCG-U2-STATUS-R2）

显示构建新增SDRAM STATUS=READY：仅芯片上电及显示区域可用，PHASE=0/PASSES=0/CHECKED_BYTES=0；不是完整内存测试PASS。专用U1诊断仍保留，二者互斥。

```text
$UI,STATE=RUNNING,FRAMES=77,ERRORS=0,DISCARDS=0,CAPTURE_MS=77831,ROLE=A,NET=DOWN,TEMP_SOURCE=AUTO,TEMP_VALID=0,TEMP_MC=0,C_MM_S=0
```

本行由前台最多每1000ms进入日志队列，实际串口到达可延迟；不是IRQ输出。STATE为OFF/READY/RUNNING/ERROR，READY尚未开始扫描；RUNNING不单独证明面板可读或外部同步精度。FRAMES是BSP确认发布帧数，DISCARDS是超龄后台帧数，均uint32饱和计数；ERRORS是0/1异常标志（不是累计错误次数）。CAPTURE_MS是已确认页面所用业务快照的HAL uint32毫秒时间，不是物理屏幕扫描时间。

ROLE=A/B/UNKNOWN，NET=UP/DOWN/UNKNOWN，TEMP_SOURCE=AUTO/MANUAL；TEMP_VALID为0/1。有效时TEMP_MC是int32毫摄氏度、C_MM_S是uint32毫米/秒，无效时两数值0仅作协议占位，屏幕显示--而不是0°C。全部为ASCII字段，无二进制结构体或端序依赖。温度设置沿用TEMP SET 25.0、TEMP SET 30.0、TEMP AUTO，命令以LF/CRLF结束；AUTO缺传感器时无有效值。

页面A参考偏差--；B只显示新鲜同步估计，内部锁不等于±10us已验收。距离m、方向SIDE --、QUALITY --目前均无测距结果，不填模拟数值。自动分析不能证明面板，U2实屏结论另由用户目视确认。

## U3.1：TouchGFX状态页只读观测

当前固件`SBCG-U3.1-TOUCHGFX`复用U2业务字段，追加：

```text
$UI,STATE=RUNNING,FRAMES=3966,ERRORS=0,DISCARDS=0,CAPTURE_MS=81893,ROLE=A,NET=DOWN,TEMP_SOURCE=AUTO,TEMP_VALID=0,TEMP_MC=0,C_MM_S=0,BACKEND=TOUCHGFX,UPDATES=81
```

- `FRAMES`：uint32饱和计数，单位为框架前台`endFrame`完成次数，**包括未绘图帧**；不能当实际绘图次数或业务数据版本。
- `UPDATES`：uint32业务文本副本版本，约每1000ms更新，回绕跳过0；主循环调度不是精密1Hz。
- `BACKEND=TOUCHGFX`：与历史U2区分，旧RGB565轮询/绘图栈不同时运行。
- `ERRORS`：0/1显示故障标志，合并BSP配置/传输/FIFO/HAL错误；不是各类错误总和。错误锁存后停止框架调度并关闭显示。
- `CAPTURE_MS`：HAL毫秒计数；`TEMP_MC`有符号毫摄氏度、`C_MM_S`无符号毫米/秒，仅`TEMP_VALID=1`时可用。`TEMP_VALID=0`行内0不代表0°C，屏上显示占位。

当前A参考显示LOCAL REFERENCE且偏差--，不伪造0偏差。即使B以后显示INTERNAL LOCK，仍不代表外部±10us验收。完整单板证据及首次窗口未知重启见[U3.1记录](Milestones/U3_1_TouchGFX_Status_Page.md)。
