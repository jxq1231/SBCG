# M2.2.4 板端实时估算观测

<!-- doc-status: completed-evidence; reviewed: 2026-10-23 -->

> **已归档的增量验收记录。** 仅证明本页所列固件、窗口和测试范围；不是当前待办。后续进展以现行状态页为准，旧版本的未测项和失败不改写为已通过。 当前配置/任务见[项目状态](../Project_State.md)，阅读范围见[文档导航](../README.md)。

日期：2026-10-21。用户批准实施，基线b246fba。**SBCG-M2.2-OBS已获本版本明确授权并完成一次烧录、校验、复位和联合实物验收：30组板端结果与真实C参考逐条完全一致；旧网络业务通过。当前仍为单板LOCAL/SYNC=0。**

## 目标与实现

单一目标：原时间交换完成后，电脑补交原t1和首个有效ACK的t4；当前板用同键保存的RX/TX硬件时间在主循环运行原C估算器。30组板端整数结果已与电脑真实C参考逐条一致。保持LOCAL/SYNC=0，只观察，不校时。

新增type7占用原5007/6007服务，不新增PCB。只有REPORT发送返回OK的缓存可入队一次；提前、缺失、重复、冲突和队列满分别统计。队列为4条CPU副本，原created_ms起2000ms到期；主循环每次至多处理一项，结果输出最多1Hz。原C窗口固定800000000ns，错误行不附带旧估算值。详见[协议](../Protocol.md)及[批准设计](../Plans/2026-10-21_Board_Sync_Observation_Design.md)。

## 文件与调用关系

- `App/Sync/sync_observation.c/.h`：CPU队列、限速、真实C调用、整数文本及统计。
- `App/Network/udp_time_exchange.c`、`udp_config.h`：type7解码、成功配对资格和一次领取。
- `App/app_main.c`：SBCG-M2.2-OBS标识、Init/Process；根`CMakeLists.txt`注册新用户模块。
- `Tests/Sync/test_sync_observation.c`、`run_tests.py`：真实C、FIFO/过期/限速/回绕及64位文本。
- `Tests/TimeExchange/test_udp_time_exchange.c`、`run_tests.py`：原包装链与type7集成。
- `Tests/App/test_app_main.c`：初始化与主循环顺序、重初始化及旧调度。
- `Tools/Monitor/time_exchange_probe.py`及`tests/test_time_exchange_probe.py`：显式提交与默认兼容。
- `Docs/Protocol.md`、`Project_State.md`、`README.md`、本报告、设计及实施清单，根`README.md`：协议、当前状态和验证边界。

```text
App_Init → 旧服务/PTP初始化 → SyncObservation_Init → UDPTimeExchange_Init
App_Process → MX_LWIP_Process → 旧业务/PTP → UDPTimeExchange_Process → SyncObservation_Process
type7 UDP回调 → 成功REPORT记录完整键匹配 → SyncObservation_Submit（仅复制）
SyncObservation_Process → SyncEstimator_Calculate → 有界十进制文本 → SendLine → USART1
```

No new interrupt relationship。NO_SYS输入/输出链和原SysTick不变；回调不计算、不格式化、不发送串口。

No new DMA relationship。只复用原ETH内部帧DMA及BSP硬件快照；队列不与DMA共享。I/D Cache仍关闭，不增加维护操作；MPU、描述符、池和栈配置不变。

No CubeMX change required。不改.ioc、Core、HAL、生成网络胶水、BSP或旧PTP观察器。数学内核sync_estimator.c/.h保持原文。

## 软件验证

已观察到测试先失败：旧服务拒绝type7；App缺少观测初始化；Sync Submit stub无法入队；电脑工具尚无提交行为。对应RED/GREEN及后续回归证据保存于忽略目录`Logs/M2_2_Board_Observation/`。

