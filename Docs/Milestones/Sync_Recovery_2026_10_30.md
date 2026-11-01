# B板瞬态同步故障恢复：软件验证记录

基线：057dcb1，分支jxq/ui-navigation。本轮仅实现B板同步协调器的已知硬件状态恢复。**软件验证及独立审查通过；2026-11-01已烧录B板并验证一次断网后无需复位重新同步，随后90秒稳定；2026-11-02通过临时注入Fault锁存点完成FAULT恢复分支实物验收。恢复增量已在7048baa提交，标定增量已在4c65a85提交，作者和committer均为jxq；未push。** 500mm物理标定和CLAP/WAVE播放验收仍待实机完成。

## 已复现的问题与修复

原实现一次本地读时失败或控制动作失败后，协调器和伺服锁存FAULT。即使硬件状态后来恢复正常，后续完整交换也不能解除故障。新增主机测试先在“1000ms后应ACQUIRING”的断言处失败，再由本轮代码通过；不是只通过编译推测修复。

现在仅B板在故障后每至少1000ms检查一次：TX清理完成→fine硬件状态已知→PPS资格撤销成功→旧协议、声音队列、音频锚点/模型和观察窗口撤销→按已确认硬件频率InitKnown→回到ACQUIRING。本次恢复不写时钟，也不消费恢复前的旧交换。偏差继续无效，后续新四时刻才能重新TRACKING。累计有效/拒绝样本和错误计数保留，恢复尝试/成功次数单独记录。

只读BSP检查复用既有CheckFineUpdate与Read，核对fine模式、200MHz HCLK、addend匹配、无TSSTI/TSSTU/TSARU未决命令及本地快照。不修改寄存器或代际。

## 明确边界

- 硬件命令实际超时后，既有BSP会关闭PTP时钟。此时仍保持FAULT；本轮没有自动重新初始化时钟或复位MCU。
- NOT SYNCED是B伺服连续约5秒没有接受到有效样本，FAULT为另一路状态。UNSYNCED本身已有重新获取路径，不能把本轮修复当作所有NOT SYNCED现场现象的唯一原因。
- A的LOCAL REFERENCE不证明板间通信正常。只读现场采集时，A连续25秒报告ETH LINK=DOWN；电脑有线网卡Disconnected，仅COM3可用，无法观察B的故障过程。
- 不增加软件“复位按钮”或伪造同步资格；不修改原2秒HOLDOVER、5秒UNSYNCED、协议隔离窗口、同步参数、识别阈值或测距合同。

## 修改文件与调用关系

- `App/BSP/bsp_ptp.c/.h`：只读BSP_PTP_ReadKnownFrequency接口。
- `App/Sync/time_sync.c/.h`：恢复节拍、资格检查、消费者失效、重新获取及诊断。
- `Tests/Ptp/test_bsp_ptp_fine.c`：健康检查正反例、失败输出保持及零寄存器写入断言。
- `Tests/SyncController/test_time_sync.c`：瞬态读时失败、持续失败、重试节拍/门控、TX暂忙、旧消费者清理、硬件关闭、A隔离及HAL tick回绕。
- `Tests/PpsIntegration/test_controller_pps.c`：PPS撤销失败阻止恢复；恢复本身不提供新Observe或旧锁定。
- `Tests/SoundEventTime/test_clock_time.c`：重新获取前撤销旧声音时间模型和交换。
- `Docs/Plans/2026-10-30_Sync_Recovery.md`与本记录。

调用：`App_Process → TimeSync_Process → 故障后1000ms节拍 → TxClean / ReadKnownFrequency → PPS Invalidate / InvalidateConsumers → ClockServo_InitKnown → 后续新四时刻重新获取`。

**No CubeMX change required。无新IRQ/DMA关系。** 沿用既有ETH描述符清理，不改ETH/SAI/LTDC、GPIO、TIM、NVIC、DMA、HAL或cache配置。未修改生成区。此前18个标定源码/测试文件与原软件验证SHA256全部一致。

## 验证证据

本轮证据目录：`Logs/UI/U3_3/sync-recovery-en8coq31/`，Logs不提交。

