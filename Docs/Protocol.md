# 串口观测与UDP应用协议

> 2026-10-26 TOA口径补充：`SOUND_CAND.TOA_US`仍是会话采样轴按名义48000Hz换算的微秒，不是PTP域时刻或已校准物理到达。`SOUND_TIME`继续查询原能量候选整数onset+1，保留旧语义；M4.3.1新增`SOUND_TOA_TIME`使用检测器固定窗口锚点+Q16子帧查询本地PTP模型。完整窗口不足、Q16非法或不夹锚时不产生成功TOA时间；局部估算成功计数不能代替输出/声学有效性。见[M4.3.1记录](Milestones/M4_3_1_Local_PTP_TOA_Time.md)。

<!-- doc-status: current-interface; reviewed: 2026-10-23 -->

更新：2026-10-23。新增SBCG-M2.3-PPS尚未烧录/实物验收，PPS行定义见下；当前已验收A/B板网络基线为 **SBCG-M2.3-ROLE**；**SBCG-M2.3-SYNC**已由用户手动烧录并通过1分钟运行内部观测。以下按首次引入增量列出仍有效的接口；旧“本版本”是对应报告的实验范围，不表示板上仍运行旧固件。原UDP type1～7及温度命令均保留，新增AUDIO_POS、AUDIO_ANCHOR及AUDIO_MAP/CHECK仅为串口行。

M3.2.3当时完成纯算法与离线回放验证；M3.2.4已将原内核接入板端并以SBCG-M3.2-MAP实测；其后M2.3.1以SBCG-M2.3-ROLE复验原业务并验收A身份。当前状态见[Project_State](Project_State.md)。

## M2.3.3 PPS串口（软件接口，实物未验收）

本版FW为`SBCG-M2.3-PPS`，保留原UDP和串口接口；新增ASCII/CRLF日志，每至少1000ms一行，使用原DHT安全发送队列。示例仅为格式，非实测数据：

```text
$PPS,ROLE=A,STATE=QUALIFIED,QUALIFIED=1,ARMED=1,TARGET_SEC=12,EMITTED=10,MISSED=0,ERRORS=0,SYNC=0
```

ROLE为A/B；STATE为UNQUALIFIED/QUALIFIED/RATE_REQUALIFY/FAULT；QUALIFIED及ARMED为0/1。TARGET_SEC为uint32本地PTP目标秒；EMITTED/MISSED/ERRORS为饱和uint32软件诊断计数。EMITTED只表示前台观察到TIM2.TIF、ETH.MACSR.TSTS、自清TSITE、目标及epoch一致，不代表探头测到的边沿个数或精度。MISSED为取消已装载目标次数，不能直接当作实物丢脉冲率。SYNC固定0，独立精度尚未验证。

A本地PTP健康时输出参考脉冲；B需|offset_x2_ns|≤20000、delay_x2_ns在0..4000000、|drift_ppb|≤500且首次内部锁定。样本年龄<2000ms；一次≤1125ppb纯变频允许<4000ms重新资格，仍受样本年龄约束，第二次变频撤资格。FAULT需显式初始化或复位恢复；未知本地时钟代际后A须重新初始化。A的对端重置不改变本地参考时钟，不撤其参考PPS。当前只有A时B输出及两板精度不在验收范围。

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


## M4.3.1 固定窗口TOA本地PTP模型

M4.3.1新增独立串口记录，旧`SOUND_TIME`语义不变；无新UDP、无CubeMX/硬件配置变化：

- `SOUND_CAND`继续携带旧`TOA_US`（名义48k诊断，不是模型时间）。
- `SOUND_TOA_TIME`成功：`SEQ/SESSION`为候选身份；`FRAME`为**NDTR模型进度整数坐标**（零基PCM窗口前沿floor后加1），`Q16`为1/65536帧分数；`STATUS=OK,CORE=0,L/R`为相邻源锚序号；`EST_NS/LO_NS/HI_NS`为本地PTP模型纳秒；`LOCAL=1,SYNC=0`。
- 缺失/非法：`STATUS=TOA_MISSING`或`TOA_INVALID`，不带FRAME/Q16/L/R/模型时间；不会回退用FIRST+1冒充成功TOA。
- 模型边界针对量化后的Q16坐标；一Q16 LSB分辨率不是物理采样/声学误差，也不把SYNC改为1。
- `SOUND_TOA_STATE`：`QUEUED=...`为uint32饱和计数，`QUEUED`含旧整数路径；`OK/REJECT/MISSING/INVALID/DROPPED/PENDING`用于窗口TOA；每秒至多一条。
- 旧`SOUND_TIME`仍按候选`FIRST+1`查询整数模型；两个记录的坐标、边界和适用性不能混用。见[M4.3.1记录](Milestones/M4_3_1_Local_PTP_TOA_Time.md)。


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

