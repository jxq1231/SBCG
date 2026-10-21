# T1.1 单板DHT11与整数声速：首次实测与R1修复

2026-10-21：当前板已按本版本授权烧录SBCG-T1.1-TEMP-R1（Debug SHA前缀5ee566f1d7c1），**正常采样及原网络联合窗口通过**。用户明确“先不做拔插测试”，本次只收尾正常采样子增量，**拔插/STALE/恢复未实测，T1.1全部故障验收尚未完成**。R0的7次START失败及其修复过程作为历史证据保留。

## 接线

| 传感器丝印 | 主板信号 | 官方排针 |
| --- | --- | --- |
| + | 3.3V | CN6-4 |
| − | GND | CN6-6或CN6-7 |
| S | PG7 / Arduino D4 | CN4-5 |

用户近照传感器正面由左到右为+、−、S，分别接蓝色扩展板3.3V、GND、4/PG7。接口示意图中左侧一列三针口最上方标4/GND/5V，最右侧GND/4是两针口；用户已反馈引脚/供电处理完成，并明确更正当前实际供电为3.3V。因此前一轮依据旧图5V标注给出的5V接法不适用于当前实物。维持原3.3V方案，不修改MCU电源、时钟或GPIO配置。供电为用户反馈的3.3V，尚未独立仪表量测电压/上拉；正常温度数据读取已由R1实测确认，未进行校准温度精度鉴定。三芯线以端点丝印为准，不按红白颜色认极性。

## 实现和资源

App_Process在旧业务之后调用TemperatureService_Process，随后以FlushOneLine发送已允许的积压日志，经BSP_Start拉低，后续循环等待20ms后Capture，再经Dht11Frame_Decode与SoundSpeed_Calculate更新快照，经SendLine/BSP_Console_Write输出。App_Init在旧服务初始化之后加入TemperatureService_Init；旧wrapper所有权失败时仍先退出，不重置温度服务。

GPIO为用户CubeMX生成的PG7开漏High/NOPULL/LOW，代码仅检查并使用标签，不重新配置模式。DWT只作协议计时，开启跟踪/周期计数但不复位CYCCNT。未新增TIM、NVIC、IRQ、DMA、cache或MPU关系；未改HAL、生成网络或旧PTP配置。CPU私有帧/快照不交给DMA。

起始低电平用HAL tick调度20ms；BSP用GPIO写前后周期界限检查18～30ms，服务另以HAL tick拒绝长停顿，防止DWT完整回绕别名。所有捕获结束路径释放DATA，失败不覆盖原帧。轮询期限6ms、单边沿150us，读取GPIO后再次检查时限；IRQ保持使能，因此任意抢占下不保证6ms墙钟上限。R0中旧业务同步串口输出可以延长低电平；R1按下面的有界日志队列修复，保持时序阈值与网络调度，R1正常窗口已复验通过，故障恢复仍待后续。

## R0软件准备证据（历史）

- 原248项回归通过（App9、Network19、UDP6、Probe7、PTP38、Sync20、TimeExchange34、Monitor73、Analysis42）。
- 新温度测试：帧1190检查、模型320206检查、真实BSP十组、服务1116检查；覆盖负号/小数/0℃、输出不变、超时、总期限、GPIO读写内抢占、DWT及HAL tick回绕、重初始化和故障恢复。App9组同时新增温度顺序及所有权断言。
- Debug/Release构建及ETH布局均通过，无新增编译警告；124个C单元统一HAL_ETH_USE_PTP；原sync_estimator.c/.h哈希保持。
- 实际CubeMX再生成474个既有文件逐字节保持；仅恢复生成器删除的LAN8742许可证原字节。该次快照早于最后两个测试扩展，后续只扩展测试，固件源不变。
- 独立规范与质量复核通过；实物风险保留至实测。

证据位于忽略目录Logs/Temperature/T1_1/：regressions.json、regression-*.log、software-build.json、generate-result.json、pregen.zip、software-sources.json；RED/GREEN日志保留。两配置ELF SHA256：