板端native回归已通过133组：App9、Network19、Udp6、UdpProbe7、Ptp38、Sync20（原13组/1044次估算器调用加新7组）、TimeExchange34（原26加新8组）。新测试覆盖真实旧包装链、单次领取、提前/失败/缺失/过期、重复/冲突、队列满、复制所有权、完整键、来源、tick和限速回绕。

完整回归248项全部通过：上述133组、Monitor73项、Analysis42项。相对于原219项新增29项（Sync7、TimeExchange8、Monitor14）。规范审查之后分别进行了板端和电脑工具质量审查，均通过。

实际Generate Code覆盖457文件，逐字节无差异；只将被CubeMX删除的LAN8742许可证从快照原样恢复。再生成后两配置再次构建通过，均无新警告，ETH布局均通过，120个C编译单元全部统一HAL_ETH_USE_PTP。最终ELF中已保留SyncEstimator_Calculate、SyncObservation_Process/Submit和UDPTimeExchange_Process，估算器不再被链接器裁除。

| 构建 | ELF SHA256 | Flash使用（链接报告） |
| --- | --- | --- |
| Debug | `5afe448fa40839ee133abed084011d0081e07dba7c5bda1e8a902fcae3ae3af0` | 70820字节 |
| Release | `439d418d11cfd25b93270a1707eb76d99587556de35c77991e1e8b3bdc38e44c` | 44948字节 |

原6份M2.2.2实物证据哈希未变，原数学内核哈希未变。证据包含tests.json、build-verification.json、regeneration.json及capture-preparation.json。使用带准确SHA门控的有时限采集及真实C联合比较脚本完成本版本实测；未改变固件、原证据或数学内核。

## 本版本授权烧录与联合实物验收

用户明确批准SBCG-M2.2-OBS及Debug SHA256前缀5afe448fa408的烧录和自动验收。只烧录一次，沿用`.vscode/openocd-stm32f746.cfg`，执行`program {build/Debug/SBCG.elf} verify reset exit`；OpenOCD退出0，观察到Programming Finished、Verified OK和Resetting Target。没有擦除配置或选项字节修改。

COM3使用115200 8N1、DTR=0、RTS=0；三路UDP同时采集。完整原文前缀为`Logs/UDP/m224-obs-1792566156000-*`。串口1011行与原始二进制逐字节一致，无启动前丢弃或末尾残行；只观察到一次新BOOT，HCLK=200000000、UID=001A002D:30365105:36363631。

| 检查 | 本次结果 |
| --- | --- |
| 新时间交换 | 30个唯一请求、ACK、REPORT、完整四时刻样本及type7本地提交；SESSION=10E3E89CD498C692 |
| 板端运行计算 | ACCEPTED=QUEUED=CALC_OK=30；30条STATUS=0结果，完整键全部匹配 |
| 真实C对照 | 对新模式完整样本直接调用既有build_cli/run_estimator；30组状态、OFFSET_X2_NS、DELAY_X2_NS逐条完全相同 |
| 旧5005 | 70.000秒收到70个唯一包，SEQ=6～75；缺口/重复/乱序/非法/异源全0，相邻tick均1000ms |
| 旧5006/6006 | 30发30收；30组串口RX/TX/ACK完整匹配，超时及错误0 |
| 连续运行 | 90条心跳、90条PTP状态连续；TX_ERRORS=0、LOCAL/SYNC=0，PTP时间单调 |
| 异常统计 | OBS_RX、SYNC_OBS_STATS、TS_IO、TS_PAIR及旧RX/TX统计中的所有异常、溢出、过期及格式错误均0 |
| 启动边界 | 只有启动SEQ=1在PHY尚未UP时跳过，发生在70秒观察窗之前；窗口本身无缺口 |
| 关闭采集 | 三个工具均退出0，串口线程已结束、COM3已关闭，板保持运行 |

实测首条板端结果：

```text
$SYNC_OBS,SESSION=10E3E89CD498C692,REQ_SEQ=1,CLIENT_TICK_MS=5633548,UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631,STATUS=0,OFFSET_X2_NS=-11256401969620,DELAY_X2_NS=424860,SYNC=0,PC=SW,BOARD=HW
```