## 启动原因诊断（SBCG-U3.1-RESET）

BOOT之后新增一条ASCII/CRLF日志，原UDP和周期业务保持原协议：

```text
$RESET,CSR=14000000,FLAGS=14000000,PIN=1,POR=0,BOR=0,SFT=1,IWDG=0,WWDG=0,LPWR=0,CLEAR_OK=1,CAPTURED=1
```

CSR为早期读取的完整uint32十六进制原值，FLAGS=CSR&FE000000；PIN/POR/BOR/SFT/IWDG/WWDG/LPWR分别为26/27/25/28/29/30/31位的0/1值。多位可同时置位，PIN1不等于人工按键；上述数据是明确软件复位对照的实测结果。

CAPTURED1表示早期单次捕获存在；CLEAR_OK1表示保存后清除旧累计复位标志并读回成功。Get不再次清标志，任一资格为0时不能把下次标志直接当独立新原因。CSR是状态记录，不是纳秒时间，也不承诺唯一根因。历史额外启动仍待复现，见[诊断报告](Milestones/U3_Reset_Diagnosis.md)。

## M4.2.2 有界扫频匹配（SBCG-M4.2-CHIRP-R2，已烧录、静默失败）

初版SBCG-M4.2-CHIRP、R1及R2均已烧录、静默未通过；当前A为R2，协议与R1字段相同。R2音频累计零丢块，但搜索DROP56、串口TX_ERRORS503，不能按示例数值认定通过。仅新增ASCII/CRLF串口日志，不改变UDP端口/报文或原SOUND_TIME语义。下面是格式示例，不是实物采集结果：

```text
$CHIRP_INIT,RESULT=OK,CH=0,TEMPLATE=2K6K50MS,SCAN=8192,SCORE=8,BEFORE=192,AFTER=64,CACHE_FRAMES=2656,JOB_MS=1500,RMS=64,CORR_PPM=800000,GATE_RMS=128,PROFILE=EXPERIMENTAL,SYNC=0
$CHIRP,SEQ=1,SESSION=1,FIRST=48000,LAST=50399,CORR_PPM=1000000,RMS=11400,CLIP=0,INVERTED=0,LOCAL=1,SYNC=0
$CHIRP_STATE,OK=100,BAD=0,CAND=10,MATCH=10,DROP=0,SKIP=0,ERR=0,EVAL=2570,COPY_MS=1,WORK_MS=2,S_MAX=8192,E_MAX=8,PENDING=0
```

- `CHIRP_INIT`：`CH`为交错PCM声道0/1；`TEMPLATE`标识当前固定2–6kHz/50ms候选，完整源文件SHA256见[本轮记录](Milestones/M4_2_2_Bounded_Chirp_Integration.md)。`SCAN/SCORE`是每轮最多8192帧/8个2400点窗口；当前块门控一次扫完，评分分轮执行。`CACHE_FRAMES=2656`为队首完整输入快照点数，`JOB_MS=1500`为任务/副本时限毫秒；`BEFORE/AFTER`为候选提示点前192/后64帧。`RMS`为匹配窗AC RMS门限64 PCM，`CORR_PPM`为评分门限800000/1000000，`GATE_RMS`为候选门控开启门限128 PCM。均为实验配置。
- `CHIRP`：`SEQ`为独立uint32饱和匹配发布计数；`SESSION`为BSP采集会话uint32，重启后须以BOOT分段。`FIRST/LAST`为uint64十进制绝对双声道采样组帧号，含首含尾，共2400帧；不是int16元素索引、HAL毫秒或PTP纳秒。
- `CORR_PPM`为绝对归一化相关度×1000000四舍五入的uint32，范围0..1000000，不是识别概率；`RMS`为匹配窗AC幅值uint16，范围0..32768。`CLIP=1`表示窗口出现PCM16极值，测量层不能直接当作未失真高质量结果。`INVERTED=1`表示相关分子为负，不是声源方向。
- `LOCAL=1,SYNC=0`明确本地帧匹配，没有新增统一时间或距离。**CHIRP的SEQ独立于SOUND_CAND；现有SOUND_TIME仍对应原能量候选，不能把它的时间值拼接成CHIRP到达时间。** 本服务没有填造搜索结束时刻的采样游标。
- `CHIRP_STATE`的`OK/BAD`为归还后提交/拒绝块数；`CAND/MATCH`为新门控候选/完整搜索过门限结果数。完整搜索未匹配不产生测量行，不计作数据丢失。
- `DROP`为容量、过期、缺少历史、覆盖或失效清理导致未完成搜索被丢弃的数量；`SKIP`为新块替换时旧块尚未完全扫描的次数，和原`AUDIO,DROP`硬件/归还计数不同。`ERR`为生命周期或内部流处理错误数，`EVAL`为实际已评分窗口数。以上均为uint32饱和计数。
- `COPY_MS/WORK_MS`为观察到的最大HAL毫秒差：分别覆盖服务复制，以及归还后的扫描/评分；后者不包含串口格式化、UART发送、原统计或其他主循环业务。0ms只表示未跨HAL tick，不代表零耗时。不是最坏执行时间证明。
- `S_MAX/E_MAX`为单次Process实际最大扫描帧数/评分窗口数uint16，分别不得超过8192/8；`PENDING`为当前搜索队列数量uint8，0..4。状态约1Hz，匹配行优先，每次服务调用最多发送一行。