- `red-controller.txt`：旧实现对新增恢复断言失败，退出码3。
- `green-controller.txt`及`final-controller.txt`：恢复入口实现后的主机行为断言通过；最终21组输出包含持续读失败不延后退避的回归。
- `tests.json`：8个相关runner全部exit 0。最后追加共享fixture测试后，`final-tests.json`中的4个受影响runner再次全部exit 0。
- BSP子任务：`Logs/UI/U3_3/bsp-recovery-792e53p1/`。最初RED为新增API缺失的编译失败，随后寄存器模拟通过；不把该编译失败描述成行为断言失败。
- 独立只读审查未发现P1/P2问题；之后补充的PPS和声音模型集成断言均通过。

运行命令：

```text
python -B Tests/SyncController/run_tests.py
python -B Tests/PpsIntegration/run_tests.py
python -B Tests/UI/run_snapshots.py
python -B Tests/SoundEventTime/run_tests.py
python -B Tests/ClockServo/run_tests.py
python -B Tests/BoardSync/run_tests.py
python -B Tests/App/run_app_udp_integration.py
python -B Tests/Ptp/run_tests.py
cmake --build --preset Debug --parallel 4
cmake --build --preset Release --parallel 4
```

两种预设各运行音频、ETH、TouchGFX前端布局检查，共6项全部通过。真实A/B应用主机编排75项断言通过，包含既有标定功能；协调器恢复测试替代协议/伺服依赖，BSP测试模拟寄存器，均不等于实物同步恢复或声学精度验收。

| 构建 | FLASH | ELF SHA256 |
|---|---:|---|
| Debug | 362160B | 932fca2ce4a2309dbcc05fb5c7efcf0ee34d85e8a8fc57f2226b125054494588 |
| Release | 231164B | 1b6a48fef280d9c3699388b01936208bcb34d60d8953fc6a44ddae0cc9edfcac |

ITCM仍14688B，前端SDRAM仍5792B/8192B；音频和ETH固定保留区通过布局检查。新增恢复计数为3个uint32_t，不改变保留区映射。构建仅显示既有RWX链接警告，无新增编译警告。BOOT版本文字未另改，镜像身份以本表SHA256为准；A保留此前974ed3cd…标定镜像，B于2026-11-01更新为上表Debug镜像。

## 实机验证记录（2026-11-01）

用户指令“测试”，适用AGENTS.md第20节允许明确测试工作流中的烧录。已确认COM3/A和COM5/B的ST-LINK序列号，沿现有配置仅烧录B，使用`adapter serial 066DFF485153826687084153`选择登记探针。镜像SHA256为上表Debug版本，OpenOCD退出0、Verified OK；没有改选项字节或A板程序。

证据：`Logs/UI/U3_3/sync-recovery-20261101-6uai06bb/`。180秒主采集包含烧录前状态和B启动后134.8秒，再以独立文件追加90秒双板观察；原始串口bin/jsonl、B-flash.json/txt和analysis.json均保留，不覆盖旧run。新固件BOOT中的B UID为`00380050:35385104:37313532`，主采集中只有该次预期烧录启动，后续90秒无启动。无需主动播放声音。

用户确认仅拔插B网线，保持供电且不复位。主采集相对秒数：

| 观测 | 秒数 |
|---|---:|
| B ETH LINK=DOWN | 150.017916 |
| B ETH LINK=UP | 159.643566 |
| B重新TRACKING | 167.266691 |
| A DOMAIN VALID=1 | 169.049012 |
| B DOMAIN重新有效 | 169.070460 |
| B域短暂撤销后稳定有效 | 173.083407 |

链路中断约9.6秒；从LINK=UP到TRACKING约7.6秒、到B共同域稳定有效约13.4秒。期间出现HOLDOVER→UNSYNCED→ACQUIRING→HOLDOVER→UNSYNCED→TRACKING的重获取过程，未复位。域令牌均为3171538039。

随后90秒：A的90条同步记录均REFERENCE；B的89条记录为TRACKING 34条、LOCKED_INTERNAL 55条，无HOLDOVER/UNSYNCED/FAULT，无DOMAIN变更。两板AUDIO DROP/ERR、UI ERRORS、PPS FAULTS/MISSED/ERRORS均0，SYNC_CTRL ERRORS/REJECT均0。B内部偏差估计最大绝对值365ns；这仅为闭环日志，不是新增示波器精度验收。

