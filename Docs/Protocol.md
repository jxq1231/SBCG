# UDP应用协议

M2.2.2已实现，通过主机验证及本版本授权烧录/联合实物验收；当前板运行SBCG-M2.2-IO。详见[30组样本实测](Milestones/M2_2_Time_Exchange.md)。

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

工具PASS仅表示本次网络样本检查通过；hardware_verified恒false，实物结论必须由串口BOOT、板状态、新旧网络采集与固件哈希联合建立。本阶段不调用估算器，不校时或宣称同步精度。实现边界及联合验收要求见[批准设计](Plans/2026-10-20_Time_Exchange_IO_Design.md)。