能量门控和有限搜索在持续噪声、弱信号、处理积压或反射环境下可能漏检或选中较强反射；门限及范围尚未经过手机/音箱实机标定。格式及主机测试通过不等于现场识别率、±100μs或测距精度通过。

## M4.2-DIAG观测扩展（历史定位版本，定位功能已实机验证）

M4.2-DIAG为当时A板实测定位版本（Debug前缀d3e084b1d83e），保留原CHIRP字段与搜索行为，CHIRP_INIT末尾增加DIAG=1。以下为格式示例；真实观测为SCAN_MS59/SCORE_MS5、TIMEOUT21/HISTORY13、LOG_FULL208，详见对应报告，不表示性能已修复。

```text
$CHIRP_DIAG,SCAN_MS=10,SCORE_MS=50,GAP_MS=80,TIMEOUT=12,HISTORY=3,FULL=0,RANGE=0,RESET=0,DONE=4
$HEARTBEAT,SEQ=10,TICK_MS=10000,TX_ERRORS=5,LOG_FULL=5,LOG_WRITE=0,LOG_BAD=0,LOG_Q=20,LOG_HWM=32
```

CHIRP_STATE和CHIRP_DIAG共享原1000ms节流并交替输出，合计约1Hz；事件仍优先、每次服务最多一行。SCAN_MS/SCORE_MS/GAP_MS为uint32最大HAL毫秒差：分别为Scan段、Search段（含缓存/清理）、相邻Process调用间隔；不包含串口格式化/发送或其他模块，不是最坏时限证明。

TIMEOUT为单任务期限拒绝、HISTORY为所需副本被覆盖、FULL为搜索队列满、RANGE为范围/缓存长度拒绝、RESET为失败End/会话/流等整体清理。五类是分支优先级选定的互斥主原因，uint32饱和；未饱和时合计等于DROP。副本整体过期清理属RESET。DONE为完整搜索完成数，包含不匹配；仍需用MATCH区分匹配结果。

LOG_FULL/WRITE/BAD与原TX_ERRORS同路分别记录日志队满、BSP写失败和非法长度，uint32自然回绕，与原聚合语义一致。LOG_Q为格式化心跳时的待发行数，不包含该心跳；LOG_HWM为成功初始化以来的最大待发行数，单位均为行。日志行变长会增加少量串口开销，定位版不代表拥堵已解决。新字段仅CHIRP构建启用，旧构建心跳格式不变。

## M4.2.3-B批量扫描（SBCG-M4.2-BATCH）

M4.2.3-B只把Chirp扫描从逐帧`SoundDetector_Push`迁移到`SoundDetector_PushBatch`：完整64点窗走批量统计，其他输入沿用原逐帧回退；原候选门控、有限搜索、两套阈值、Begin/End事务、8窗/轮及日志策略保持不变。`$CHIRP_INIT`/`$CHIRP`/`$CHIRP_STATE`格式不变，`$CHIRP_DIAG`追加两个任务终结时延字段。以下为格式示例，不是实物结果：

```text
$BOOT,FW=SBCG-M4.2-BATCH,HCLK_HZ=200000000,...
$CHIRP_DIAG,SCAN_MS=3,SCORE_MS=5,GAP_MS=80,TIMEOUT=0,HISTORY=0,FULL=0,RANGE=0,RESET=0,DONE=4,DONE_MAX_MS=420,DROP_MAX_MS=1500
```

