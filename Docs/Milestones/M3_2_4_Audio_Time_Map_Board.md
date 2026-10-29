# M3.2.4 板端时间估算接入

<!-- doc-status: completed-evidence; reviewed: 2026-10-23 -->

状态：**实现、回归、Debug/Release构建/布局、独立复核及Debug实物验收均通过。** 用户批准SHA前缀9921f102cc60后，仅沿原配置烧录一次、校验复位成功。120.190秒获得120个有效锚点、40组MAP/CHECK与真实C/Fraction一致，最大绝对留出残差14.884us。当前已验收固件SBCG-M3.2-MAP；[实施计划](../Plans/2026-10-23_Audio_Time_Map_Board_Implementation.md)已归档，不重复执行。

## 单一目标与调用链

新增SBCG-M3.2-MAP，将既有纯C估算器接入前台。每个原锚点尝试发布单槽CPU快照；无效尝试的会话、位置和时间清零。新服务每3个连续有效锚点以第1/3点估算第2点的整数NDTR相位中点，输出估计区间和观测残差。不是声音检测或PCM事件时间查询API。

App_Init → AudioCapture_Init → AudioTimeAnchor_Init → AudioTimeMapMonitor_Init。
App_Process → 原音频处理 → AudioTimeAnchor_Process → AudioTimeMapMonitor_Process → TakeObservation → 原AudioTimeMap_Estimate → SendLine。

DHT起始低电平期间不领取/计算/输出；断序、跨会话、年龄>250ms、无效/倒序或内核拒绝会清空窗口，失败输出不带旧时间。有效新点按协议重建；不额外取PTP、不读PCM或保留DMA指针。三点快照均为CPU整数副本，无IRQ调用或动态分配。

## 文件

| 文件 | 目的 |
| --- | --- |
| App/Audio/audio_time_anchor.c/.h | 类型化一次领取快照，原AUDIO_ANCHOR格式和取时逻辑保留 |
| App/Audio/audio_time_map_monitor.c/.h | 三点窗口、拒绝/恢复、有界64位日志 |
| App/app_main.c | 初始化/前台顺序及SBCG-M3.2-MAP标识 |
| cmake/audio-layout.cmake | 用户扩展登记新服务和原纯C内核，启用SBCG_USE_AUDIO_MAP |
| Tests/Audio/test_audio_time_anchor.c、test_audio_time_map_monitor.c、run_tests.py | 快照、失败清零、正负残差、Init/NULL、DHT/序号/tick及日志；Audio共10程序 |
| Tests/App/test_app_main.c、run_tests.py | 调度/门控及固件标识回归 |
| Analysis/audio_anchor_analysis.py | 可选预期FW参数，旧默认保持 |
| Analysis/audio_time_map_board_analysis.py、tests/test_audio_time_map_board_analysis.py | 新BOOT/UID、音频位置/锚点、MAP/CHECK顺序和真实C/Fraction全值核对 |
| Analysis/serial_capture_integrity.py、tests/test_serial_capture_integrity.py | raw二进制与JSON投影逐行核对，限定复位边界，新BOOT后严格ASCII |
| Docs/Plans/2026-10-23_Audio_Time_Map_Board_Implementation.md、本报告及现行入口/协议/工作流 | 当前实现、实物结果与模型边界 |

## IRQ、DMA、Cache与CubeMX

No new interrupt relationship。仍为原SAI/DMA回调交接；新快照、计算、格式化全部在前台。
No new DMA relationship。SAI2-B RX/DMA2 Stream7 Channel0，halfword、circular、32768元素/64KiB、半满/满各8192帧及IRQ4/0保持。音频区0x20010000..0x20020000、ETH内存和4KiB主栈保持。
**No CubeMX change required。** GPIO/TIM/NVIC/ETH/SAI/I2C/ADC/LTDC、主时钟/PLLI2S、缓存配置和生成代码均未改变；I/D Cache仍关闭，无新增维护。

## 已完成软件验证

- 快照/monitor缺实现、App调度未调用及新分析器缺失时均先观察测试失败，再实现。
- 10组原生模块回归通过，Audio包含10个程序；Tools83项、Analysis最终87项通过（M3.2.3原78＋新板端检查5＋复位证据检查4）。
- 独立只读复核发现CHECK可跨下一源锚点的P2漏检；先保存错序负例失败，再增加源锚点前必须完成CHECK的约束，最终独立重跑5项新工具测试通过。审查范围内无未解决P1/P2。
- Debug和Release沿现有autodebug/CMake预设构建通过；两配置ETH/audio布局通过，无新增编译warning。
- nm确认两种ELF均包含AudioTimeMap_Estimate、AudioTimeMapMonitor_Process和AudioTimeAnchor_TakeObservation；新FW标识存在，纯C内核源码未改。
- Debug FLASH111484B，Release64872B。新增静态CPU缓冲/窗口由布局检查约束；原音频DMA区不变。
- ARM编译器.su：Debug main8、App_Process192、Monitor_Process72、Calculate312、core352、ValidAnchor24字节；该算术路径已知静态栈合计960B，不含库函数/异常栈，不能当作完整最坏栈或实物水位证明。Release Monitor_Process288、core88B。

Debug SHA256：`9921f102cc605a63d162655474bf98732f132a7be901f3372787155f619f20ad`。
Release SHA256：`99566cbc92a16c935e2ece7890c0ec5b696330bf95ea4b55d3c3045bce726253`。
证据在Logs/Audio/M3_2_4/software-01、native/app/analysis-red、check-order-red及review_m324；主机检查恒hardware_verified=false。