```text
Debug   67e1520e28d120a754f90444017f8e3a9b881360714b6d0674667c97a951a24c
Release deb8768b37c8219cd26adc089002ed24979a01fb5457f889084ebeb56b45c550
```

## 文件变动

- `App/BSP/bsp_dht11.c`
- `App/BSP/bsp_dht11.h`
- `App/Temperature/dht11_frame.c`
- `App/Temperature/dht11_frame.h`
- `App/Temperature/sound_speed.c`
- `App/Temperature/sound_speed.h`
- `App/Temperature/temperature_config.h`
- `App/Temperature/temperature_service.c`
- `App/Temperature/temperature_service.h`
- `App/app_main.c`
- `CMakeLists.txt`
- `Core/Inc/main.h`
- `Core/Src/main.c`
- `SBCG.ioc`
- `Tests/App/test_app_main.c`
- `Tests/Temperature/run_tests.py`
- `Tests/Temperature/stubs/main.h`
- `Tests/Temperature/test_bsp_dht11.c`
- `Tests/Temperature/test_dht11_frame.c`
- `Tests/Temperature/test_sound_speed.c`
- `Tests/Temperature/test_temperature_service.c`

文档更新：本文件、Docs/Project_State.md、Docs/Protocol.md及两份T1.1设计/执行计划。生成的三个PG7文件属于用户本次CubeMX工作，保留原改动；许可证恢复后与HEAD一致。

## 实物验收要求

1. 用户已反馈接线完成，实际供电为3.3V；传感器放在通风处，远离主板热源。电压/上拉及校准精度尚未独立仪表量测，R1正常数据读取已经实测通过。
2. 取得上述T1.1 Debug固件的明确烧录/验收授权后，沿用.vscode/openocd-stm32f746.cfg烧录、verify、reset，不改选项字节。此前M2.2.4批准不代替T1.1批准。
3. 自动采集COM3（115200 8N1）及旧三路UDP；至少12个正式有效温度样本，逐条检查模型、版本、年龄、异常计数，与30组时间交换及原心跳/PTP并行核对。记录实际调度间隔，不硬套正好1000ms。
4. 用户配合断开/恢复传感器，验证HOLD_LAST→STALE、重预读及正式恢复，无虚假温度。异常率或原业务退化必须原样记录并修复/重新评估，不能借旧版本证据判通过。
5. 实物结果通过后再更新记录、以jxq提交当前增量、按既有正常推送授权推送，然后停止。

构建复验：`python -B Tools/Monitor/autodebug.py build --preset Debug`（Release同理）；布局：`python -B Tools/Monitor/check_eth_layout.py --preset Debug`。R0首次实测未通过；当前R1正常窗口已通过，不将两版证据混用。


## R0继续前的复核（历史）

用户确认实际3.3V接法后，逐文件检查21个固件/测试文件与已审查快照完全一致。重新运行温度测试与App9组、Debug配置构建及ETH布局，均通过；Debug ELF仍为上列67e1520e28d1，未因接线说明改动代码。当前GPIO仍为PG7/DHT11_DATA、High、OD、NOPULL、LOW。无需再次修改CubeMX。

自动验收准备位于忽略目录Logs/Temperature/T1_1/：run_hardware_acceptance.py沿用既有ST-LINK配置和COM3采集，先核对完整ELF及源文件/辅助脚本哈希，拒绝错误哈希；analyze_hardware_acceptance.py联合核对至少12个不同温度版本、整数声速、原30组请求和30组四时刻/板端C结果。90秒正常窗口与后续断开/恢复窗口分别验收。当时语法及错误哈希拒绝检查通过，尚未运行串口采集或烧录；后来R0/R1实测见下文。确认记录见preflash-review.json和capture-guard-test.json。

正常窗口的UDP与心跳调度步长原样保留，辅助分析以1000～1020ms作本轮回归观察门槛；该门槛不是课程同步精度指标。异常计数、温度错误或网络缺项均需复核，正常窗口PASS也不能代替后续HOLD_LAST/STALE/恢复验证。