- `SCAN_MS`仍为一次Scan段最大HAL毫秒差，包含64点批量推进调用；每轮最多推进8192帧，预算和候选坐标语义未变。
- `DONE_MAX_MS`为已接纳搜索任务从关联音频块`completed_ms`到完整完成的饱和最大HAL毫秒差，包含缺尾等待、缓存、调度及处理；不是纯计算耗时，也不是声音到达精度。
- `DROP_MAX_MS`为已接纳任务被丢弃时同一均起点的饱和最大时延，与`DONE_MAX_MS`分开统计；丢弃本身仍计入`DROP`及五类原因。未进入搜索队列的候选（如FULL）没有任务起点，不计入两者。
- 两字段使用uint32无符号差，tick回绕时差值语义仍正确，但不表示绝对时刻；0表示尚未发生对应事件或未跨HAL tick，不代表零耗时。
- `JOB_MS=1500`仍为任务/副本期限，超期任务记TIMEOUT并按`DROP_MAX_MS`报告。完成时延≤1000ms是受控负载下的工程目标，不是本协议保证，也不把丢弃样本从统计中剔除。

2026-10-26 A板Debug（`SBCG-M4.2-BATCH`）静默实测：SCAN_MS最大3、DONE_MAX_MS=831、丢弃原因/日志/音频错误全0；主动标准音频与MATCH未验收。详见[M4.2.3-B记录](Milestones/M4_2_3_B_Batch_Scan.md)。

## M4.2.4-A2 同循环前台归因（CHIRP构建心跳追加字段）

仅CHIRP构建在`$HEARTBEAT`的`LOG_HWM`后追加四个uint32最大毫秒字段；非CHIRP构建心跳格式不变：

```text
$HEARTBEAT,SEQ=10,TICK_MS=10000,TX_ERRORS=5,LOG_FULL=5,LOG_WRITE=0,LOG_BAD=0,LOG_Q=20,LOG_HWM=32,LOOP_MS=54,AUDIO_MS=7,UI_MS=26,OTHER_MS=21
```

- `LOOP_MS`：`App_Process`入口（身份检查后）到`FlushOneLine()`后的迄今最大单轮HAL毫秒差，包含本诊断自身读取与心跳追加字段。
- `AUDIO_MS`/`UI_MS`：上述最大轮内`AudioCapture_Process`与UI处理（TouchGFX或LCD）的HAL毫秒耗时；UI不含其日志`Report()`格式/发送，AUDIO含其内部服务与日志处理。
- `OTHER_MS`：`LOOP_MS-AUDIO_MS-UI_MS`的残差（下溢钳0），包含`FlushOneLine`同步UART、LwIP、温度/PTP/PPS、AudioAnchor/SOUND_TIME、SDRAM等，不是纯CPU或“未解释时间”。
- **采样口径限制**：`GAP_MS`在`AudioCapture_Process`内相邻`ChirpService_Process`之间测量；DHT起始低电平会整轮跳过UI、日志跳过时下一次GAP可能跨多轮。LOOP_MS与GAP_MS不是同一采样点，只能作同调度周期的近似对照，**不得用LOOP_MS−GAP_MS推断未归因时间**。
- 只保留迄今最大一轮的分解；较慢轮被记录后，后续轮次不会覆盖，除非出现更慢轮。0表示未跨HAL tick或未发生；1ms分辨率，不是最坏执行时间或声学精度证据。

## M4.2.4-A3 AUDIO/OTHER内部分解（低频诊断行）

仅CHIRP+AUDIO构建每5秒最多输出两行诊断；非CHIRP/无AUDIO构建不输出。两行均为迄今最大轮的HAL毫秒分解，不是最坏执行时间保证：

```text
$PROFILE,LOOP_MS=40,AUDIO_MS=27,UI_MS=0,OTHER_MS=13,FLUSH_MS=12
$AUDIO_PROFILE,TOTAL_MS=28,STAT_MS=15,SD_STAGE_MS=6,SD_PROC_MS=0,CH_STAGE_MS=1,CH_COMP_MS=0,CH_PROC_MS=6,IO_MS=0,LOG_MS=1,OTHER_MS=0
```