**结论：本次单次断网后自动恢复通过。** 没有`$SYNC_RECOVERY`记录、没有FAULT，因此不能据此宣称新加FAULT恢复分支已经实机验证；它目前仍由主机故障注入测试证明。主机同步协调器21组输出和PPS集成23组输出在本次操作前重新通过。现有长期偶发“一次错误后不恢复”是否还包含其他原因，仍须保留首次异常现场继续验证。

## 实物注入FAULT验收（2026-11-02）

自然断网只产生UNSYNCED，不能触发FAULT；B板ST-LINK在`reset_config srst_only srst_nogate connect_assert_srst`下无法运行时halt，调试器改寄存器不可行。经用户批准，使用临时worktree `/d/electronic_design/SBCG-fault-inject`（基线dfd4b4d）在`CheckClockGeneration`的B分支注入一次性`Fault()`锁存点，仅烧B板；测试代码未提交、未进入主工作树。

证据：`Logs/Acceptance/fault-inject-20261102T012022/B-serial.jsonl`（COM5，80s）。

| 相对秒数 | 观测 |
|---:|---|
| 23.47 | `$FAULT_INJECT` |
| 23.87 | `$SYNC_CTRL,ROLE=B,STATE=FAULT,...,ERRORS=1`；PPS UNQUALIFIED，MISSED=1 |
| 24.47 | `$SYNC_RECOVERY,ROLE=B,STATUS=0,ATTEMPTS=1,RECOVERIES=1,FREQ_PPB=-709,EPOCH=2,RATE_REV=7,SYNC=0` |
| 24.87 | STATE=ACQUIRING |
| 25.87 | STATE=TRACKING |
| 27.88 | STATE=LOCKED_INTERNAL；PPS重新QUALIFIED |

全程`$AUDIO DROP=0`。恢复链路“故障锁存→撤销旧消费者/PPS→每秒重试→重新获取已知频率→重新ACQUIRING/TRACKING”实物PASS，恢复约1s，PPS约3s重新合格。边界：本轮注入的是`Fault()`锁存点，不是真实PTP硬件读失败触发；检测路径仍由主机测试覆盖，真实硬件故障触发未制造。

随后使用主工作树`build/Debug/SBCG.elf`（sha256 `5c15ed3c87c0dbc120b7a132c05ab7e4b80df579f68d035ddee3f90071e0c21d`，HEAD dfd4b4d）恢复B官方镜像；`Logs/Acceptance/restore-b-20261102T012205`确认0条`$FAULT_INJECT`，正常HOLDOVER→UNSYNCED→ACQUIRING→TRACKING→LOCKED_INTERNAL。A板未烧测试镜像。

## 后续无需发声实机步骤

1. 两板连接同一交换机，确认两板ETH LINK=UP；B的ST-LINK USB接入电脑，确认UID及COM5，A为COM3。先捕获故障，避免复位丢证据。
2. 本版B板烧录及双板采集已完成，无需重复烧录。A保留原标定版本；协议和参考时钟行为未改。
3. 正常时检查B TRACKING/LOCKED_INTERNAL、共同域恢复及两端无新音频错误。网络短暂中断后应自动重获取；不能仅因没有触发FAULT，就宣称FAULT路径硬件已测通过。
4. 若自然复现FAULT，等待硬件恢复后观察`$SYNC_RECOVERY,ROLE=B,STATUS=0,...RECOVERIES=`增长，然后ACQUIRING→新样本TRACKING。恢复期间旧距离必须撤销，不直接复活旧结果。
5. 若仍是NOT SYNCED而非FAULT，检查两端`$ETH`、`$PTP_SYNC_STATS`、`$PTP_SYNC_DIAG`、`$SYNC_CTRL`、`$SYNC_ADJUST`、`$PPS`和`$PROFILE`，继续定位报文/硬件戳缺在哪一步。

Git：恢复代码、测试及本记录基线已在7048baa提交，作者和committer均jxq；2026-11-02的FAULT验收补记另行提交；均未push。标定及高级UI增量已在4c65a85提交；用户原有LICENSE删除、Analysis/、旧Docs和nul保持未动。