可复验的软件操作（仅构建/分析，不触碰设备）：

```text
python -B Tests/Audio/run_tests.py
python -B Tests/App/run_tests.py
python -B -m unittest discover -s Analysis/tests -v
python -B Tools/Monitor/autodebug.py build --preset Debug
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Debug
python -B Tools/Monitor/check_audio_layout.py --preset Debug
python -B Tools/Monitor/check_eth_layout.py --preset Release
python -B Tools/Monitor/check_audio_layout.py --preset Release
```

实物重点观测`$AUDIO_MAP,STATUS=OK,VALID=1`与同SEQ的`$AUDIO_MAP_CHECK`；L/M/R对应连续三源锚点，所有估计/区间/残差需与真实C和Fraction逐值一致，不只检查日志出现。

## 授权烧录与实物验收

用户明确批准本版本SHA后，沿现有.vscode/openocd-stm32f746.cfg执行一次`program {build/Debug/SBCG.elf} verify reset exit`，program/verify/reset全部通过，不改选项字节。复位后120.190秒采集COM3 115200/8N1（DTR/RTS关闭）和三路UDP；保持3.3V/PG7温度接线，无发声、拔插、改线或第二板实验。结束后串口、接收线程和所有UDP采集进程均已关闭。

| 检查 | 实物结果 |
| --- | --- |
| BOOT/UID | SBCG-M3.2-MAP，200MHz，001A002D:30365105:36363631；新BOOT后无再次启动 |
| AUDIO_POS | 120对、SEQ1～120、SESSION1，无编号/连续性/丢块错误，最终5767168帧 |
| AUDIO_ANCHOR | 120尝试全部有效、拒绝0；112个WIDTH1、8个WIDTH0；相对本地PTP率中点47991.070529185Hz |
| MAP/CHECK | 40组，与原锚点三点窗口、真实C及Fraction全部逐值一致；残差−14884～+13200ns，最大绝对值14884ns，小于预定100000ns健全性门槛 |
| 音频DMA | 120条有效状态，HAL半满/满各352次，累计5767168帧，无drop/error |
| UDP5005 | 110.001秒收到110包，SEQ6～115，缺口/重复/乱序/非法/外来均0；启动链路未就绪只跳过SEQ1 |
| 旧请求/应答、RX/TX | 30组ACK及RX/TX硬件戳关联全部通过，计数无错 |
| 新时间交换/原板端估算 | 30组ACK/REPORT/四时刻，30个板端估算与真实C一致，所有原计数错误0；仍LOCAL/SYNC=0 |
| 温度 | 23个有效版本，24次尝试含1次PRIME；24.4～24.8℃、33%RH，声速整数公式一致，全部错误计数0 |
| Codec | 60次ID8994、HAL_ERR及ERRORS均0；读取0～1ms，轮询2000～2068ms |
| 前台周期 | 心跳1000～1015ms，网络tick间隔985～1017ms；仅记录实际周期，不宣称精密定时 |

验收门槛在烧录前已冻结：至少90有效锚点/位置、30组MAP/CHECK，残差绝对值≤100000ns，UDP110秒至少100包零缺口，两组各30请求及原业务无新增错误。实施计划中早期UDP≥70的宽松条目已由烧录前criteria明确收紧为≥100；本次110包满足两者。未根据实测结果调参，纯C内核、固件和既有488份冻结源码/配置哈希保持不变。

## 原始证据与复位边界

- 原始数据：Logs/Audio/M3_2_4/runtime-01/capture-serial.bin/.jsonl、capture-monitor/probe/time.jsonl、capture-flash.log、runtime-run.json；raw-freeze.json记录采集结束时10个文件哈希，分析后逐项一致。
- 串口raw SHA256：`2b3bab95173aa85c19f0b7c078b520e4c245946cf5ffc84e9ed30e0dbaa97072`。共2072行；新BOOT前14完整旧行及同一行前63字节旧残片排除，仅该残片末字节为0xff。新版完整窗口2058行全为ASCII，唯一新BOOT，无不完整尾部。
- 第一次分析试图把JSON中的替代字符回编码ASCII，产生UnicodeEncodeError，保存于analysis-initial-failure.txt。原JSON是raw的ASCII替代投影，不能独自还原0xff；新增离线检查器逐行验证raw→JSON一致并保存原始prefix hex，新BOOT后任何非ASCII/缺行/篡改/重复BOOT均拒绝。仅修复证据解释，不修改固件或原数据，不再次烧录。
- 新增4项检查先缺模块失败后通过；最终Analysis87项全绿。software-01/preflash-review.json作为烧录前历史冻结原样保留；新增离线文件和分析脚本哈希另记postflash-integrity.json。
- 联合报告acceptance.json为`PASS_M324_BOARD_MAP_AND_ORIGINAL_SERVICES`，hardware_verified=true仅适用于本次授权实物窗口。独立复核重算raw哈希、边界，并重新编译真实C验证40组一致；离线工具本身始终hardware_verified=false。复核未发现未解决P1/P2。

## 范围与后续边界

本轮完成前台三点留出自检接入。无新IRQ/DMA/Cache关系，CubeMX生成配置保持不变。仍LOCAL/SYNC=0；模型区间只含线性进度下游标量化/取整，不是声学误差界。14.884us残差不能当作声音到达精度、双板同步精度或测距精度。Release只构建、未烧录；故障恢复负例来自软件回归，本轮未做实物拔插。

完成本增量后停止，不自动启动声音事件查询、声学校准、检测、PPS/双板或LCD/TouchGFX。
