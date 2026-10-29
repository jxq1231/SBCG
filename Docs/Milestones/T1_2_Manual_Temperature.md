# T1.2 串口手动设温与自动恢复

<!-- doc-status: completed-evidence; reviewed: 2026-10-23 -->

> **已归档的增量验收记录。** 仅证明本页所列固件、窗口和测试范围；不是当前待办。后续进展以现行状态页为准，旧版本的未测项和失败不改写为已通过。 当前配置/任务见[项目状态](../Project_State.md)，阅读范围见[文档导航](../README.md)。

更新：2026-10-22。**应用代码、主机测试、构建及本版本90秒联合实物验收通过。** 当前板运行 SBCG-T1.2-MANUAL，用户明确授权 SHA256 前缀 824d3c3cecaa 的版本后烧录一次、校验并复位。独立审查工具本轮无法运行；以下仅包含本地逐项复核和测试结果，不冒充独立复核通过。

## 目标和边界

本增量只让当前单板从 USART1 接收温度查询、手动设值及自动模式切换。保持现有 UDP type1～7、PTP 本地观测及 DHT11 自动采样路径。当前开发 UID 为 001A002D:30365105:36363631。

用户已通过 CubeMX 启用 USART1 global interrupt，优先级 5/0；PA9 TX、PB7 RX、115200 8N1 及 PG7/D4 温度数据线、实际 3.3V 供电保持原样。没有新增定时器、DMA、网络消息、LCD、第二板或校时控制。按用户要求不做传感器拔插测试。本增量完成也不能代表课程 B5 的 LCD/测距部分完成。

## 使用方法

