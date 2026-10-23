# M2.3.2 双板硬件时钟闭环：软件交付与1分钟运行验收

<!-- doc-status: runtime-short-window-verified; reviewed: 2026-10-23 -->

本增量按用户要求一次实现时差/漂移观测和B硬件校时。软件及独立复核通过后，**用户报告两板已手动烧录；本轮只读采集验证了1分钟运行中的内部闭环与原业务**。未由助手重复烧录/复位。用户将原10分钟窗口明确缩短为1分钟；不把本次记录称作长期稳定性、冷启动收敛或外部同步精度验收。当前批准范围见[方案](../Plans/2026-10-23_Dual_Board_Clock_Closed_Loop.md)。

## 2026-10-23 手动烧录后的短时实物结果

- 本机识别A=COM3/ST-LINK序列066EFF3832394D3457253156、B=COM5/066DFF485153826687084153。两板均可ping，ARP对应.1/MAC01和.2/MAC02；原UDP应答核对真实UID。串口读到A REFERENCE、B闭环及校正日志，未为补BOOT而复位。未读回Flash计算哈希，不能把冻结ELF哈希当作实物已读回证明。
- A串口60.1768秒、B串口60.1704秒、后续网络60.0001秒，三个窗口部分重叠，**不是同起同止60秒**。B采集开始时已内部锁定，所以首次上电到锁定耗时、初始相位步进方向未在本次窗口验证。没有追加10分钟采集。
- B取得236组四硬件时刻，全部与真实`SyncEstimator`原生C输出及Fraction重算一致。协议估算B−A时差−65～+340ns，平均路径时延11920～12085ns。来自同一协议及对称路径假设，**不是外部误差或声学测距精度**；RAW与CTRL没有共享样本ID，不声称二者逐样本对齐。
- B60条CTRL中35条LOCKED_INTERNAL、25条TRACKING，FAULT/HOLDOVER均0；8次FREQUENCY请求STATUS=0，频率记录−1457～−1413ppb，残余漂移估计−61～+35ppb，epoch固定2、rate_revision9→17，REJECT/ERRORS均0。变频后按设计清除连续锁证据再获取，不能表述为全程连续锁定。仅负向频率修正实际被本窗口观察；正频率/正负大相位仍只在软件测试覆盖。
- B本地PTP相对HAL毫秒历时保持约1秒/秒，排除了本窗口明显的fine倍率2倍/半倍；两者同源，不能证明绝对晶振准确度。A维持REFERENCE、0ppb且无硬件校正请求。
- 后续稳定网络窗口：A SEQ145～204、B71～130，各60个唯一UDP包，零缺口/重复/乱序/身份冲突。原probe A/B各15请求全应答；原time exchange A/B各15组ACK/REPORT全齐，A另15次观测提交计算成功。
- 两板各60条音频位置连续，GAP/DROP/ERR/FAULTS均0；codec各30次ID8994且无错。A自动温度13个版本28→40、26.6～27.1℃，无采集错误；B按UID禁用本地温度，属于预期范围。
- 原音频模型复核：A20组、B14组与原生C/Fraction一致，最大绝对留中残差分别11393/11106ns。A首组SEQ48所需左/中锚点在采集开始前，仅排除该明确边界组；未修改历史分析器的BOOT/90秒/30组门槛来制造通过。
- raw bin与JSONL投影逐字节核对，A开头一行是打开串口时的残片，明确保留；B无前缀残片，两端均无不完整尾行。采集窗口内无BOOT；不等价于覆盖窗口外重启。COM3/COM5均已关闭。

**保留的初始异常：** `manual-normal-01/dual-udp.jsonl`结果FAIL，B45次接收中37个落在初始序号基线之前，不能被改为PASS或删除。发生于USB识别/初始连通准备阶段，序号回退原因未证实。后续独立的稳定窗口`manual-normal-02/dual-udp.jsonl`PASS；只在该窗口断言网络无序号错误。

证据目录：`Logs/TimeSync/M2_3_2/manual-normal-01/`、`manual-normal-02/`；核对程序`validate_manual_acceptance_01.py`、结果`manual-acceptance-01/acceptance.json`，含raw哈希、边界排除及原生C结果。总状态`PASS_M232_SHORT_RUNNING_LOOP_AND_ORIGINAL_SERVICES`，外部精度/冷启动/故障注入均未验证。日志仍按规则忽略，不纳入源代码。

下一阶段可独立验证从上电开始的获取、断网/对端复位恢复及PPS外部精度；本次不自动要求拔插或复位。


## 配置与范围