## R0首次授权实测：未通过

用户明确回复“授权”后，使用已审查Debug ELF 67e1520e28d1，沿用原ST-LINK配置完成Programming/Verified OK/Reset。采集原始前缀为`Logs/UDP/t11-temp-1792591834874`，原始串口共1192行；记录在`Logs/Temperature/T1_1/attempt-01/`归档原版本脚本指纹、固件指纹、运行记录及observed-result.json。

- 温度ATT=18、PRIME=1、OK=10、START=7，其余温度错误0。前7次尝试均START，PC两组30请求结束后自动恢复，取得版本1～10；末次25900毫摄氏度、25000毫%RH、346995mm/s。每个已输出有效值的整数声速关系检查通过，仍未达到至少12版本且无异常的正常窗口要求。
- 5005实收70包，缺口/重复/乱序/非法0，UDP tick步长1000ms；原6006请求30/30，专用6007四时刻30/30。30组板端观测与真实C整数结果完全一致。上述检查在联合分析到达温度门槛失败前已通过；不能用它们把T1.1整体标PASS。
- 未做传感器断开/恢复；不创建feature-complete提交。原始失败记录保留，不通过延长采集或剔除前7次失败改写为正常通过。

### 诊断边界

密集串口阶段与START错误相关；应用SendLine此前只检查长度，直接调用同步HAL UART，即使TemperatureService仍持有起始低电平也允许输出。新增回归先复现“低电平期间发生UART发送”，再修复。该代码缺陷得到确认，但R0日志没有直接记录每个低电平宽度，不宣称它是所有START错误的唯一原因；ETH等阻塞路径仍可能使实际低电平超窗，须在R1联合采集验证。

## R1修复及正常窗口验收通过

FW=`SBCG-T1.1-TEMP-R1`。功能变更仅5个文件：App/app_main.c、App/Temperature/temperature_service.c/.h、Tests/App/test_app_main.c、Tests/Temperature/test_temperature_service.c。

- 新增主循环只读查询TemperatureService_IsStartLowActive()。SendLine在起始低电平期间或已有队列时复制日志到16项×256字节固定FIFO，不持有调用方栈指针；长度上限仍255字节，满队列计入TX_ERRORS且不强制发送。
- 每轮TemperatureService_Process之后，确认DATA已释放才至多发送一条积压日志，保持FIFO顺序。队列仅占CPU私有静态内存（数据/长度/索引合计4136字节），未交给DMA。
- App_Init仅在原wrapper所有权前置检查通过后清空旧队列，Temp_Init释放旧起始状态后再发送排队BOOT；所有权失败保持原温度/队列状态。
- 旧MX_LWIP、UDP、PTP及TimeExchange调用顺序不变；没有20ms空等、暂停网络、放宽脉宽、关中断或新增硬件资源。主循环链为旧网络/PTP→TemperatureService_Process→FlushOneLine；IRQ/DMA/cache/MPU/CubeMX均无新配置关系。

R1软件验证：原248项加6组App队列测试共254项通过；温度帧1190检查、模型320206检查、真实BSP十组、服务1296检查通过。App新增队列RED证据为app-quiet-window-red.log，修复后15组App通过。两配置构建及ETH布局通过，无新增未解释警告；124个C单元统一PTP宏，旧数学内核及生成配置哈希未变。实际Generate Code对475文件逐字节保持，仅恢复原LAN8742许可证；再生成后两配置构建/布局再次通过。独立R1代码复核无待修发现。

```text
R1 Debug   5ee566f1d7c1572aea152182e49a7fb46a2acfbb7703edd60c6d26949c543444
R1 Release 9f48e0a4ebf4fca87ae6d599e778ba3ab341dc5d1dd8011af95b48cd768e548f
```