- `$PROFILE`：最大`App_Process`循环的A2四字段加上同轮的`FLUSH_MS`（`FlushOneLine()`同步UART耗时）；`LOOP_MS`与`GAP_MS`采样点不同，不能相减推断。
- `$AUDIO_PROFILE`：最大一轮`AudioCapture_Process`的`TOTAL_MS`及同轮分段：`STAT_MS`为`AudioStatistics_Compute`，`SD_STAGE_MS`/`SD_PROC_MS`为原声音候选Stage/Process，`CH_STAGE_MS`/`CH_COMP_MS`/`CH_PROC_MS`为Chirp复制/完成/扫描评分，`IO_MS`为Begin/End/候选Complete，`LOG_MS`为音频状态格式化/发送，`OTHER_MS`为总时长减已计分段的残差。
- 两套都是“最大轮”快照而非平均/分布；1ms分辨率，含SysTick抢占和诊断自身开销（每行最多一次UART发送）。`$PROFILE`/`$AUDIO_PROFILE`每5秒各最多一行，`$PROFILE`最长106字符、`$AUDIO_PROFILE`最长222字符（最坏），实测63/132，均<256；新增两行会轻微抬高其报告值。
- **口径**：`OTHER_MS`仍包含`FLUSH_MS`（`LOOP=AUDIO+UI+OTHER`，不能再用OTHER加FLUSH）；`$PROFILE`的最大循环与`$AUDIO_PROFILE`的最大AudioCapture轮是两次独立最大值，不能互相求和。A2无A3诊断时实测LOOP38/42、AUDIO21/25、OTHER16；A3为LOOP40、AUDIO27、OTHER13/FLUSH12，A3未抬高最慢LOOP，但AUDIO增量可能含插桩开销，FLUSH12与诊断行发送时长一致，不能完全排除A3诊断自扰动；A4前应做关闭诊断的对照采集。
- 正式口径：`OTHER_MS`/`FLUSH_MS`不能当纯CPU；`STAT_MS`等分段是墙钟差，不是指令周期或最坏时间；不得用单次最大值直接改动日志/业务策略。


## M4.2.5 失败搜索诊断（SBCG-M4.2-SEARCHDIAG）

本节为新增软件接口；旧BATCH固件不输出，当前实物状态以Project_State及本轮报告为准。门限、搜索范围、有效匹配CHIRP行保持原含义；CHIRP_INIT中DIAG=2标记此接口。只为完整搜索完成的任务增加一行，示例不是实测：

```text
$CHIRP_SEARCH,ID=20,SESSION=1,HINT=3000000,LO=2999808,HI=3000064,BEST=2999900,CORR_PPM=421000,RMS=600,CLIP=0,INV=0,EDGE=0,MATCH=0,WHY=LOW_CORR,N=257,V=257,LR=0,Z=0,I=0
```

- ID为当前启动内的uint32候选累计计数，拒绝任务会留下编号缺口，UINT32_MAX后饱和，不能单独作永久唯一身份；SESSION为uint32非零音频会话，仍须区分板卡/BOOT域。
- HINT为该任务实际门控提示帧；LO/HI为被完整搜索的模板窗口起点范围（含端点）；BEST为所有相关可定义窗口的最高原始相关峰起点，均为uint64绝对PCM帧，不是声学到达时刻。原始峰使用double比较，同分取最早。
- CORR_PPM为门限前abs去直流归一相关乘1000000并四舍五入，uint32、0..1000000；RMS为该raw峰窗的去直流PCM16幅值整数RMS，uint16、0..32768；CLIP/INV为0/1，分别表示该窗包含INT16_MIN/MAX和相关符号为负。
- EDGE为位标记：0内部、1左边界、2右边界、3同时两端。触边只能提示覆盖风险，不能证明真实峰在范围外。
- MATCH为整个任务是否存在通过现有RMS及相关门限的窗口；WHY是raw峰窗口的状态LOW_RMS/LOW_CORR/MATCH。两者独立：最高raw峰可能低RMS而另一个窗口通过，不得用raw峰代替原有效匹配best。
- N/V/LR/Z/I为uint16窗口计数：总次数/正方差可评分次数/低RMS次数/零方差次数/非法次数；N=V+Z+I，LR<=V。当前每任务最多257窗、每次调用最多8窗；V包含低RMS窗口，相关恰为0仍可评分。
- V=0时WHY=NO_VALID，BEST/CORR_PPM/RMS/CLIP/INV/EDGE为NA，不能当作相关0或有效帧0；Z/I区分无方差与非法输入。
- 原CHIRP事件优先，同一调用最多输出一行，成功任务诊断在后续前台调用发送；不在ISR或Stage/Complete发送。CHIRP_DIAG追加RPT/RLOST两个uint32饱和计数：交给原日志回调的报告次数/流或报告交接失效（历史清理、防御性覆盖及无法交给回头的格式化拒绝；当前配置下最后一项不可达）丢弃的待发报告数。RPT不是电脑已收到，应用队满仍查HEARTBEAT日志计数。
- 搜索诊断最坏247B、CHIRP_DIAG追加后最坏252B（含CRLF），均小于256B；不新增PCM缓冲、DMA或硬件配置。新增格式化与UART排空有成本，需要独立实物复验，评分次数不变不能证明毫秒耗时不变。