- 用户手工CubeMX MEMP_NUM_UDP_PCB=6并Generate Code，已核对.ioc及lwipopts一致、USER区保留，无GPIO/RCC/DMA/NVIC变化。再生成删除的原LAN8742许可已按HEAD原样补回。
- 用户明确批准自有BSP fine运行时扩展：A不校时；B10ns、nominal addend0x80000000、绝对±100000ppb，20ms/100000轮询双预算。B步进使用新鲜读时+有符号偏差得到绝对目标，以TSSTI覆盖，不使用HAL相对更新/改写HAL。
- 固定单播PTPv2子集，UDP319/320，domain0/port1。四时刻全部ETH硬件来源，拒绝非零correctionField；不是完整BMCA/第三方互操作栈。
- 原三路UDP、温度、codec、PCM采集保留。只清除旧配对/时间历史，PCM DMA不重启；LTDC/TouchGFX/PPS/检测/测距不在本次实现范围。

## 调用链及异步关系

```text
App_Init → PtpClock_Init（A coarse / B fine）
         → 原网络观察器与UDPTimeExchange_Init → TimeSync_Init
App_Process → MX_LWIP_Process（包装内同步捕获RX/TX）
            → 原网络/温度与温度命令
            → TimeSync_Process（按本轮DHT状态放行）
              → PtpBoardSync_Process / TakeExchange
              → SyncEstimator_Calculate → ClockServo_Observe
              → TX空闲/旧消费者失效 → BSP_PTP_Step / SetFrequencyPpb
              → ClockServo_Commit（只认真实BSP结果）
            → codec / 音频 / 锚点 / 模型
```

卸载必须先TimeSync/PTP外层，再UDPTimeExchange内层；双钩子所有权不符或活动回调时失败，不盲恢复。B健康重装保留已确认频率，未知/失效硬件不假定0ppb。

**IRQ：无新增中断关系。** PTP网络回调在NO_SYS前台同步执行，校正不在包装回调内。原USART1 RX及SAI2/DMA2_Stream7保持。

**DMA：无新增配置。** ETH继续增强描述符硬件戳/实际单缓冲TX令牌；SAI2 B接收继续DMA2 Stream7 Channel0、halfword circular、32768元素/64KiB、半满/满交接。Cache仍关闭；无新clean/invalidate。原布局/缓冲区所有权不变。

## 控制状态与不合格数据

正时差表示B领先A，正相位推进B、正频率加快B。获取至少3个一致样本，初始偏差超过1ms允许一次受控步进；后续频率限幅。连续8个合格样本、偏差≤10us、真实残余漂移≤500ppb、平均路径≤2ms才内部锁定。锁定后的频率稳定窗口约8秒，使本地音频模型可重建。

过期/估计失败立即撤锁并清连续证据，**不刷新最后有效样本时间**；2秒无有效样本HOLDOVER、5秒UNSYNCED，保留已确认频率。时钟命令失败/结果不确定/硬件记录不符FAULT，不能靠后续报文自动抹掉。对端重启/长失联清历史并重新获取，6秒隔离依赖有界网络/前台推进；不承诺跨掉电唯一会话或恶意重放保护。

`INTERNAL_LOCK=1`仅为本协议内部观测，所有`SYNC=0`保持；没有PPS或外部仪器，不能声称独立±10us验收。

## 软件验证

| 检查 | 本次结果与证据 |
| --- | --- |
| 原生模块 | 15组运行器退出0；Logs/TimeSync/M2_3_2/native-regressions.json及各模块*-regression.txt |
| 真C伺服 | 9412断言；10组±50ppm、正负初始偏差与100ns抖动仿真，首次内部锁10.25～17.5秒；纯数学仿真，不是实物误差 |
| 协调层 | 12组；真实硬件动作门控、失效顺序、fresh tick、非零频率重装及持续坏样本2/5秒状态 |
| 主机工具 | 113项unittest通过（含5项新串口采集测试） |
| 离线分析 | 98项unittest通过（含11项新四时刻/闭环分析测试） |
| Debug/Release | 现有autodebug/CMake preset构建通过，无新增编译诊断；debug-final-build.txt/release-final-build.txt |
| 两种布局 | ETH与音频四次检查均退出0；layout-final.json |
| 内存 | Debug Flash128736B，Release75028B；固定MSP4KiB、LwIP保留窗/描述符/音频区互不重叠。链接97.02%为含固定高地址区的占用跨度，不代表静态变量接近主栈；Debug BSS末0x20004FB0、音频0x20010000..0x20020000，newlib堆在音频之后 |
| 栈检查 | 通过现有Debug preset临时开启-fstack-usage，记录stack-usage及stack-final-build；新控制器/估计器无大局部数组，最终已恢复原C_FLAGS并重建。库内部/真实最坏中断嵌套仍不由静态单函数记录证明 |
| 独立复核 | 两项P2已红灯复现并修复：拒绝时撤锁；拒绝不刷新最后有效tick。复核解除阻断；git diff --check通过 |

## 冻结程序

A/B使用同一份ELF，启动按UID选择身份。

- Debug: `build/Firmware/SBCG-M2.3-SYNC/SBCG-M2.3-SYNC-Debug.elf`
  SHA256 `8fea8c54cc2767761c4eeaaa384a38041a5da677d24638af718173c6537f0b76`
- Release: `build/Firmware/SBCG-M2.3-SYNC/SBCG-M2.3-SYNC-Release.elf`
  SHA256 `ca5b89440a5bfbb7368e908e495f340096ce7f42967f9b170412612313500600`