打开 [SerialWeb](https://conductance-lab.xyz/SerialWeb/)，选择 ST-LINK VCP（本机此前 COM3），115200、8N1、无流控。发送文本，开启结尾补换行并选 CRLF；不要发送字符串字面量 `\r\n`。每次发送后等待响应，建议间隔至少 200 ms。

| 命令 | 行为 |
| --- | --- |
| `PING` | 返回 `$CMD,NAME=PING,RESULT=OK` |
| `TEMP?` | 返回 `$ENV` 当前有效值、模式、来源和版本，及 `$CONSOLE` 接收错误统计 |
| `TEMP SET 25.0` | 手动 25.0℃，声速 346450 mm/s |
| `TEMP SET 0` | 手动 0℃，声速 331300 mm/s，零温是有效输入 |
| `TEMP SET -10.0` | 手动 -10℃，声速 325240 mm/s |
| `TEMP AUTO` | 恢复 DHT11 自动采样；已经 AUTO 时幂等 |

数值允许一个负号、1～2 位整数、可选一位小数；范围 -20.0～60.0℃。`+25`、`25.00`、`1e1`、`nan`、多余空格/参数及越界值拒绝，原模式、数值和版本不变。命令区分大小写。手动值保留到下次 SET、AUTO 或复位，不保存到 Flash。

以下仅为格式示例；本次真实版本号和值见文末实测记录：

```text
$CMD,NAME=SET,RESULT=OK
$ENV,MODE=MANUAL,SOURCE=MANUAL,VALID=1,VERSION=1,T_MILLI_C=25000,C_MM_S=346450,AGE_MS=0
$CMD,NAME=AUTO,RESULT=OK
$ENV,MODE=AUTO,SOURCE=NONE,VALID=0,VERSION=1
```

手动模式停止 DHT11 采样，手动值不按 15 秒自动过期；周期 `$TEMP` 输出 STATE=MANUAL，不伪造湿度。MANUAL→AUTO 清除手动有效值、保留版本计数，等待 DHT11 重新预读一帧，下个合格帧才发布自动值。与上次采集起始保持至少 5 秒间隔，正常恢复需约 5～10 秒。期间 `$ENV` 为 VALID=0、SOURCE=NONE，省略温度/声速。自动样本错误/过期时查询也省略不可用值；原 `$TEMP` 仍可显示 HOLD_LAST/STALE 历史记录供诊断。

## 文件清单

| 文件 | 作用 |
| --- | --- |
| App/BSP/bsp_console.c、App/BSP/bsp_console.h | USART1 单字节中断接收、有界缓冲、错误/续收恢复与统计；保留同步发送 |
| App/Console/temperature_console.c、App/Console/temperature_console.h | 文本分帧、温度语法、命令分派、限流和查询响应 |
| App/Temperature/temperature_service.c、App/Temperature/temperature_service.h | 统一当前环境快照、手动/自动切换、有效性和版本 |
| App/app_main.c | 初始化接收、主循环调度命令，FW=SBCG-T1.2-MANUAL |
| CMakeLists.txt | 用户拥有的源文件注册点，加入命令模块 |
| cmake/linker/stm32f746_eth.ld | 用户链接扩展预留 4 KiB 主栈，容纳主循环格式化及串口中断嵌套 |
| Tools/Monitor/check_eth_layout.py、Tools/Monitor/tests/test_eth_layout.py | USART1 IRQ 启用时检查主栈至少 4 KiB，并检查符号一致 |
| SBCG.ioc | 用户通过 CubeMX 添加 USART1_IRQn 5/0，本增量未手改 |
| Core/Inc/stm32f7xx_it.h、Core/Src/stm32f7xx_it.c、Core/Src/stm32f7xx_hal_msp.c | 用户生成的 USART1 中断声明、HAL 分派与 NVIC 初始化/反初始化 |
| Tests/Console/run_tests.py、Tests/Console/test_temperature_console.c、Tests/Console/test_bsp_console_rx.c、Tests/Console/stubs/main.h | 命令及 UART 接收行为的原生 GCC 测试 |
| Tests/Temperature/test_temperature_service.c | 手动值、非法不变、有效性与自动恢复测试 |
| Tests/App/test_app_main.c | 初始化、DHT 起始避让、发送失败观测和旧调度回归 |
| Docs/Plans/2026-10-21_Manual_Temperature.md | 方案及任务进度 |
| Docs/SerialWeb.md、Docs/Protocol.md、Docs/Hardware_Resources.md、Docs/Project_State.md、本文件 | 当前接口、资源、验证范围及操作说明 |

CubeMX 本次及保留性试验均删除 LAN8742/LICENSE.txt，已恢复原始字节；该文件不属于功能改动。

## 调用链和实时约束

```text
USART1_IRQHandler → HAL_UART_IRQHandler
    → HAL_UART_RxCpltCallback / HAL_UART_ErrorCallback
    → 复制字节/标记中断流缺口/单次 HAL_UART_Receive_IT 续收

App_Init → TemperatureService_Init → TemperatureConsole_Init → BSP_Console_RxInit
App_Process → 原网络/PTP → TemperatureService_Process
    → TemperatureConsole_Process → TemperatureService_SetManual / UseAuto / GetSnapshot
    → SendLine → 既有有界日志队列 → BSP_Console_Write
```

新 IRQ 仅 USART1（5/0）；SysTick 保持原配置。中断不解析、不打印、不等待。接收缓冲 256 字节、实际可排队 255 字节，无动态分配；主循环用短 PRIMASK 临界区保护读取、丢弃和续收，不修改 NVIC。初始化调用 HAL_UART_AbortReceive 清理旧接收，工程没有 UART DMA；续收失败最多每 10 ms 重试一次。

每轮最多读取 64 个字节、执行一条命令；响应至少相隔 100 ms。DHT11 起始低电平时推迟命令处理，接收中断继续入队。过长行、非法字节和 UART 错误/缓冲溢出后丢弃至 LF，防止残缺设温命令执行。遇到接收缺口时，旧队列整体作废；下一条命令也可能被丢弃以重新定位边界，可先发送一个空行，再重发完整命令。溢出计数不是逐字节丢失计数。

无新增 DMA；原 ETH DMA、网络保留区和缓存状态不变，不增加缓存维护。主栈顶保持 0x20048000，下界由 0x20047C00 改为 0x20047000，预留从 1 KiB 增至 4 KiB；响应采用主循环专用静态缓冲，避免嵌套大数组。自动模式原 `$TEMP` 字段顺序保持兼容。UNREADY 初次版本为 0；手动返回自动后保留此前版本但 VALID=0，直到下一次有效发布才递增。版本到 UINT32_MAX 后停止发布新值并报告 VERSION，不把不同值归到相同版本。

## 已执行的软件验证

- 先增加测试，确认缺失 API/调度失败，再实现温度模式、命令和接收逻辑；手动服务扩展及原用例合计 1381 次检查通过。
- `python -B Tests/Console/run_tests.py`：分段/粘连、正负/零温、范围/语法拒绝、63/64 字节边界、NUL/缺口恢复、100 ms 限流和 tick 回绕；UART 缓冲回绕、外来句柄、ORE、非阻塞错误、初始化/续收失败、溢出及 PRIMASK 保持通过。
- App、Network、Udp、UdpProbe、Ptp、Sync、TimeExchange、Temperature 原生测试通过；Tools/Monitor/tests 与 Analysis/tests 全部通过。日志位于忽略目录 Logs/Temperature/T1_2/check-*.log，最终汇总 final-software-checks.json（15 组命令全部返回 0）。
- 现有 `Tools/Monitor/autodebug.py build --preset Debug` 和 `--preset Release` 构建通过；`check_eth_layout.py --preset Debug/Release` 均通过。
- 最终实际运行 CubeMX Generate Code，483 个已纳入检查的文件无字节变化；生成器删除的许可证单独恢复原文。最终记录 Logs/Temperature/T1_2/regen-final/result.json；初次 482 文件检查另保留。
- Debug 反汇编确认原串口 HAL IRQ 入口约 240 字节栈，响应函数原先嵌套缓冲约 576 字节；增加 4 KiB 栈预留并改静态响应缓冲。新增布局测试能拒绝旧 1 KiB 栈 ELF（stack-guard-red.log），新 ELF 两配置通过；监控工具测试新增后全部通过。
- 本地逐项复核通过；独立审查本次未执行成功。

## 实物验收流程（本次已执行）

1. 已固定 Debug ELF 哈希并获得本版本授权，沿用现有 ST-LINK 配置编程、校验、复位，不改选项字节。
2. 采集 COM3 与原三路 UDP；确认新 BOOT、连续心跳及 PTP LOCAL/SYNC=0。
3. 查询 AUTO 有效值，再 SET 25.0，保持至少 15 秒，确认模式不变、版本不变、声速 346450；SET 0/-10.0 核对 331300/325240。
4. 越界/坏格式必须拒绝且查询仍为前一个值；分段命令仅在换行后执行。
5. TEMP AUTO 立即失效旧手动值，两帧后查询为 AUTO/DHT11/VALID=1。核对原三路 UDP、30 组请求及时间戳/板端计算，温度/UART 错误计数。
6. 未拔插传感器；未验收第二板、LCD、测距或双板同步。按本次实物通过范围创建功能提交，作者/提交者 jxq。

## 已烧录固件标识

- Debug SHA256：`824d3c3cecaa17bf93827e9b639d625cb37cd917ca9d23935b40f2d64c6519d3`。
- Release SHA256：`e98e2154b146a51449bfc2e5a63a37ef35777a309b9e056496b16ca1cbf78d6e`。
- 两配置构建/布局及原有业务回归通过，125 个 C 编译单元均启用 HAL_ETH_USE_PTP。
- 源文件哈希、构建产物及独立审查不可用状态记录于 Logs/Temperature/T1_2/preflash-review.json。本版本另获明确烧录授权，未沿用 R1 授权。实测结论如下。

## 90秒联合实测结果

- 仅烧录一次，OpenOCD 编程/校验/复位通过；BOOT字段 FW=SBCG-T1.2-MANUAL，HCLK=200000000，UID 与当前板一致；捕获内仅一次 BOOT，无额外重启、无前缀拼接、无串口读取错误。
- PING 成功；4次 SET 成功，分别为25.0℃、0℃、-10.0℃、分段发送的20.0℃。声速依次346450、331300、325240、343420 mm/s。
- 25.0℃的查询 AGE_MS 达16250，手动模式、有效值和VERSION=2保持不变。60.1、25.00、1e1三次输入被拒绝，其后的查询仍保持负温值及原版本。
- 分段前半 `TEMP SET 20` 无执行响应，补发 `.0` 加CRLF后才成功。TEMP AUTO立即返回AUTO/NONE/VALID=0，旧手动值省略；重新预读后恢复AUTO/DHT11/VALID=1。
- 13个正式自动采样版本，当前窗口均为26.9℃、347601 mm/s；湿度首次36%、后续34%。合计4个手动版本，最终VERSION=17；ATT=15、OK=13、PRIME=2，INIT/BUS/START/TIMEOUT/PULSE/CLOCK/CRC/RANGE/FORMAT全0。所有查询的RX_ERRORS/RX_OVERFLOWS/RX_REARM_ERRORS为0。
- UDP5005窗口70包，SEQ=6～75，无缺口、重复、乱序、非法或外来包；旧30组RX/TX/ACK、新30组ACK/REPORT/四时刻以及30组板端计算与真实C匹配均通过。
- 90条心跳及90条PTP连续，TX_ERRORS=0，LOCAL/SYNC=0；心跳相邻tick为1000～1006ms。UDP相邻tick为999～1001ms，相位 tick_ms−1000×SEQ 为16～17ms，跨度1ms，网络包与串口同SEQ的tick全部一致。
- 新增中断及静态响应缓冲在本次联合负载下运行正常；4KiB主栈保留的布局检查通过。本次没有测量所有可能路径的栈高水位，也不宣称同步或测距精度达标。
- 捕获已结束，串口线程停止、COM3已关闭。原始证据不改写；本轮未执行传感器拔插恢复验收。

分析初次因沿用旧脚本的“相邻UDP tick必须至少1000ms”断言而停止。核对现有UDPHeartbeat_Process后确认其按last_slot += slots×1000保持时隙，晚1ms后回到时隙可产生999ms间隔。分析修正为相邻间隔980～1020ms、时隙相位跨度不超过20ms、逐包同SEQ串口tick完全一致，同时保留无缺口/无错误等全部检查；没有修改固件、原始日志或重烧。初次脚本和原因保存在 analyzer-before-cadence-correction.py、cadence-check-correction.json 中。本轮实际相位跨度为1ms；该边界是调度回归检查，不是课程同步指标。

证据（位于git忽略目录）：Logs/Temperature/T1_2/runtime-run.json、baseline-acceptance.json、runtime-comparison.json；原始前缀 Logs/UDP/t12-manual-1792598209136，包括串口二进制/JSONL、发送命令JSONL、3路UDP及烧录记录，联合报告保存7份原始文件的SHA256。软件最终记录 final-software-checks.json、preflash-review.json 和 regen-final/result.json。独立审查本轮未能运行；结论依据本地复核、原生测试与上述实际采集，未宣称独立审查通过。

后续更新（2026-10-22）：本固件另经用户授权进行一次DHT11物理拔插，恢复功能通过；电脑端曾中断采集，未证明整段连续网络零丢包，详见[物理拔插报告](T1_1_Temperature_Replug.md)。上文90秒窗口与本轮故障观察分别保留。

连续补测（2026-10-22）：同一固件完成约488.9秒不间断拔插验收，488包UDP无缺口、两组30请求及真实C对照通过；故障旧值保持、15秒边界附近状态、无效查询和重新预读恢复已连续捕获。详见[PASS_CONTINUOUS_REPLUG记录](T1_1_Temperature_Replug.md)。首轮空档保留，不回填原始日志。