此接口仅用于定位本次真实音频0/10匹配，不提供PTP时间、物理到达精度或距离；不会将低于门限的raw峰作为有效CHIRP发布。

## M4.2.6 候选附近PCM捕获（SBCG-M4.2-PCMCAP）

本节为新增软件接口，供候选附近End验证PCM下传与离线宽带相关分析；不改变M4.2.5门限、搜索范围或匹配行。CHIRP_INIT第3行DIAG=3标记本版并携带定界容量：`DIAG=3,CAPT=8544,L=2048,R=2048`。每个完整搜索任务在完成时把候选附近有界PCM快照经UDP 5008→PC 6008（目标192.168.10.100）分片下传，并输出一行元数据，示例不是实测：

```text
$CHIRP_CAPTURE,ID=24,SESSION=1,FIRST=3500000,FRAMES=6496,CLAMP=0,TAIL=1,PKTS=13,TOTAL=13,RESULT=OK
```

- ID/SESSION与CHIRP_SEARCH同源；FIRST为该切片首样本的绝对PCM帧（uint64），FRAMES为实际复制双声道帧数，范围0..8544，不承诺6496点的名义跨度。
- CLAMP为0/1，表示左或右端至少一端被裁剪到已验证范围；TAIL为0/1，表示任务完成时用新到验证块补充过右尾；PKTS为实际成功提交的数据报数，TOTAL为该切片期望分片数，RESULT=OK/LINK_DOWN/PCB_ERROR/BIND_ERROR/NO_PBUF/SEND_ERROR。RESULT只表示LwIP栈接受状态。
- UDP负载为56字节网络大端头+小端int16 PCM：`PCM1`魔数、version=1、kind=1、flags（bit0左裁剪/bit1右裁剪/bit2补过右尾）、保留字节、u32分片序号、u32总分片数、u32本片样本数、u32保留、u32 ID、u32 SESSION、u64本片首帧、u64 HINT帧、u64 raw峰帧；随后最多512个int16样本。PC按键(session,id)重组，缺片不产未完成文件。
- 捕获内容为选中声道、经End验证的CPU副本，在任务缓存时刻复制（左2048起点/hint+6496右端，按已验范围两端裁剪），完成时仅右尾按新到块补丁；不读取DMA、不影响搜索/匹配结果与耗时语义。仅在任务完成时产生，与匹配与否无关。

此接口仅用于区分“声学失真/多径”与“门限/模板/对齐”（离线宽带相关+带限变体+静音负样本分布），不提供PTP时间或到达精度，也不能直接据此下调0.80门限。

## M4.2.7-A 单板声到达时刻（SBCG-M4.2-PCMCAP 增容）

`$SOUND_CAND` 追加尾字段（旧固件无此字段，示例不是实测）：

```text
$SOUND_CAND,SEQ=20,SESSION=1,CH=0,FIRST=3000000,CONFIRMED=3000063,E_AC=4096,PEAK=8000,CLIP=0,LOCAL=1,SYNC=0,TOA_US=62500333
```

- TOA_US 为候选窗前沿过阈线性插值后的声到达时刻，按本板会话帧栅格×1000000/48000 换算为近端微秒并四舍五入；位同 FIRST/插值分数之和，只在检测窗幅值门槛成立时有效，失败为 `NA` 且不影响其余字段。
- 插值阈值=max(128, 窗峰/4)；窗取自 onset 起 32 帧，不足 8 帧或无过阈返回 `NA`。`SOUND_STATE` 不变，服务快照新增 TOA_OK/TOA_MISS 计数（串口状态行未逐项列出）。
- 该时刻是“本板计时基的到达时刻”，用于重复播放抖动评估（≤±100μs 口径）与双板 ΔT 测距的前置；本接口不提供 PTP 绝对时间，双板换算后续复用 SoundEventTime 锚点模型。
