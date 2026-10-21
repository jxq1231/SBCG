# UDP应用协议

当前板运行已通过本版本授权烧录/联合实物验收的SBCG-M2.2-OBS，见[M2.2.4实测](Milestones/M2_2_Board_Observation.md)。原M2.2.2时间戳回报作为旧基线已通过实物验收。详见[30组样本实测](Milestones/M2_2_Time_Exchange.md)。

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

详见[M2.2.4实现及验收边界](Milestones/M2_2_Board_Observation.md)。当前SBCG-M2.2-OBS已获本版本授权烧录、校验、复位并实测30组通过；不表示双板同步或精度已达标。


## T1.1 温度与声速串口观测（R1正常窗口通过，拔插恢复未测）

本节记录已实测的FW=SBCG-T1.1-TEMP-R1；R0实测FW=SBCG-T1.1-TEMP。当前T1.2已获本版本授权烧录及联合实测，见下节。仅当前测试UID启用DHT11，旧UDP type1～7不变，无温度网络消息。

```text
$TEMP,STATE=OK,VERSION=1,T_MILLI_C=24400,RH_MILLI_PCT=53000,C_MM_S=346086,AGE_MS=0,FRESH=1,LAST=OK
$TEMP_STATS,ATT=2,OK=1,PRIME=1,INIT=0,BUS=0,START=0,TIMEOUT=0,PULSE=0,CLOCK=0,CRC=0,RANGE=0,FORMAT=0
```

以上为格式示例，非实测。各行ASCII并以CRLF结束，每秒最多各一条；本模块起始低电平与捕获期间延后日志。整数均为十进制：T_MILLI_C为有符号32位毫摄氏度，RH_MILLI_PCT为无符号32位毫%RH，C_MM_S为无符号32位毫米/秒，AGE_MS为uint32毫秒。VERSION及统计为饱和uint32；STATE/LAST为字符串，FRESH为0/1。

UNREADY/禁用时VERSION=0且不输出T/RH/C/AGE；首次读数仅预读，下次5秒周期正式更新。失败保留原值及版本，FRESH=0；HOLD_LAST表明历史快照，15秒以上为STALE。DISABLED表示测试UID或logger门控未满足。LAST分别为INIT/BUS/START/TIMEOUT/PULSE/CLOCK/CRC/RANGE/PRIMING/OK/DISABLED。INIT统计包含一次性初始化失败，CLOCK统计运行期时钟错误；FORMAT仅本地格式错误，UART发送错误归既有HEARTBEAT/TX_ERRORS。R1在起始低电平期间将所有应用日志放入16项FIFO，释放后每轮至多发送一条；队列溢出也计入TX_ERRORS。

声速为331300 + round_signed(606*T_MILLI_C/1000)，单位mm/s，恰半远离零。仅为干空气线性温度近似，湿度只观测，不参与模型；输出分辨率不是DHT11精度。接口与实机要求见[T1.1记录](Milestones/T1_1_Temperature.md)。

T1.1实物状态（2026-10-21）：R1经本版本授权烧录，17个正式版本均为T_MILLI_C=25700、RH_MILLI_PCT=25000、C_MM_S=346874，错误计数0，原网络联合窗口通过。用户延后拔插，因此上述HOLD_LAST/STALE及重预读恢复目前只有逻辑测试覆盖，未实物验收；不将正常采样通过扩展为全部故障行为通过。

## T1.2 手动温度命令（本版本实物通过）

命令为 ASCII 文本、区分大小写，以 LF 或 CRLF 结束，LF 前最多 63 字节（含可选 CR）。支持 `PING`、`TEMP?`、`TEMP SET <T>`、`TEMP AUTO`。温度允许可选负号、1～2 位整数及可选一位小数，范围 -20.0～60.0℃；额外空格、参数、指数及更多小数位拒绝。每轮最多 64 字节/一条命令，命令开始处理最小间隔 100 ms。请等待响应后再发下一条。

SET/AUTO/PING 成功返回 `$CMD,NAME=SET|AUTO|PING,RESULT=OK`；失败返回 `$CMD,NAME=SET|AUTO|INVALID,RESULT=ERROR,REASON=VALUE|UNAVAILABLE|COMMAND|FRAME`。VALUE 为温度格式/范围错误，UNAVAILABLE 为服务禁用/忙或版本耗尽，COMMAND 为未知命令，FRAME 为过长/坏字节/串口缺口后的重新定位。错误不改变设定值或版本。

TEMP? 返回 `$ENV` 和 `$CONSOLE`；SET/AUTO 成功后也返回 `$ENV`。ENV 顺序为 MODE、SOURCE、VALID、VERSION，VALID=1 时追加 T_MILLI_C、C_MM_S、AGE_MS。MODE=AUTO/MANUAL；SOURCE=DHT11/MANUAL/NONE，NONE 表示当前无可用值。VALID=0 时省略温度/声速/年龄，不应以旧值替代。数值宽度和单位沿用 T1.1；VERSION uint32 仅在有效发布时递增，切换到 AUTO 不重置版本。达到 UINT32_MAX 时停止发布新值，自动 LAST=VERSION；手动 SET 返回 UNAVAILABLE。

手动模式暂停 DHT11，值不自动过期、不持久化、不输出伪造湿度；每秒 `$TEMP` 为 STATE=MANUAL，并有 MODE=MANUAL、SOURCE=MANUAL。切换 AUTO 清除手动有效值，保持与最后一次采集至少 5 秒间隔并重新预读一帧，第二帧合格才更新。等待期间 UNREADY 可以带非零历史版本。正常 AUTO 的 `$TEMP`/`$TEMP_STATS` 格式保持原样。

CONSOLE 字段 RX_ERRORS、RX_OVERFLOWS、RX_REARM_ERRORS 都是饱和 uint32 事件计数：HAL 错误、环形缓冲溢出、续收启动失败。256 字节缓冲实际可存 255 字节。任何缺口作废队列和当前行，丢弃至 LF，再接受新命令；必要时先发送空行重定位。续收失败主循环最多每 10 ms 单次重试。HAL 回调不解析/打印；DHT 起始低电平期间命令响应推迟，既有日志队列保护仍生效。

本版本 FW=SBCG-T1.2-MANUAL，已完成手动保持/零温/负温/非法拒绝/分段/恢复自动及原网络联合实测，旧 UDP 协议及缓存配置未改。使用步骤与测试范围见 [T1.2](Milestones/T1_2_Manual_Temperature.md)。