manifest位于`build/Firmware/SBCG-M2.3-SYNC/manifest.json`，同时绑定App/Core/LWIP/CMake/.ioc源文件哈希，基线提交6920312，原冻结时标记UNCOMMITTED_AWAITING_HARDWARE_ACCEPTANCE；本次按短时实物证据限定范围提交，构建目录不进入Git。

## 原软件交付时的验收步骤（历史计划；本次用户改为1分钟）

1. 原计划依据AGENTS第20节先明确授权本版A烧录/校验/复位与串口/UDP采集；用户随后自行烧录两板，本次不再执行烧录，沿用原ST-LINK配置和UID守卫，不改选项字节。B由用户/队友烧录上列同一Debug ELF，并核对完整SHA256、BOOT UID、ROLE=B/.2；旧ROLE版本无法参加新PTPv2交换。
2. 两板/PC保持原交换机2/1/3口。A/B各采集串口至少620秒，记录BOOT；先验证Bfine读时倍率及校正方向，再观察闭环。串口独占，采集前关闭SerialWeb对应端口。
3. A例：`python -B Tools/Monitor/serial_monitor.py --port COM3 --duration 620 --log-prefix Logs/TimeSync/M2_3_2/a-normal-01`。B将COM3替换其实际端口，用b-normal-01前缀；采集只读，不自动复位，不覆盖已有日志。bin/JSONL/元数据均须保留。
4. 同时运行既有dual_board_monitor和两节点probe/time_exchange_probe，分别核对UID及端点。对原PC交换避免跨校正边界强求旧配对成功；拒绝应可见，稳定分段再复验至少30次；A温度、两板音频/codec及新时钟错误分别记录。
5. 分析B：`python -B Analysis/board_sync_analysis.py --serial Logs/TimeSync/M2_3_2/b-normal-01-serial.jsonl --out Logs/TimeSync/M2_3_2/b-normal-01-analysis.json`。Fraction精确重算raw四时刻；CTRL无共享样本ID，与RAW无法逐样本对齐，工具明确标记。不能以“采集结束”代替验收。
6. 正常窗口目标首次内部锁≤30秒，记录全部收敛/拒绝/变频/重新建模过程。断网及对端复位恢复另通知用户配合，单独保留故障窗口；外部同步精度留待PPS/仪器独立验收。
7. 根据真实证据记录结果再以jxq提交当前单一功能，不自动开展下一里程碑。

## 本增量文件清单

- `Analysis/board_sync_analysis.py`
- `Analysis/tests/test_board_sync_analysis.py`
- `App/Audio/audio_time_anchor.c`
- `App/Audio/audio_time_anchor.h`
- `App/Audio/audio_time_map_monitor.c`
- `App/Audio/audio_time_map_monitor.h`
- `App/BSP/bsp_ptp.c`
- `App/BSP/bsp_ptp.h`
- `App/Network/ptp_board_sync.c`
- `App/Network/ptp_board_sync.h`
- `App/Network/ptp_wire.c`
- `App/Network/ptp_wire.h`
- `App/Network/udp_time_exchange.c`
- `App/Network/udp_time_exchange.h`
- `App/Sync/clock_servo.c`
- `App/Sync/clock_servo.h`
- `App/Sync/ptp_clock.c`
- `App/Sync/ptp_clock.h`
- `App/Sync/sync_observation.c`
- `App/Sync/sync_observation.h`
- `App/Sync/time_sync.c`
- `App/Sync/time_sync.h`
- `App/app_main.c`
- `CMakeLists.txt`
- `LWIP/Target/lwipopts.h`
- `SBCG.ioc`
- `Tests/App/run_tests.py`
- `Tests/App/test_app_sync.c`
- `Tests/BoardSync/run_tests.py`
- `Tests/BoardSync/test_ptp_board_sync.c`
- `Tests/BoardSync/test_ptp_wire.c`
- `Tests/ClockEpoch/run_tests.py`
- `Tests/ClockEpoch/test_anchor.c`
- `Tests/ClockEpoch/test_exchange.c`
- `Tests/ClockEpoch/test_map.c`
- `Tests/ClockEpoch/test_observation.c`
- `Tests/ClockEpoch/test_ptp_clock.c`
- `Tests/ClockEpoch/test_ptp_role.c`
- `Tests/ClockServo/README.md`
- `Tests/ClockServo/run_tests.py`
- `Tests/ClockServo/test_clock_servo.c`
- `Tests/Ptp/run_tests.py`
- `Tests/Ptp/stubs/main.h`
- `Tests/Ptp/test_bsp_ptp_fine.c`
- `Tests/SyncController/run_tests.py`
- `Tests/SyncController/test_time_sync.c`
- `Tools/Monitor/serial_monitor.py`
- `Tools/Monitor/tests/test_serial_monitor.py`

文档入口、当前状态、协议、资源、架构及原M2.3.1网络证据同步更新；历史实验结果不改写。