证据与待执行采集准备在`Logs/Temperature/T1_1/r1/`：verification.json、verification-*.log、software-build.json、generate-result.json、preflash-review.json、capture-guard-test.json。新的采集脚本绑定R1完整SHA与437个非文档项目文件及辅助脚本指纹；错误SHA在打开设备前被拒绝。随后用户明确回复“烧录”，R1已按该授权运行烧录和正常采集。用户又明确延后拔插，本轮版本控制仅记录正常采样子增量，不作完整故障恢复验收结论。


## R1本版本授权实测：正常窗口通过

使用上述5ee566f1d7c1 Debug ELF及原.vscode/openocd-stm32f746.cfg完成Programming Finished、Verified OK、Reset。原始前缀`Logs/UDP/t11-temp-r1-1792592618876`，串口原始数据1192行；采集完成后串口与UDP进程均已关闭，未再复位或重复烧录。

| 核对项 | 本轮结果 |
| --- | --- |
| 温度 | ATT=18、PRIME=1、OK=17，正式版本1～17连续；全部25700毫摄氏度、25000毫%RH、346874mm/s，模型逐条一致 |
| 温度及串口错误 | INIT/BUS/START/TIMEOUT/PULSE/CLOCK/CRC/RANGE/FORMAT全部0；HEARTBEAT/TX_ERRORS=0 |
| 原单向UDP | 70包，缺口/重复/乱序/非法/外来0，tick步长1000ms |
| 原请求/应答 | 30/30，唯一RX/TX/ACK全部配对，超时/异常0 |
| 新时间交换与观测 | 30/30唯一ACK/REPORT/四时刻；30条板端结果与真实C参考逐条一致 |
| 启动与连续性 | 固件/UID/HCLK匹配；90条PTP和90条心跳连续，LOCAL/SYNC=0，错误及溢出0；启动仅SEQ1链路未就绪跳过 |
| 调度观察 | 心跳相邻tick最小1000ms、均值1000.169ms、最大1007ms；未据此宣称两板同步或测距精度 |

证据：`Logs/Temperature/T1_1/r1/baseline-acceptance.json`、`runtime-run.json`、`runtime-comparison.json`及各原始串口/UDP文件哈希。工具结论保持`PASS_BASELINE_FAULT_RECOVERY_PENDING`，与用户要求的测试边界一致。17个版本是17次校验通过的新采样快照，不表示环境温度发生17次变化；实际读数均为25.7℃。

### 启动边界的原始字节说明

采集在复位前已经打开串口，旧固件被复位截断的3字节`$SY`与新BOOT拼在同一行，实际字节为`$SY$BOOT,FW=SBCG-T1.1-TEMP-R1,...`。原始二进制与JSONL逐字节重构完全一致，未编辑原始记录或重新烧录来掩盖这个边界。

原冻结分析脚本按行首查找BOOT因此拒绝该窗口。新增忽略目录辅助分析`analyze_hardware_acceptance_resync.py`只在唯一目标BOOT处重新识别启动边界，记录排除的前缀十六进制245359；后续再出现任意BOOT仍拒绝。正常、带前缀、缺失、重复、后续隐藏重启和超长前缀共7项解析检查通过。旧分析脚本与预烧录437文件指纹保持不变，新解析脚本独立记录哈希。分析记录为analysis-resync-check.json及RED/GREEN日志；原始数据哈希保持。

## 用户延后的测试与交付范围

用户在正常窗口通过后明确要求“先不做拔插测试”。未启动拔插采集、未要求用户拔线、未改动接线；不继续该测试，也不自动推进新里程碑。`delivery-scope.json`记录`fault_recovery_status=DEFERRED_BY_USER`及`full_T1_1_complete=false`。

本次正常采样子增量可作为团队复现基线记录：3.3V、S→PG7/D4，COM3/115200 8N1，观察FW=SBCG-T1.1-TEMP-R1及TEMP/温度版本/统计。原UDP/PTP功能只覆盖上述正常窗口。以后恢复测试需重新采集并核对HOLD_LAST→STALE、原值/版本保留、重预读和正式恢复；当前单元测试覆盖这些逻辑，但未把它们记为实物通过。