两个整数是二倍纳秒的分子；偏差包含电脑与板各自参考点之差，不能用它判断UTC误差或同步精度。PC软件RTT最小/平均/最大为489700/582633.333333/713900ns；板ACK TX−请求RX为182160/191332/202760ns。它们分别描述本次往返/板端周转，不是同步误差。

验收记录为`Logs/M2_2_Board_Observation/runtime-acceptance.json`，逐条对照在`runtime-comparison.json`，真实C构建/输入/输出在`runtime-native-reference/`。额外重新运行真实C并检查输入与原始样本一致，结果不变，457份烧录时冻结文件及6份本次原始证据哈希通过复核。电脑工具原summary仍为hardware_verified=false；只有包含固件、串口和原始网络证据的本次联合记录限定范围为hardware_verified=true。

## 复验

保持电脑物理以太网192.168.10.100/24与当前板相连，释放占用COM3的网页串口页面。重新烧录新版本仍须按项目规则取得相应授权。新模式可单独复验：

```powershell
python -B Tools/Monitor/time_exchange_probe.py --uid 001A002D:30365105:36363631 --count 30 --submit-observation --log Logs/UDP/board-observation-01.jsonl
```

此命令不烧录、不改网络或时钟。电脑summary PASS仅表示本地采集/提交干净，不证明板端收到或算对；hardware_verified仍为false。不能用新模式数据伪装旧M2.2.3输入。

本次联合验收直接对完整样本调用`Analysis/sync_exchange_analysis.py`的真实C适配器，以SESSION、UID、SEQ、CLIENT_TICK_MS关联串口，30组状态及OFFSET_X2_NS、DELAY_X2_NS完全相同；新旧异常计数0，旧网络70包无缺口、30组RX/TX/ACK匹配、至少60条PTP/心跳连续。保留完整原文及哈希。

Flash/Runtime：PASS。本功能已按jxq提交并依已有协作授权推送，远程核对一致，本增量停止。两板同步收敛、±10μs精度、PPS与声波测距仍需后续独立验收。

## 本次原始证据SHA256

| 文件 | SHA256 |
| --- | --- |
| `Logs/UDP/m224-obs-1792566156000-serial.jsonl` | `20b9839f6afa4b45e0aa0cf43d8d7380ad8fc9926d07cebd344ed5a820e3223e` |
| `Logs/UDP/m224-obs-1792566156000-monitor.jsonl` | `654ec6ecf3879cc9fc6ae0220fa49c6abfb6b5a6af540584cbb44a3fb418a309` |
| `Logs/UDP/m224-obs-1792566156000-probe.jsonl` | `0f9d80a9d37d963366d690bfa49b3a0c72dc3110055b051b5a3bb35a78171a79` |
| `Logs/UDP/m224-obs-1792566156000-time.jsonl` | `0b282320714350e6f21b9ab8903eecdc393a3d64b1953acbd432c5bf610fa4a2` |
| `Logs/UDP/m224-obs-1792566156000-flash.log` | `361c78ce615ef8feceb0a3ccaa37468c31c3c6cdd4c61f8b0e7c1a164499d03e` |
| `Logs/UDP/m224-obs-1792566156000-serial.bin` | `43722f279e4a74f1dcfdf6607dbe880edd0b2324aaa7e99dc2cf8b454e321fba` |

## Git交付

功能提交：[8ecaea3](https://github.com/jxq1231/SBCG/commit/8ecaea36248632d088f8285224d6423a0249f759)，消息`feat: add verified board clock observation`。作者与提交者均为jxq，邮箱保持仓库既有配置。已正常推送到`jxq/m0-clock-review`，远程提交号核对一致。原始日志按规则留在忽略目录，未上传到源码仓库；本报告保存范围、计数和哈希供协作复验。
